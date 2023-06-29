#include <iostream>
#include <unordered_map>
#include <fstream>
#include <ctime>
#include <filesystem>
#include <regex>
#include "nlohmann/json.hpp"
#include "algorithms/LRU_K.h"
#include "algorithms/CLOCK.h"
#include "algorithms/ARC.h"
#include "algorithms/CAR.h"
#include "algorithms/LRU.h"
//Threading
#include <thread>
#include <barrier>
#include <mutex>
#include <condition_variable>
#include <zlib.h>
#include "tests/cprng.h"
#include <random>
#include "tests/test.h"
#include <unordered_set>

#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

#define SERVER 1

static constexpr size_t MAX_PAGE_CACHE_SIZE = 507569; // pages = `$ ulimit -l`/4  ~= 2GB mem
#ifdef SERVER
static constexpr size_t page_cache_size = 8*1024*1024; // ~ 128 KB mem
#else
static constexpr size_t max_page_cache_size = 256*1024; // ~ 128 KB mem
#endif
static constexpr size_t TEXT_LINE_SIZE_BYTES = 16; // "W0x7fffffffd9a8\n"*1 (===sizeof(char))
static constexpr size_t BIN_ADDR_BYTES = 8;
static constexpr size_t BIN_RW_BYTES = 1;
static constexpr size_t BIN_LINE_SIZE_BYTES = BIN_ADDR_BYTES+BIN_RW_BYTES; // "W0x7fffffffd9a8\n"*1 (===sizeof(char))

static const size_t max_num_threads = std::thread::hardware_concurrency();
static const size_t num_array_comp_threads = (max_num_threads > 16 ? max_num_threads/4 : 2);

static double round_to_precision(double f, uint8_t nd){
    const auto tens = static_cast<double>(std::pow(10,nd));
    return std::round(f*tens)/tens;
}

size_t parseMemoryString(const std::string& memoryString) {
    std::string strippedString = memoryString;
    strippedString.erase(std::remove_if(strippedString.begin(), strippedString.end(), ::isspace), strippedString.end());

    size_t value = 0;
    size_t multiplier = 1;

    std::istringstream iss(strippedString);
    iss >> value;

    char unitChar;
    if (iss >> unitChar) {
        switch (unitChar) {
            case 'B':
                multiplier = 1;
                break;
            case 'K':
                multiplier = 1024;
                break;
            case 'M':
                multiplier = 1024 * 1024;
                break;
            case 'G':
                multiplier = 1024 * 1024 * 1024;
                break;
            case 'T':
                multiplier = static_cast<size_t>(1024) * 1024 * 1024 * 1024;
                break;
            default:
                throw std::invalid_argument("Invalid memory unit: " + strippedString);
        }
    } else {
        throw std::invalid_argument("Invalid memory string: " + strippedString);
    }

    return value * multiplier;
}

static std::string parseMemorySize(size_t num) {
    const auto units = std::array{"B", "KB", "MB", "GB", "TB"};
    size_t unitIndex = 0;

    while (num >= 1024 && unitIndex < (units.size() - 1)) {
        num /= 1024;
        unitIndex++;
    }

    std::ostringstream oss;
    oss << num << units[unitIndex];
    return oss.str();
}

