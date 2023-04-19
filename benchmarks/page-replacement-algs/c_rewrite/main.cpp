#include <iostream>
#include <unordered_map>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <algorithm>
#include <regex>
#include "nlohmann/json.hpp"
#include "algorithms/LRU.h"
#include "algorithms/CLOCK.h"
#include "algorithms/ARC.h"
#include "algorithms/CAR.h"
//Threading
#include <thread>
#include <barrier>
#include <mutex>
#include <condition_variable>
#include <zlib.h>
#include "tests/cprng.h"
#include <random>
#include "tests/test.h"

using json = nlohmann::json;
namespace fs = std::filesystem;

static constexpr uint8_t K = 2;
static constexpr size_t MAX_PAGE_CACHE_SIZE = 507569; // pages = `$ ulimit -l`/4  ~= 2GB mem
static constexpr size_t page_cache_size = 32*1024; // ~ 128 KB mem
static constexpr size_t LINE_SIZE_BYTES = 16; // "W0x7fffffffd9a8\n"*1 (===sizeof(char))

static const size_t max_num_threads = std::thread::hardware_concurrency();
static const size_t num_array_comp_threads = (max_num_threads > 16 ? max_num_threads/4 : 2);

std::string dtos(double f, uint8_t nd) {
    std::ostringstream ostr;
    const auto tens = static_cast<double>(std::pow(10,nd));
    ostr << std::round(f*tens)/tens;
    return ostr.str();
}

struct Args {
    bool ratio_realistic = false;
    std::string db_file = "db.json";
    bool always_overwrite = false;
    std::string data_save_dir = "results/%mtp/%tst";
    std::string mem_trace_path;

    Args(int argc, char* argv[]) {
        int i = 1;
        while (i < argc) {
            const std::string arg(argv[i++]);
            if (arg == "-r" || arg == "--ratio-realistic") {
                ratio_realistic = true;
            } else if (arg == "--db-file") {
                db_file = argv[i++];
            } else if (arg == "--always-overwrite") {
                always_overwrite = true;
            } else if (arg == "--data-save-dir") {
                data_save_dir = argv[i++];
            } else if (i == argc) {
                mem_trace_path = arg;
            } else {
                std::cerr << "Invalid argument: " << arg << std::endl;
                exit(-1);
            }
        }

        if (mem_trace_path.empty()) {
            std::cerr << "Missing argument: mem_trace_path" << std::endl;
            exit(-1);
        }

        const fs::path mem_trace_path_fs(mem_trace_path);
        if (!fs::exists(mem_trace_path_fs) || !fs::is_regular_file(mem_trace_path_fs)) {
            std::cerr << "Invalid mem_trace path: file does not exist!" << std::endl;
            exit(-1);
        }else{
            mem_trace_path = fs::absolute(mem_trace_path_fs).lexically_normal().string();
        }

        const std::vector<std::string> KNOWN_BENCHMARKS = {"pmbench", "stream"};
        std::string bm_name = "unknown";
        for (const auto& bm : KNOWN_BENCHMARKS) {
            if (mem_trace_path_fs.native().find(bm) != std::string::npos) {
                bm_name = bm;
                break;
            }
        }

        if (data_save_dir.find('!') != std::string::npos) {
            std::cerr << "Invalid data save dir, '!' detected" << std::endl;
            exit(-1);
        }

        std::time_t const t = std::time(nullptr);
        std::tm const tm = *std::localtime(&t);
        std::stringstream ss;
        ss << std::put_time(&tm, "%Y%m%d-%H%M%S");
        std::string const timestr = ss.str();

        data_save_dir = std::regex_replace(data_save_dir, std::regex("\\%\\%"), "!");
        data_save_dir = std::regex_replace(data_save_dir, std::regex("\\%mtp"), bm_name);
        data_save_dir = std::regex_replace(data_save_dir, std::regex("\\%tst"), timestr);
        data_save_dir = std::regex_replace(data_save_dir, std::regex("!"), "%");

        const fs::path data_save_dir_fs(data_save_dir);
        if (fs::exists(data_save_dir_fs) && !fs::is_directory(data_save_dir_fs)) {
            std::cerr << "Data save dir is not a directory. Exiting..." << std::endl;
            exit(-1);
        }

        fs::create_directories(data_save_dir_fs);
        data_save_dir = fs::absolute(data_save_dir_fs).lexically_normal().string();
        db_file = fs::absolute(db_file).lexically_normal().string();
        fs::create_directories(fs::path(db_file).parent_path());
    }
};

