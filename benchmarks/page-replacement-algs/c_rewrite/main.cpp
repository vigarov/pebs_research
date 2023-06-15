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
static constexpr size_t page_cache_size = 8*1024; // ~ 128 KB mem
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
static constexpr size_t BUFFER_SIZE = 1024*1024;

static constexpr const int ALG_DIV_PRECISION = 2;
namespace page_cache_algs {
    enum type {LRU_t, GCLOCK_t, ARC_t, CAR_t, NUM_ALGS};
    static constexpr std::array all = {LRU_t, GCLOCK_t, ARC_t, CAR_t};
    std::unique_ptr<GenericAlgorithm> get_alg(type t){
        switch(t){
            case LRU_t:
                return std::make_unique<LRU_K>(page_cache_size,K);
            case GCLOCK_t:
                return std::make_unique<CLOCK>(page_cache_size,K);
            case ARC_t:
                return std::make_unique<ARC>(page_cache_size);
            case CAR_t:
                return std::make_unique<CAR>(page_cache_size);
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
static constexpr double NO_RATIO = -1;

struct ThreadWorkAlgs{
    const compared_t alg_info;
    std::string save_dir = NO_STANDALONE;
    const double ratio;
    uint8_t is_userspace = 0;
};

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

struct AlgInThread{
    std::unique_ptr<GenericAlgorithm> alg;
    size_t considered_loads = 0, considered_stores = 0;
    uint8_t changed = true;
    const consideration_method considerationMethod;
    uint64_t n_pfaults = 0, considered_pfaults = 0;
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


static const std::string OTHER_DATA_FN = "stats.csv";


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


static constexpr char SEPARATOR = ',';

static void simulate_one(std::barrier<>& it_barrier, ThreadWorkAlgs twa){
    auto tid = std::this_thread::get_id();
    size_t n_writes = 0,seen = 0;
    //Create algs

    AlgInThread ait = {.alg=page_cache_algs::get_alg(twa.alg_info.first),.considerationMethod=consideration_methods::get_consideration_method(twa.is_userspace,twa.ratio!=NO_RATIO),.twa=std::move(twa)};
    std::cout << tid << ": Waiting for first fill and starting..." << std::endl;
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

        for(size_t i = 0;i<BUFFER_SIZE;i++){
            auto page_base = page_start_from_mem_address(mem_address_buf[i]);
            auto is_load = mem_reqtype_buf[i];

            seen += 1;
            if (seen % 100'000'000 == 0){
                std::stringstream ss;
                ss << tid << " - "<< get_alg_div_name(ait.twa.alg_info) <<" - Reached seen = " << seen << "\n"
                          << "SampleRate=" << ait.twa.alg_info.second << ",#T="
                          << ait.considered_loads + ait.considered_stores << " (#S="
                          << ait.considered_stores << ",#L=" << ait.considered_loads;
                if (ait.twa.ratio != NO_RATIO) {
                    ss << ", gt_ratio=" << ait.twa.ratio
                              << ", curr_ratio=" << (static_cast<double>(ait.considered_loads) / static_cast<double>(ait.considered_stores));
                }
                ss << "), n_writes=" << n_writes << ",i=" << i;
                std::cout<<ss.str()<<std::endl;
            }
            auto pfault = ait.alg->is_page_fault(page_base);
            //auto should_break = (alg.twa.alg_info.second == 1) && (alg.twa.standalone_save_dir != NO_STANDALONE) && (alg.twa.alg_info.first == page_cache_algs::LRU_t);
            if(ait.considerationMethod(pfault,ait,seen,is_load)){
                if(is_load){
                    ait.considered_loads++;
                }else{
                    ait.considered_stores++;
                }
                ait.changed = ait.alg->consume(page_base);
                if(pfault){
                    ait.considered_pfaults++;
                }
            }
            if (pfault) {
                ait.n_pfaults++;
            }
        }

        std::ofstream ofs(ait.twa.save_dir + OTHER_DATA_FN,std::ios_base::out|std::ios_base::trunc);
        ofs << "seen,considered_l,considered_s,pfaults,considered_pfaults\n"
            << seen << SEPARATOR << ait.considered_loads << SEPARATOR << ait.considered_stores << SEPARATOR
            << ait.n_pfaults << SEPARATOR << ait.considered_pfaults << "\n";
        ofs.close();

        n_writes++;
        //Say we're ready!
        {
            std::unique_lock<std::mutex> lk(it_mutex);
            num_ready = num_ready + 1;
            it_cv.notify_all();
        }
    }

    {
        std::unique_lock<std::mutex> lk(it_mutex);
        num_ready = num_ready + 1;
        it_cv.notify_all();
    }
    std::cout<< tid << " - "<< get_alg_div_name(ait.twa.alg_info) << " Finished file reading; no last"<<std::endl;
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

//#define BIGSKIP 7228143
#ifdef BIGSKIP
#define RELAX_NUM_LINES 1000
static const unsigned long long SKIP_OFF = (BIGSKIP-RELAX_NUM_LINES)*LINE_SIZE_BYTES;
#endif

static void reader_thread(std::string path_to_mem_trace){
    const auto total_nm_processes = num_ready;
    const std::string id_str = "READER PROCESS -";
    std::ifstream f(path_to_mem_trace);
    auto stop_condition = [](size_t read){return read>520'000'000;};
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
        while(!stop_condition(total_read)){
            //Get new data
            if(fill_array(f)){
                total_read+=BUFFER_SIZE;
            }
            else{
                break; //error or EOF
            }

            {
                std::unique_lock<std::mutex> lk(it_mutex);
                num_ready = 0;
                it_cv.notify_all();
            }
            
            n+=1;
            std::cout << id_str << " n=" << n << std::endl;

            {
                std::unique_lock<std::mutex> lk(it_mutex);
                it_cv.wait(lk,[total_nm_processes](){return num_ready==total_nm_processes;});
            }
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

static const std::string KERNEL_PREFIX = "kernel/";
static const std::string USERSPACE_PREFIX = "userspace/";

void start(const Args& args, const std::unordered_map<std::string, json>& db) {

    constexpr std::array samples_div = {REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO,
                                  AVERAGE_SAMPLE_RATIO,
                                  0.2,0.4,0.6,0.8,1.0};

    const std::string base_dir_posix = fs::path(args.data_save_dir).lexically_normal().string() + "/";

    constexpr size_t num_comp_processes = samples_div.size() * page_cache_algs::NUM_ALGS * 2;

    //Setup shared Synchronisation and Memory
    num_ready = num_comp_processes;
    std::barrier it_barrier(static_cast<long>(num_comp_processes));

    std::vector<std::jthread> all_threads{};
    all_threads.reserve(num_comp_processes);

    const auto ratio = db.at(args.mem_trace_path).at("ratio").get<double>();

    for(uint8_t is_userspace = 0; is_userspace <= 1 ; is_userspace++) {
        const auto& kernel_uspace_str = is_userspace ? USERSPACE_PREFIX : KERNEL_PREFIX;
        for (auto &div: samples_div) {
            for (auto alg: page_cache_algs::all) {
                auto path = fs::path(base_dir_posix + kernel_uspace_str + get_alg_div_name(alg,div));
                fs::create_directories(path);
                auto save_dir = fs::absolute(path).lexically_normal().string() + '/';
                ThreadWorkAlgs t{{alg,div}, save_dir, NO_RATIO, is_userspace};
                all_threads.emplace_back(simulate_one,std::ref(it_barrier),t);
                if(div == REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO) {
                    //Also create a Ratio thread
                    auto path_r = fs::path(base_dir_posix + kernel_uspace_str + get_alg_div_name(alg,div)+"_R");
                    fs::create_directories(path_r);
                    auto save_dir_r = fs::absolute(path_r).lexically_normal().string() + '/';
                    ThreadWorkAlgs t_r{{alg,div}, save_dir_r, ratio, is_userspace};
                    all_threads.emplace_back(simulate_one,std::ref(it_barrier),t_r);
                }
            }
        }
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