struct Args {
    bool ratio_realistic = false;
    std::string db_file = "db.json";
    bool always_overwrite = false;
    std::string data_save_dir = "results/%mtp/%tst";
    std::string mem_trace_path;
    bool text_trace_format = false;
    size_t mem_size_in_pages = 0;

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
            } else if(arg == "-o" || arg == "--old-trace"){
                text_trace_format = true;
            } else if(arg=="-m") {
                mem_size_in_pages = parseMemoryString(argv[i++]);
            }
            else if (i == argc) {
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

        std::string bm_name = "unknown";
        const std::vector<std::string> KNOWN_BENCHMARKS = {"pmbench", "stream"};
        auto changed= false;
        for (const auto& bm : KNOWN_BENCHMARKS) {
            if (mem_trace_path_fs.native().find(bm) != std::string::npos) {
                bm_name = bm;
                changed = true;
                break;
            }
        }
        if(!changed) {
            const std::vector<std::string> KNOWN_PARSEC_BENCHMARKS = {"ferret","dedup"};
            for (const auto &bm: KNOWN_PARSEC_BENCHMARKS) {
                if (mem_trace_path_fs.native().find(bm) != std::string::npos) {
                    bm_name = "parsec/"+bm;
                    break;
                }
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
        if(mem_size_in_pages == 0) mem_size_in_pages = page_cache_size;
        //Add 'memory.txt'
        std::ofstream mem_file(data_save_dir+"/memory.txt");
        if(mem_file.is_open()) {
            mem_file << "memory_used=" << parseMemorySize(mem_size_in_pages) << "pages=" << parseMemorySize(mem_size_in_pages*PAGE_SIZE) << "memory";
            mem_file.close();
        }
        else{
            std::cerr<<"Couldn't create memory.txt file!" <<std::endl;
            exit(-1);
        }
    };
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
        std::ios::openmode mt_mode = std::ios::out;
        if(!args.text_trace_format) mt_mode |= std::ios::binary;
        std::ifstream mtf(full_path,mt_mode);
        if (!mtf.is_open()) {
            std::cerr << "Failed to open memory trace file" << std::endl;
            exit(-1);
        }
        uint64_t lds = 0, strs = 0;
        std::string line;


        std::function<bool(std::string&)> read;
        std::function<std::pair<uint8_t,uint64_t>(std::string&)> parse;

        if(args.text_trace_format) {
            read = [&mtf](std::string &line) { return bool(std::getline(mtf, line)); };
            parse = [](std::string &line){
                const uint8_t is_load = line[0]=='R';
                const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
                return std::pair{is_load,mem_address};
            };
        }else {
            read = [&mtf](std::string &line) { return bool(mtf.read(&line[0], BIN_LINE_SIZE_BYTES)); };
            parse = [](std::string &line){
                const uint8_t is_load = line[0]==0;
                const uint64_t mem_address = 0;
                memcpy((void *) &mem_address, &line[1], sizeof(uint64_t));
                return std::pair{is_load,mem_address};
            };
        }

        std::unordered_set<page_t> all_pages;
        while (read(line)) {
            auto[is_load,address] = parse(line);
            if (is_load) {
                lds += 1;
            } else {
                strs += 1;
            }
            all_pages.insert(page_start_from_mem_address(address));
        }
        db[full_path] = {{"loads", lds}, {"stores", strs}, {"ratio", round_to_precision(static_cast<double>(lds)/static_cast<double>(strs),4)}, {"count", lds + strs},{"n_unique",all_pages.size()}};
        dbf.seekp(0);
        dbf << db.dump();
    }
    return db;
}


struct SimpleRatio{
    const size_t num;
    const size_t denom;

    constexpr SimpleRatio(size_t num,size_t denom) : num(num),denom(denom){}
    [[nodiscard]] double toDouble() const {
        return static_cast<double>(num)/static_cast<double>(denom);
    }
};

static constexpr SimpleRatio REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO = {1,100};//0.01;
static constexpr SimpleRatio AVERAGE_SAMPLE_RATIO =  {1,20};//0.05;
static constexpr SimpleRatio ZERO_RATIO =  {0,1};
static constexpr SimpleRatio ONE_RATIO =  {1,1};

#define DENOM 5

template<typename T, std::size_t N, typename F, std::size_t... I>
constexpr auto create_array_impl(F&& func, std::index_sequence<I...>) {
    return std::array<T, N>{ {func(I)...} };
}
template<typename T, std::size_t N, typename F>
constexpr auto create_array(F&& func) {
    return create_array_impl<T, N>(std::forward<F>(func), std::make_index_sequence<N>{});
}


#ifdef SERVER
static constexpr size_t BUFFER_SIZE = 128*1024*1024;
#else
static constexpr size_t BUFFER_SIZE = 1024*1024;
#endif
static constexpr const int ALG_DIV_PRECISION = 3;
namespace page_cache_algs {
    enum type {LRU_t, GCLOCK_t, ARC_t, CAR_t, NUM_ALGS};
    static constexpr std::array all = {LRU_t, GCLOCK_t, ARC_t, CAR_t};
    std::unique_ptr<GenericAlgorithm> get_alg(type t,untracked_eviction::type u_t, size_t mem_size_in_pages = page_cache_size){
        switch(t){
            case LRU_t:
                return std::make_unique<LRU>(mem_size_in_pages,u_t);
            case GCLOCK_t:
                return std::make_unique<CLOCK>(mem_size_in_pages,u_t,1);
            case ARC_t:
                return std::make_unique<ARC>(mem_size_in_pages,u_t);
            case CAR_t:
                return std::make_unique<CAR>(mem_size_in_pages,u_t);
            default:
                return nullptr;
        }
    }
    std::string type_to_alg_name(type t){return get_alg(t,untracked_eviction::FIFO /*here FIFO doesn't matter; we just use the name*/)->name();}
}