std::unordered_map<std::string, json> populate_or_get_db(const Args& args) {
    bool in_file;
    std::ios_base::openmode mode;
    if (fs::exists(args.db_file)) {
        mode = std::ios_base::in | std::ios_base::out;
        in_file = true;
    } else {
        mode = std::ios_base::out | std::ios_base::trunc;
        in_file = false;
    }
    std::fstream dbf(fs::absolute(args.db_file).lexically_normal().string(), mode);
    if (!dbf.is_open()) {
        std::cerr << "Failed to open db file" << std::endl;
        exit(-1);
    }
    json db{};
    if (in_file) {
        try {
            dbf >> db;
        } catch (const std::exception& e) {
            std::cerr << e.what() << " Couldn't parse JSON data from DB, exiting..." << std::endl;
            exit(-1);
        }
    }
    const std::string full_path = args.mem_trace_path;
    in_file = db.contains(full_path);
    if (!in_file) {
        std::ifstream mtf(full_path);
        if (!mtf.is_open()) {
            std::cerr << "Failed to open memory trace file" << std::endl;
            exit(-1);
        }
        int lds = 0, strs = 0;
        std::string line;
        while (std::getline(mtf, line)) {
            if (line[0] == 'R') {
                lds += 1;
            } else {
                strs += 1;
            }
        }
        db[full_path] = {{"loads", lds}, {"stores", strs}, {"ratio", dtos(static_cast<double>(lds)/static_cast<double>(strs),4)}, {"count", lds + strs}};
        dbf.seekp(0);
        dbf << db.dump();
    }
    return db;
}

static constexpr double REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO = 0.01;
static constexpr double AVERAGE_SAMPLE_RATIO = 0.05;
static constexpr size_t BUFFER_SIZE = 1024*256;

static constexpr const int ALG_DIV_PRECISION = 2;
namespace page_cache_algs {
    enum type {LRU_t, GCLOCK_t, ARC_t, CAR_t, NUM_ALGS};
    static constexpr std::array all = {LRU_t, GCLOCK_t, ARC_t, CAR_t};
    std::unique_ptr<GenericAlgorithm> get_alg(type t){
        switch(t){
            case LRU_t:
                return std::make_unique<LRU_K>(page_cache_size,K,num_array_comp_threads);
            case GCLOCK_t:
                return std::make_unique<CLOCK>(page_cache_size,K,num_array_comp_threads);
            case ARC_t:
                return std::make_unique<ARC>(page_cache_size,num_array_comp_threads);
            case CAR_t:
                return std::make_unique<CAR>(page_cache_size,num_array_comp_threads);
            default:
                return nullptr;
        }
    }
    std::string alg_to_name(type t){return get_alg(t)->name();}
}

typedef std::pair<const page_cache_algs::type,double> compared_t;
typedef std::pair<compared_t,compared_t> comparison_t;


static inline std::string get_alg_div_name(page_cache_algs::type t,double div) {return page_cache_algs::alg_to_name(t)+'_'+ dtos(div, ALG_DIV_PRECISION);}
static inline std::string get_alg_div_name(compared_t ct){return get_alg_div_name(ct.first,ct.second);}

static const std::string NO_STANDALONE = "!";

struct ThreadWorkAlgs{
    const compared_t alg_info;
    std::string standalone_save_dir = NO_STANDALONE;
    const double ratio;
    uint8_t is_userspace = 0;
};

typedef std::pair<ThreadWorkAlgs,ThreadWorkAlgs> thread_arg_t;

template<typename It>
size_t indexof(It start, It end, double value);


//Iteration finished synch
std::mutex it_mutex{};
std::condition_variable it_cv{};
volatile uint8_t num_ready;

//Continue running - doesn't need synchronisation atop of volatile
volatile uint8_t continue_running = true;

//Shared Buffers
std::array<page_t,BUFFER_SIZE> mem_address_buf{};
std::array<uint8_t,BUFFER_SIZE> mem_reqtype_buf{};

struct AlgInThread;