typedef std::pair<const page_cache_algs::type,SimpleRatio> compared_t;
typedef std::pair<compared_t,compared_t> comparison_t;

static std::string double_to_string_with_precision(double div, int precision) {
    std::stringstream stream;
    stream << std::fixed << std::setprecision(precision) << round_to_precision(div,precision);
    return stream.str();
}

static inline std::string get_alg_div_name(page_cache_algs::type t,double div) {return page_cache_algs::type_to_alg_name(t)+'_'+ double_to_string_with_precision(div,ALG_DIV_PRECISION);}

static inline std::string get_alg_div_name(compared_t ct){return get_alg_div_name(ct.first,ct.second.toDouble());}

static const std::string NO_STANDALONE = "!";

struct ThreadWorkAlgs{
    const compared_t alg_info;
    std::string save_dir = NO_STANDALONE;
    const untracked_eviction::type untracked_eviction_alg;
    const size_t mem_size_in_pages;
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

typedef std::pair<uint64_t,uint64_t> temperature_change_log;
typedef std::vector<temperature_change_log> temp_log_t;

namespace consideration_methods{

    class Considerator {
    public:
        Considerator() = default;
        virtual ~Considerator() = default;
        virtual bool should_consider() = 0;
    };

    //Considers I memory accesses in J calls (assuming a call each encountered memory access)
    class I_in_J : public Considerator {
    public:
        I_in_J(size_t i, size_t j) : left_to_consider(i),left_in_batch(j),i(i),j(j){
            if(i>j){
                std::cerr << "I_in_J considerator: i is greater than j --> impossible";
                exit(-1);
            }
        }
    protected:
        size_t left_to_consider,left_in_batch;
        const size_t i,j;
    };

    class Probabilistic_I_in_J : public I_in_J{
    public:
        Probabilistic_I_in_J(size_t i,size_t j) : I_in_J(i,j){
            std::random_device dev;
            rng = std::mt19937(dev());
        }

        bool should_consider() override {
            if(left_to_consider == 0 && left_in_batch == 0) {
                left_to_consider = i;
                left_in_batch = j;
            }
            bool consider = false;

            if(left_to_consider != 0) {
                if (left_in_batch == left_to_consider) {
                    consider = true;
                } else {
                    //We must still consider, say, k memory accesses, out of the p left in our batch
                    //--> consider this memory access with a probability of k/p
                    std::uniform_int_distribution<std::mt19937::result_type> dist(1,left_in_batch);
                    consider = dist(rng) <= left_to_consider; // k successes out of p possible values --> P(consider)=k/p
                }
            }

            if(consider) left_to_consider --;
            left_in_batch--;
            return consider;
        }
    private:
        std::mt19937 rng;
    };

    class Sequential_I_in_J : public I_in_J {
    public:
        Sequential_I_in_J(size_t i,size_t j) : I_in_J(i,j){}
        bool should_consider() override{
            if(left_in_batch == 0) {
                left_to_consider = i;
                left_in_batch = j;
            }
            bool consider = left_to_consider > 0;
            if(consider) left_to_consider --;
            left_in_batch --;
            return consider;
        }
    };

    class Never_Consider : public Considerator{
    public:
        Never_Consider() = default;
        bool should_consider() override {
            return false;
        }
    };

    class Always_Consider : public Considerator{
    public:
        Always_Consider() = default;
        bool should_consider() override {
            return true;
        }
    };