typedef bool (*consideration_method)(uint8_t is_page_fault, const AlgInThread& alg,size_t seen,uint8_t is_load);

typedef std::pair<uint64_t,uint64_t> temperature_change_log;
typedef std::vector<temperature_change_log> temp_log_t;

struct AddStandaloneInfo{
    std::string ptchange_dir;
    std::shared_ptr<temp_log_t> ptchanges{};
    uint64_t n_pfaults = 0, curr_pfault_distance_sum = 0, curr_non_pfault_distance_sum = 0;
    temp_t previous_sum = 1;
    bool emplaced_previously = true;
    std::optional<std::shared_ptr<nd_t>> necessary_data{};
};

struct AlgInThread{
    std::unique_ptr<GenericAlgorithm> alg;
    size_t considered_loads = 0, considered_stores = 0;
    uint8_t changed = true;
    const consideration_method considerationMethod;
    std::optional<AddStandaloneInfo> asi{};
    const ThreadWorkAlgs twa;
};

namespace consideration_methods{
    static inline bool consider_div(const AlgInThread &alg, size_t seen) {
        return (static_cast<double>(alg.considered_loads + alg.considered_stores) / static_cast<double>(seen)) <= alg.twa.alg_info.second;
    }

    static inline bool
    should_consider_div_kernel(uint8_t is_page_fault, const AlgInThread &alg, size_t seen, uint8_t is_load) {
        (void) is_load;
        return is_page_fault || consider_div(alg, seen);
    }

    static inline bool consider_ratio(const AlgInThread &alg, uint8_t is_load) {
        return alg.considered_stores == 0
            || (is_load && ((static_cast<double>(alg.considered_loads) / static_cast<double>(alg.considered_stores)) <= alg.twa.ratio))
               || (!is_load && (alg.twa.ratio <= (static_cast<double>(alg.considered_loads) / static_cast<double>(alg.considered_stores))));
    }


    static inline bool
    should_consider_ratio_kernel(uint8_t is_page_fault, const AlgInThread &alg, size_t seen, uint8_t is_load) {
        return should_consider_div_kernel(is_page_fault, alg, seen, is_load) && consider_ratio(alg, is_load);
    }

    static inline bool
    should_consider_div_uspace(uint8_t is_page_fault, const AlgInThread &alg, size_t seen, uint8_t is_load) {
        (void) is_load;
        (void) is_page_fault;
        return consider_div(alg, seen);
    }

    static inline bool
    should_consider_ratio_uspace(uint8_t is_page_fault, const AlgInThread &alg, size_t seen, uint8_t is_load) {
        return should_consider_div_uspace(is_page_fault, alg, seen, is_load) && consider_ratio(alg, is_load);
    }

    static constexpr consideration_method all[2][2] = {{should_consider_div_kernel, should_consider_ratio_kernel},
                                 {should_consider_div_uspace, should_consider_ratio_uspace}};


    static consideration_method get_consideration_method(bool is_userspace, bool is_ratio){
        return all[is_userspace][is_ratio];
    }
}


static const std::string PTCHANGE_DIR_NAME = "ptchange";
static const std::string OTHER_DATA_FN = "stats.csv";
static constexpr char SEPARATOR = ',';


template<typename T>
static typename std::enable_if<std::is_base_of<GenericAlgorithm,T>::value,temp_t>::type overall_distance_(const GenericAlgorithm& compared, const GenericAlgorithm& baseline){
    temp_t sum = 0;
    auto baseline_iterable = dynamic_cast<const T&>(baseline).get_cache_iterable();
    for (const auto &page: *baseline_iterable) {
        auto baseline_temp = baseline.get_temperature(page,std::nullopt);
        temp_t compared_temp = 0;
        if (!compared.is_page_fault(page)) {
            compared_temp = compared.get_temperature(page,std::nullopt);
        }
        sum += std::abs(static_cast<long long>(baseline_temp) - static_cast<long long>(compared_temp));
    }
    return sum;
}

static temp_t overall_distance(const GenericAlgorithm& compared, const GenericAlgorithm& baseline, page_cache_algs::type baseline_type){
    switch(baseline_type){
        case page_cache_algs::LRU_t:
            return overall_distance_<LRU_K>(compared,baseline);
        case page_cache_algs::GCLOCK_t:
            return overall_distance_<CLOCK>(compared,baseline);
        case page_cache_algs::ARC_t:
            return overall_distance_<ARC>(compared,baseline);
        case page_cache_algs::CAR_t:
            return overall_distance_<CAR>(compared,baseline);
        default:
            exit(-1);
    }
}


void save_to_file_compressed(const std::shared_ptr<temp_log_t>& array, const std::string& savedir, size_t number_writes) {
    const std::string filename = savedir + std::to_string(number_writes) + ".gz";
    gzFile f = gzopen(filename.c_str(), "wb");
    if (f == nullptr) {
        std::cerr << "Error: Could not open file " << filename << std::endl;
        return;
    }
    gzwrite(f, array->data(), array->size() * sizeof(temp_log_t::value_type));
    gzclose(f);
}

static void comparison_and_standalone(std::barrier<>& it_barrier, std::string comp_write_dir, thread_arg_t twas){
    auto tid = std::this_thread::get_id();
    size_t n_writes = 0,seen = 0;
    //Create algs

    AlgInThread ait1 = {.alg=page_cache_algs::get_alg(twas.first.alg_info.first),.considerationMethod=consideration_methods::get_consideration_method(twas.first.is_userspace,twas.first.ratio>0),.twa=std::move(twas.first)};
    AlgInThread ait2 = {.alg=page_cache_algs::get_alg(twas.second.alg_info.first),.considerationMethod=consideration_methods::get_consideration_method(twas.second.is_userspace,twas.second.ratio>0),.twa=std::move(twas.second)};
    {
        std::string alg_dir;
        if ((alg_dir=ait1.twa.standalone_save_dir) != NO_STANDALONE || (alg_dir=ait2.twa.standalone_save_dir) != NO_STANDALONE) {
            fs::path const ptchange_dir(alg_dir+PTCHANGE_DIR_NAME);
            fs::create_directories(ptchange_dir);
            auto ptchange_dir_abs = fs::absolute(ptchange_dir).lexically_normal().string()+"/";
            auto ptchanges = std::make_shared<temp_log_t>();
            ptchanges->reserve(BUFFER_SIZE/2);
            AddStandaloneInfo asi = {.ptchange_dir=ptchange_dir_abs,.ptchanges=std::move(ptchanges)};
            if(ait1.twa.standalone_save_dir != NO_STANDALONE) ait1.asi = std::move(asi);
            else ait2.asi = std::move(asi);
        }
    }

    std::cout << tid << " Waiting for first fill and starting..." << std::endl;
    std::array<AlgInThread,2> alg_arr{std::move(ait1),std::move(ait2)};
    while(true){
        //Wait to be notified you can go ; === value to be 0
        {
            std::unique_lock<std::mutex> lk(it_mutex);
            it_cv.wait(lk,[](){return num_ready==0;});
        }
        it_barrier.arrive_and_wait();
        if(!continue_running){
            break;
        }
        auto change_diffs = std::make_shared<temp_log_t>();
        change_diffs->reserve(BUFFER_SIZE/2);

        for(size_t i = 0;i<BUFFER_SIZE;i++){
            auto page_base = page_start_from_mem_address(mem_address_buf[i]);
            auto is_load = mem_reqtype_buf[i];

            seen += 1;
            for(auto& alg : alg_arr){
                if (seen % 1'000'000 == 0){
                    std::stringstream ss;
                    ss << tid << " - Reached seen = " << seen << "\n"
                              << "SampleRate=" << alg.twa.alg_info.second << ",#T="
                              << alg.considered_loads + alg.considered_stores << " (#S="
                              << alg.considered_stores << ",#L=" << alg.considered_loads;
                    if (alg.twa.ratio > 0) {
                        ss << ", gt_ratio=" << alg.twa.ratio
                                  << ", curr_ratio=" << (static_cast<double>(alg.considered_loads) / static_cast<double>(alg.considered_stores));
                    }
                    ss << "), n_writes=" << n_writes << ",i=" << i;
                    std::cout<<ss.str()<<std::endl;
                }
                auto pfault = alg.alg->is_page_fault(page_base);
                //auto should_break = (alg.twa.alg_info.second == 1) && (alg.twa.standalone_save_dir != NO_STANDALONE) && (alg.twa.alg_info.first == page_cache_algs::LRU_t);
                if(alg.considerationMethod(pfault,alg,seen,is_load)){
                    if(is_load){
                        alg.considered_loads++;
                    }else{
                        alg.considered_stores++;
                    }
                    alg.changed = alg.alg->consume(page_base);
                    //if(pfault && !alg.changed) std::cerr << alg.changed << " " << pfault << "Doesn't match!!" << std::endl;
                    auto& alg_asi = alg.asi;
                    if(alg_asi != std::nullopt){
                        temp_t md = 0;
                        if(alg.changed && alg_asi->necessary_data != std::nullopt){
                            md = alg.alg->compare_to_previous(*alg_asi->necessary_data);
                            if (pfault) {
                                alg_asi->n_pfaults++;
                                alg_asi->curr_pfault_distance_sum += md;
                            } else {
                                alg_asi->curr_non_pfault_distance_sum += md;
                            }
                            alg_asi->ptchanges->emplace_back(seen, md);
                        }
                        else{
                            md = 0;
                        }

                        if(md != alg_asi->previous_sum){
                            if(!alg_asi->emplaced_previously) alg_asi->ptchanges->emplace_back(seen-1, alg_asi->previous_sum);
                            alg_asi->ptchanges->emplace_back(seen, md);
                            alg_asi->previous_sum = md;
                            alg_asi->emplaced_previously = true;
                        }
                        else{
                            alg_asi->emplaced_previously = false;
                        }
                        alg_asi->necessary_data = alg.alg->get_necessary_data();
                    }
                }
            }
            if(alg_arr[0].changed || alg_arr[1].changed){
                change_diffs->emplace_back(seen, overall_distance(*alg_arr[0].alg,*alg_arr[1].alg,alg_arr[1].twa.alg_info.first));
            }
        }

        const AlgInThread* alg_to_use = nullptr;
        if(alg_arr[0].asi!=std::nullopt){
            alg_to_use = &alg_arr[0];
        }else if(alg_arr[1].asi!=std::nullopt){
            alg_to_use = &alg_arr[1];
        }
        if(alg_to_use!= nullptr){
            save_to_file_compressed(alg_to_use->asi->ptchanges,alg_to_use->asi->ptchange_dir,n_writes);
            std::ofstream ofs(alg_to_use->twa.standalone_save_dir + OTHER_DATA_FN,std::ios_base::out|std::ios_base::trunc);
            ofs << "seen,considered_l,considered_s,pfaults,pfault_dist_average,non_pfault_dist_average\n"
                << seen << SEPARATOR << alg_to_use->considered_loads << SEPARATOR << alg_to_use->considered_stores << SEPARATOR
                << alg_to_use->asi->n_pfaults << SEPARATOR
                << static_cast<double>(alg_to_use->asi->curr_pfault_distance_sum) / static_cast<double>(alg_to_use->asi->n_pfaults) << SEPARATOR
                << static_cast<double>(alg_to_use->asi->curr_non_pfault_distance_sum) / static_cast<double>((alg_to_use->considered_loads + alg_to_use->considered_stores - alg_to_use->asi->n_pfaults)) << "\n";
            ofs.close();
        }
        //Save comparisons
        save_to_file_compressed(change_diffs,comp_write_dir,n_writes);
        n_writes++;

        //Say we're ready!
        {
            std::unique_lock<std::mutex> lk(it_mutex);
            num_ready = num_ready + 1;
            it_cv.notify_all();
        }
    }
    std::cout<< tid << "Finished file reading"<<std::endl;
}