    std::unique_ptr<Considerator> get_considerator(SimpleRatio ratio) {
        if(ratio.num == 0){
            return std::make_unique<Never_Consider>(Never_Consider());
        }
        else if(ratio.num == ratio.denom){
            return std::make_unique<Always_Consider>(Always_Consider());
        }
        else{// TODO : test with random i in j
            return std::make_unique<Sequential_I_in_J>(Sequential_I_in_J(ratio.num,ratio.denom));
        }
    }
}


struct AlgInThread{
    std::unique_ptr<GenericAlgorithm> alg;
    size_t considered_loads = 0, considered_stores = 0;
    uint8_t changed = true;
    const std::unique_ptr<consideration_methods::Considerator> considerator;
    uint64_t n_pfaults = 0, considered_pfaults = 0;
    const ThreadWorkAlgs twa;
};

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

static void simulate_one(
#ifdef SERVER
        const char* mmap_file_address,
        size_t total_length,
        bool text_trace_format,
#else
        std::barrier<>& it_barrier,
#endif
        ThreadWorkAlgs twa){
    auto tid = std::this_thread::get_id();
    size_t n_writes = 0,seen = 0;
    //Create algs

    AlgInThread ait = {.alg=page_cache_algs::get_alg(twa.alg_info.first,twa.untracked_eviction_alg,twa.mem_size_in_pages),
                       .considerator=consideration_methods::get_considerator(twa.alg_info.second),
                       .twa=std::move(twa)};
    std::cout << tid << ": Waiting for first fill and starting..." << std::endl;

#ifndef SERVER
    while(true){
#else
    size_t at = 0;
    std::function<std::tuple<bool,uint8_t,uint64_t>()> parse;
    if(text_trace_format){
        parse = [mmap_file_address,&at,tid](){
            auto is_load = mmap_file_address[at++] == 'R';
            char* str_end = nullptr;
            auto address = std::strtoull(mmap_file_address+at,&str_end,16);
            bool success = true;
            if (errno == ERANGE)
            {
                errno = 0;
                std::cout << tid << " - range error, exiting";
                success = false;
            }
            at += (str_end+1-(mmap_file_address+at)); //`+1` skips the \n
            return std::tuple{success,is_load,address};
        };
    }
    else{
        parse = [mmap_file_address,&at](){
            auto is_load = mmap_file_address[at++] == 0;
            const uint64_t mem_address = 0;
            memcpy((void *) &mem_address, mmap_file_address+at, sizeof(uint64_t));
            at += sizeof(uint64_t);
            return std::tuple{true,is_load,mem_address};
        };
    }
    //auto should_break = (ait.twa.alg_info.second.num== ait.twa.alg_info.second.denom) && (ait.twa.alg_info.first == page_cache_algs::LRU_t) && (ait.twa.save_dir.find("random") != std::string::npos);

    while(at < total_length){
#endif
#ifndef SERVER
        //Wait to be notified you can go ; === value to be 0
        {
            std::unique_lock<std::mutex> lk(it_mutex);
            it_cv.wait(lk,[](){return num_ready==0;});
        }
        it_barrier.arrive_and_wait();
        if(!continue_running){
            break;
        }
#endif
        for(size_t i = 0;i<BUFFER_SIZE && at < total_length;i++){ // preserve for loop's behavior of saving every BUFFER_SIZE iterartions
#ifndef SERVER
            auto page_base = page_start_from_mem_address(mem_address_buf[i]);
            auto is_load = mem_reqtype_buf[i];
#else
            const auto[parse_success,is_load,address] = parse();
            if(!parse_success) break;
            auto page_base = page_start_from_mem_address(address);
#endif

            seen += 1;
            if (seen % 500'000'000 == 0){
                std::stringstream ss;
                ss << tid << " - "<< get_alg_div_name(ait.twa.alg_info) <<" - Reached seen = " << seen << "\n"
                   << "SampleRate=" << ait.twa.alg_info.second.toDouble() << ",#T="
                   << ait.considered_loads + ait.considered_stores << " (#S="
                   << ait.considered_stores << ",#L=" << ait.considered_loads;
                ss << "), n_writes=" << n_writes <<
#ifndef SERVER
                ",i=" << i;
#else
                ",at=" << at;
#endif
                std::cout<<ss.str()<<std::endl;
            }
            auto pfault = ait.alg->is_page_fault(page_base);

            if (pfault) {
                ait.n_pfaults++;
            }

            if(ait.considerator->should_consider()){
                // if(should_break && (ait.alg->get_total_size() == ait.alg->get_max_page_cache_size())){
                //     size_t test = 0;
                // }
                if(is_load){
                    ait.considered_loads++;
                }else{
                    ait.considered_stores++;
                }
                if(pfault){
                    ait.considered_pfaults++;
                }

                ait.changed = ait.alg->consume(page_base,true);
            }
            else if(pfault){
                ait.changed = ait.alg->consume(page_base,false);
            }
            //else no need to add to U, since we don't have a page fault
            //Sanity check
            if(ait.alg->get_total_size() > ait.alg->get_max_page_cache_size()){
                std::cerr<< get_alg_div_name(ait.twa.alg_info) <<" - Max memory exceeded!"<<std::endl;
            }

        } //endfor

        std::ofstream ofs(ait.twa.save_dir + OTHER_DATA_FN,std::ios_base::out|std::ios_base::trunc);
        ofs << "seen,considered_l,considered_s,pfaults,considered_pfaults\n"
            << seen << SEPARATOR << ait.considered_loads << SEPARATOR << ait.considered_stores << SEPARATOR
            << ait.n_pfaults << SEPARATOR << ait.considered_pfaults << "\n";
        ofs.close();

        n_writes++;

#ifndef SERVER
        //Say we're ready!
        {
            std::unique_lock<std::mutex> lk(it_mutex);
            num_ready = num_ready + 1;
            it_cv.notify_all();
        }
#endif
    }

#ifndef SERVER
    {
        std::unique_lock<std::mutex> lk(it_mutex);
        num_ready = num_ready + 1;
        it_cv.notify_all();
    }
#endif
    std::cout<< tid << " - "<< get_alg_div_name(ait.twa.alg_info) << " Finished file reading; no last"<<std::endl;
}


static bool fill_array_and_updated_page_set(std::ifstream& f,std::unordered_set<page_t>& unique_pages,bool text_trace_format){
    size_t i = 0;
    std::string line;
    line.resize(BIN_LINE_SIZE_BYTES);

    std::function<bool(std::string&)> read;
    std::function<std::pair<uint8_t,uint64_t>(std::string&)> parse;

    if(text_trace_format) {
        read = [&f](std::string &line) { return bool(std::getline(f, line)); };
        parse = [](std::string &line){
            const uint8_t is_load = line[0]=='R';
            const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
            return std::pair{is_load,mem_address};
        };
    }else {
        read = [&f](std::string &line) { return bool(f.read(&line[0], BIN_LINE_SIZE_BYTES)); };
        parse = [](std::string &line){
            const uint8_t is_load = line[0]==0;
            const uint64_t mem_address = 0;
            memcpy((void *) &mem_address, &line[1], sizeof(uint64_t));
            return std::pair{is_load,mem_address};
        };
    }


    for(; i<BUFFER_SIZE && read(line); i++){
        auto parsed = parse(line);
        const page_t page_start = page_start_from_mem_address(parsed.second);
        unique_pages.insert(page_start);
        mem_address_buf[i] = (uint64_t)page_start;
        mem_reqtype_buf[i] = parsed.first;
    }
    return i==BUFFER_SIZE;
}

//#define BIGSKIP 7228143
#ifdef BIGSKIP
#define RELAX_NUM_LINES 1000
#endif

static void reader_thread(std::string path_to_mem_trace,std::string parent_dir, bool text_trace_format){
    const auto total_nm_processes = num_ready;
    const std::string id_str = "READER PROCESS -";
    std::ios::openmode mode = std::ios::out;
    if(!text_trace_format) mode |= std::ios::binary;
    std::ifstream f(path_to_mem_trace,mode);
    std::unordered_set<page_t> unique_pages{};
#ifdef SERVER
    auto stop_condition = [](size_t read){return read>510'000'000;};
#else
    auto stop_condition = [](size_t read){return read>520'000'000;};
#endif
    if (f.is_open()) {
#ifdef BIGSKIP
        //After analysis, a new unique page for this benchmark arrives every ~2000 mem accesses before access number
        // 7228143 and every 50 accesses afterwards. To skip this big uninteresting area of 7 million memory accesses,
        // we simply seek a little ahead in the file
        auto left_to_skip = (BIGSKIP-RELAX_NUM_LINES)*(text_trace_format ? TEXT_LINE_SIZE_BYTES : BIN_LINE_SIZE_BYTES;
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
            if(fill_array_and_updated_page_set(f,unique_pages,text_trace_format)){
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
        f.close();
    }
    continue_running = false;
    {
        std::unique_lock<std::mutex> lk(it_mutex);
        num_ready = 0;
        it_cv.notify_all();
        it_cv.wait(lk,[total_nm_processes](){return num_ready==total_nm_processes;});
    }
    std::ofstream u_p_file(parent_dir+"n_unique_pages");
    if (u_p_file.is_open()) {
        u_p_file << unique_pages.size();
        u_p_file.close();
    }
}


void start(const Args& args) {

#ifdef SERVER
    int fd = open(args.mem_trace_path.c_str(), O_RDONLY);
    if (fd == -1){
        std::cout<<"Couldn't syscall open the file"<<std::endl;
        return;
    }

    // obtain file size
    struct stat sb;
    if (fstat(fd, &sb) == -1){
        std::cout<<"Couldn't fstat the file"<<std::endl;
        close(fd);
        return;
    }
    auto length = sb.st_size;
    const char* addr = static_cast<const char*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0u));

    close(fd); // man 2 mmap : "After the mmap() call has returned, the file  descriptor,  fd,  can  be closed immediately without invalidating the mapping."
    if (addr == MAP_FAILED){
        std::cout<<"Couldn't mmap the file"<<std::endl;
        return;
    }
    std::cout<<"Successfully mmaped the mem_trace, proceeding"<<std::endl;
#endif


    constexpr std::array special_samples_div = {REALISTIC_RATIO_SAMPLED_MEM_TRACE_RATIO,AVERAGE_SAMPLE_RATIO};

    constexpr auto divs_array = create_array<SimpleRatio,DENOM+1>([](auto i){return SimpleRatio(i,DENOM);});

    const auto samples_div = dual_container_range(special_samples_div,divs_array);

    const std::string base_dir_posix = fs::path(args.data_save_dir).lexically_normal().string() + "/";

    const size_t num_comp_processes = samples_div.size() * page_cache_algs::NUM_ALGS * 2;

    //Setup shared Synchronisation and Memory
    num_ready = num_comp_processes;
    std::barrier it_barrier(static_cast<long>(num_comp_processes));

    std::vector<std::jthread> all_threads{};
    all_threads.reserve(num_comp_processes);

    for(auto u_eviction_type : untracked_eviction::all) {
        const auto prefix = untracked_eviction::get_prefix(u_eviction_type)+"/";
        for (auto &div_ratio: samples_div) {
            auto div = div_ratio.toDouble();
            for (auto alg: page_cache_algs::all) {
                auto path = fs::path(base_dir_posix + prefix + get_alg_div_name(alg,div));
                fs::create_directories(path);
                auto save_dir = fs::absolute(path).lexically_normal().string() + '/';
                ThreadWorkAlgs t{{alg,div_ratio}, save_dir,u_eviction_type,args.mem_size_in_pages};
                all_threads.emplace_back(simulate_one,
#ifdef SERVER
                                         addr, length,args.text_trace_format,
#else
                                         std::ref(it_barrier),
#endif
                                         t);
            }
        }
    }

#ifndef SERVER
    std::jthread reader(reader_thread,args.mem_trace_path,base_dir_posix,args.text_trace_format);

    reader.join();
#endif

    for(auto& t : all_threads){
        t.join();
    }
#ifdef SERVER
    auto ret = munmap((void *) addr, length);
    if(ret)
        std::cout<<"Couldn't munmap the file..."<<std::endl;
#endif

    std::cout<<"Got all data!"<<std::endl;
}

template<typename It>
size_t indexof(It start, It end, double value) { return std::distance(start, std::find(start, end, value)); }

#define TESTING 0
#define JUST_DB 0

int main(int argc, char* argv[]) {
    const Args args(argc, argv);
    auto db = populate_or_get_db(args);
    (void)db;
#if JUST_DB
    return 0;
#endif
#if (BUILD_TYPE==0 && TESTING == 1)
    test_latest(args.mem_trace_path);
#else
    start(args);
#endif
    return 0;
}