static bool fill_array(std::ifstream& f){
    size_t i = 0;
    for(std::string line; i<BUFFER_SIZE && std::getline(f,line);i++){
        const uint8_t is_load = line[0]=='R';
        const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
        const uint64_t page_start = page_start_from_mem_address(mem_address);
        mem_address_buf[i] = page_start;
        mem_reqtype_buf[i] = is_load;
    }
    return i==BUFFER_SIZE;
}

#define BIGSKIP 7228143
#ifdef BIGSKIP
#define RELAX_NUM_LINES 1000
static const unsigned long long SKIP_OFF = (BIGSKIP-RELAX_NUM_LINES)*LINE_SIZE_BYTES;
#endif

static void reader_thread(std::string path_to_mem_trace){
    const auto total_nm_processes = num_ready;
    const std::string id_str = "READER PROCESS -";
    std::ifstream f(path_to_mem_trace);
    auto stop_condition = [](size_t read){return read>100'000'000;};
    if (f.is_open()) {
#ifdef BIGSKIP
        //After analysis, a new unique page for this benchmark arrives every ~2000 mem accesses before access number
        // 7228143 and every 50 accesses afterwards. To skip this big uninteresting area of 7 million memory accesses,
        // we simply seek a little ahead in the file
        auto left_to_skip = SKIP_OFF;
        f.seekg(0,std::ios_base::end);
        std::cout<<"Total size=" <<f.tellg()<<std::endl;
        f.seekg(0,std::ios_base::beg);
        std::cout<<f.tellg()<<std::endl;
        while(left_to_skip!=0) {
            auto skip_ammount = static_cast<long>(std::min(static_cast<unsigned long long>(std::numeric_limits<long>::max()),left_to_skip));
            f.seekg(skip_ammount, std::ios_base::beg);
            left_to_skip-=skip_ammount;
        }
        std::cout<<f.tellg()<<std::endl;
#endif
        size_t n = 0,total_read = 0;
        if(!fill_array(f)){
            std::cerr<<id_str<<"Failed initial read, exiting..."<<std::endl;
            goto out;
        }
        total_read+=BUFFER_SIZE;
        std::cout << id_str << " First read success" <<std::endl;

        while(!stop_condition(total_read)){
            {
                std::unique_lock<std::mutex> lk(it_mutex);
                it_cv.wait(lk,[total_nm_processes](){return num_ready==total_nm_processes;});
                num_ready = 0;
                it_cv.notify_all();
            }

            //Get new data
            if(fill_array(f)){
                total_read+=BUFFER_SIZE;
            }
            else{
                break; //error or EOF
            }
            n+=1;
            std::cout << id_str << " n=" << n << std::endl;
        }
out:
        f.close();
    }
    continue_running = false;
    {
        std::unique_lock<std::mutex> lk(it_mutex);
        num_ready = 0;
        it_cv.notify_all();
        it_cv.wait(lk,[total_nm_processes](){return num_ready==total_nm_processes;});
    }
}

/*
void fill_buffers(std::string basicString) {
    std::ifstream f(basicString,std::ios_base::in);
    if(f.is_open()){
        //Reserve space for the buffers
        // Byte size 8'358'363'968
        f.seekg(0,std::ios_base::end);
        const size_t fSize = f.tellg();
        f.seekg(0,std::ios_base::beg);
        f.clear();
        const size_t num_elements = fSize/LINE_SIZE_BYTES;
        mem_address_buf.reserve(num_elements);
        mem_reqtype_buf.reserve(num_elements);
        std::string line;
        while(std::getline(f,line)){
            const uint8_t is_load = line[0]=='R';
            const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
            const uint64_t page_start = page_start_from_mem_address(mem_address);
            mem_address_buf.push_back(page_start);
            mem_reqtype_buf.push_back(is_load);
        }
        std::cout<<"Finished filling arrays!"<<std::endl;
    }
    else{
        exit(-1);
    }
    f.close();
}
*/
static const std::string KERNEL_PREFIX = "kernel/";
static const std::string USERSPACE_PREFIX = "userspace/";

std::pair<std::string,std::string> do_standalone(const std::string& standalone_div_root,std::vector<uint8_t> &done_standalone, page_cache_algs::type alg_type, size_t div_idx,double div_value ,size_t div_size = 0){
    size_t idx;
    std::string extra;
    if(div_size != 0 ){
        idx = alg_type + (page_cache_algs::NUM_ALGS * div_size);
        extra = "_R";
    }
    else{
        idx = alg_type + (page_cache_algs::NUM_ALGS * div_idx);
    }
    const bool already_done = done_standalone.at(idx);
    if(already_done) return {NO_STANDALONE,NO_STANDALONE};
    else{
        done_standalone[idx] = true;
        auto after_standalone_name_k = KERNEL_PREFIX + get_alg_div_name(alg_type,div_value) + extra;
        auto after_standalone_name_u = USERSPACE_PREFIX + get_alg_div_name(alg_type,div_value) + extra;
        auto p_k = fs::path(standalone_div_root+after_standalone_name_k);
        auto p_u = fs::path(standalone_div_root+after_standalone_name_u);
        fs::create_directories(p_k);
        fs::create_directories(p_u);
        return {fs::absolute(p_k).lexically_normal().string() + '/',fs::absolute(p_u).lexically_normal().string() + '/'};
    }
}

void start(const Args& args, const std::unordered_map<std::string, json>& db) {

    constexpr std::array samples_div = {REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO,
                                  AVERAGE_SAMPLE_RATIO,
                                  0.2,0.4,0.6,0.8,1.0};

    std::vector<std::string> alg_names;
    alg_names.reserve(page_cache_algs::NUM_ALGS);
    for (auto i : page_cache_algs::all) {
        alg_names.push_back(page_cache_algs::alg_to_name(i));
    }

    std::vector<comparison_t> non_ratio_comparisons;
    std::vector<comparison_t> ratio_comparisons;

// 1) Standalone and ratio
    for (auto& alg : page_cache_algs::all) {
        for (auto& div : samples_div) {
            if (div != 1.0) {
                const compared_t compared_alg_name(alg,div);
                const compared_t  baseline_alg_name(alg,1.0);
                non_ratio_comparisons.emplace_back(compared_alg_name,baseline_alg_name);
            }
        }
        // 1).ratio
        if (args.ratio_realistic) {
            {
                const compared_t compared_alg_name(alg, REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO);
                const compared_t baseline_alg_name(alg, REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO);
                ratio_comparisons.emplace_back(compared_alg_name, baseline_alg_name);
            }
            {
                const compared_t compared_alg_name(alg, REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO);
                const compared_t baseline_alg_name(alg, 1.0);
                ratio_comparisons.emplace_back(compared_alg_name, baseline_alg_name);
            }
        }
    }

// 2)
    for (size_t i = 0; i < page_cache_algs::all.size(); i += 2) {
        auto alg1 = page_cache_algs::all[i];
        auto alg2 = page_cache_algs::all[i + 1];
        for (auto& div : samples_div) {
            const compared_t compared_alg_name(alg1,div);
            const compared_t baseline_alg_name(alg2,div);
            non_ratio_comparisons.emplace_back(compared_alg_name,baseline_alg_name);
        }
    }

// 3)
    for (size_t i = 0; i < 2; i++) {
        auto alg1 = page_cache_algs::all[i];
        auto alg2 = page_cache_algs::all[i + 2];
        for (auto& div : samples_div) {
            const compared_t compared_alg_name(alg1,div);
            const compared_t baseline_alg_name(alg2,div);
            non_ratio_comparisons.emplace_back(compared_alg_name,baseline_alg_name);
        }
    }

/*  TODO
    //Fill the arrays buffers
    fill_buffers(args.mem_trace_path);
*/


    const std::string base_dir = fs::path(args.data_save_dir).lexically_normal().string() + "/";
    const fs::path standalone_dir = fs::path(base_dir + "standalone");
    fs::create_directories(standalone_dir);
    const std::string standalone_dir_as_posix = standalone_dir.lexically_normal().string() + "/";
    const fs::path comp_dir = fs::path(base_dir + "comp");
    fs::create_directories(comp_dir);
    const std::string comp_dir_as_posix = comp_dir.lexically_normal().string() + "/";

    const size_t num_comp_processes = (ratio_comparisons.size() + non_ratio_comparisons.size())*2; //*2 since we do userspace and kernel space

    //Setup shared Synchronisation and Memory
    num_ready = num_comp_processes;
    std::barrier it_barrier(static_cast<long>(num_comp_processes));

    std::vector<std::jthread> all_threads{};
    all_threads.reserve(num_comp_processes);

    std::vector<uint8_t> done_standalone((samples_div.size() + (args.ratio_realistic?1:0)) * page_cache_algs::NUM_ALGS,
                                         false);
    for(auto& comparison : non_ratio_comparisons){
        auto do_standalone_1 = do_standalone(standalone_dir_as_posix,done_standalone, comparison.first.first,indexof(samples_div.begin(),samples_div.end(), comparison.first.second),comparison.first.second);
        ThreadWorkAlgs t1{comparison.first, do_standalone_1.first, -1};
        auto standalone_2 = do_standalone_1.first == NO_STANDALONE ? do_standalone(standalone_dir_as_posix,done_standalone, comparison.second.first,indexof(samples_div.begin(),samples_div.end(), comparison.second.second),comparison.second.second) : std::pair<std::string,std::string>(NO_STANDALONE,NO_STANDALONE);
        ThreadWorkAlgs t2{comparison.second, standalone_2.first, -1};
        auto comp_save_dir_path_k = fs::path(comp_dir_as_posix+KERNEL_PREFIX+get_alg_div_name(comparison.first)+"_vs_"+get_alg_div_name(comparison.second));
        fs::create_directories(comp_save_dir_path_k);
        all_threads.emplace_back(comparison_and_standalone,std::ref(it_barrier),fs::absolute(comp_save_dir_path_k).lexically_normal().string()+'/',thread_arg_t({t1,t2}));
        t1.is_userspace = t2.is_userspace = 1;
        t1.standalone_save_dir = do_standalone_1.second;
        t2.standalone_save_dir = standalone_2.second;
        auto comp_save_dir_path_u = fs::path(comp_dir_as_posix+USERSPACE_PREFIX+get_alg_div_name(comparison.first)+"_vs_"+get_alg_div_name(comparison.second));
        fs::create_directories(comp_save_dir_path_u);
        all_threads.emplace_back(comparison_and_standalone,std::ref(it_barrier),fs::absolute(comp_save_dir_path_u).lexically_normal().string()+'/',thread_arg_t({t1,t2}));
    }
    for(auto& comparison : ratio_comparisons){
        auto do_standalone_1 = do_standalone(standalone_dir_as_posix,done_standalone, comparison.first.first,indexof(samples_div.begin(),samples_div.end(), comparison.first.second),comparison.first.second,samples_div.size());
        const auto ratio = db.at(args.mem_trace_path).at("ratio").get<double>();
        ThreadWorkAlgs t1{comparison.first, do_standalone_1.first, ratio};
        auto standalone_2 = do_standalone_1.first == NO_STANDALONE ? do_standalone(standalone_dir_as_posix,done_standalone, comparison.second.first,indexof(samples_div.begin(),samples_div.end(), comparison.second.second),comparison.second.second) : std::pair<std::string,std::string>(NO_STANDALONE,NO_STANDALONE);
        ThreadWorkAlgs t2{comparison.second, standalone_2.first, -1};
        auto comp_save_dir_path_k = fs::path(comp_dir_as_posix+KERNEL_PREFIX+get_alg_div_name(comparison.first)+"_vs_"+get_alg_div_name(comparison.second));
        fs::create_directories(comp_save_dir_path_k);
        all_threads.emplace_back(comparison_and_standalone,std::ref(it_barrier),fs::absolute(comp_save_dir_path_k).lexically_normal().string()+'/',thread_arg_t({t1,t2}));
        t1.is_userspace = t2.is_userspace = 1;
        t1.standalone_save_dir = do_standalone_1.second;
        t2.standalone_save_dir = standalone_2.second;
        auto comp_save_dir_path_u = fs::path(comp_dir_as_posix+USERSPACE_PREFIX+get_alg_div_name(comparison.first)+"_vs_"+get_alg_div_name(comparison.second));
        fs::create_directories(comp_save_dir_path_u);
        all_threads.emplace_back(comparison_and_standalone,std::ref(it_barrier),fs::absolute(comp_save_dir_path_u).lexically_normal().string()+'/',thread_arg_t({t1,t2}));
    }

    std::jthread reader(reader_thread,args.mem_trace_path);

    reader.join();

    for(auto& t : all_threads){
        t.join();
    }

    std::cout<<"Got all data!"<<std::endl;
}

template<typename It>
size_t indexof(It start, It end, double value) { return std::distance(start, std::find(start, end, value)); }

#define TESTING 0

int main(int argc, char* argv[]) {
    const Args args(argc, argv);
    auto db = populate_or_get_db(args);
#if (BUILD_TYPE==0 && TESTING == 1)
    test_latest(args.mem_trace_path);
#else
    start(args,db);
#endif
    return 0;
}