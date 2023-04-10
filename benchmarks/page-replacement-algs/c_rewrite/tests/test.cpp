#include <fstream>
#include <boost/unordered/unordered_flat_map.hpp>
#include <boost/unordered_map.hpp>
#include <random>
#include <chrono>
#include "test.h"
#include "../algorithms/LRU.h"
#include "../algorithms/CLOCK.h"
#include "../algorithms/ARC.h"
#include "../algorithms/CAR.h"
#include "cprng.h"
#include <cstdio>
#include <sys/stat.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <thread>


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void test_ma_small(){
    int K = 2;
    int page_cache_size = 3; // ~128 KB mem
    std::vector<GenericAlgorithm*> const my_algs = {
            new LRU_K(page_cache_size, K),
            new CLOCK(page_cache_size, K),
            new ARC(page_cache_size),
            new CAR(page_cache_size)
    };

    std::vector<std::string> const test_accesses = {"W0x7fffffffd9a8", "W0x7fffffffd980", "W0x7ffff7ffde0e", "W0x7ffff7ffdb78",
                                                    "R0x7ffff7ffcf80", "W0x7ffff7ffdcc0", "R0x7ffff7ffde0e", "R0x7ffff7ffdb50", "R0x7ffff7ffce98"};
    for (auto alg : my_algs) {
        int pfaults = 0;
        for (const auto& ta : test_accesses) {
            uint64_t const mem_address = std::stoull(ta.substr(1), nullptr, 16);
            uint64_t const page_start = page_start_from_mem_address(mem_address);
            if (alg->is_page_fault(page_start)) {
                pfaults++;
            }
            alg->consume(page_start);
        }
        auto tl = alg->get_page_cache_copy();
        std::sort(tl->begin(), tl->end(), [&](page_t a, page_t b){return alg->get_temperature(a,std::nullopt) < alg->get_temperature(b,std::nullopt);});
        std::cout << "Temperature List: [";
        for (auto e : *tl) {
            std::cout << " " << std::hex << e;
        }
        std::cout << " ] Pfaults: " << pfaults << std::endl;
    }
}

static void test_ma_diverse(){
    int K = 2;
    int page_cache_size = 3; // ~128 KB mem
    std::vector<GenericAlgorithm*> const my_algs = {
            new LRU_K(page_cache_size, K),
            new CLOCK(page_cache_size, K),
            new ARC(page_cache_size),
            new CAR(page_cache_size)
    };

    std::vector<std::string> const test_accesses = {
            "W0x7fffffffd9a8", "W0x7fffffffd980", "W0x7ffff7ffde0e", "W0x7ffff7ffdb78",
            "R0x7ffff7ffcf80", "W0x7ffff7ffdcc0", "R0x7ffff7ffce98"
    };
    for (auto alg : my_algs) {
        int pfaults = 0;
        for (const auto& ta : test_accesses) {
            uint64_t const mem_address = std::stoull(ta.substr(1), nullptr, 16);
            uint64_t const page_start = page_start_from_mem_address(mem_address);
            if (alg->is_page_fault(page_start)) {
                pfaults++;
            }
            alg->consume(page_start);
        }
        auto tl = alg->get_page_cache_copy();
        std::sort(tl->begin(), tl->end(), [&](page_t a, page_t b){return alg->get_temperature(a,std::nullopt) < alg->get_temperature(b,std::nullopt);});
        std::cout << "Temperature List: [";
        for (auto e : *tl) {
            std::cout << " " << std::hex << e;
        }
        std::cout << " ] Pfaults: " << pfaults << std::endl;
    }
}

static void test_ma_med(){
    int K = 2;
    int page_cache_size = 3; // ~128 KB mem
    std::vector<GenericAlgorithm*> const my_algs = {
            new LRU_K(page_cache_size, K),
            new CLOCK(page_cache_size, K),
            new ARC(page_cache_size),
            new CAR(page_cache_size)
    };

    std::vector<std::string> const test_accesses = {
            "R0x7ffff7ffc418", "R0x7fffffffe064", "R0x7ffff7ffc480", "R0x7ffff7ffc488", "R0x7ffff7ffc4f0",
            "R0x7ffff7ffc4f8", "R0x7ffff7ffc560", "W0x7fffffffd9a8", "W0x7fffffffd980", "W0x7ffff7ffde0e",
            "W0x7ffff7ffdb78", "R0x7ffff7ffcf80", "W0x7ffff7ffdcc0", "R0x7ffff7ffde0e", "R0x7ffff7ffdb50",
            "R0x7ffff7ffce98", "R0x7ffff7ffc568", "R0x7ffff7ffc5d0", "R0x7ffff7ffc5d8",
            "R0x7ffff7ffc640", "R0x7ffff7ffc648", "R0x7ffff7ffc6b0", "R0x7ffff7ffc6b8", "R0x7fffffffe064",
            "R0x7ffff7ffc720", "R0x7ffff7ffc728", "R0x7fffffffe064", "R0x7ffff7ffc790", "R0x7ffff7ffc798",
            "R0x7ffff7ffc800", "R0x7ffff7ffc808", "R0x7ffff7ffc870", "R0x7ffff7ffc878", "R0x7fffffffe064",
            "R0x7ffff7ffc8e0", "R0x7ffff7ffc8e8", "R0x7ffff7ffc950", "R0x7ffff7ffc958", "R0x7ffff7ffc9c0",
            "R0x7ffff7ffc9c8", "R0x7ffff7ffca30", "R0x7ffff7ffca38", "R0x7fffffffe064", "R0x7fffffffd7b0",
            "R0x7fffffffdab8", "R0x7fffffffe07c", "W0x7fffffffd7b0", "R0x7fffffffe07d", "R0x7fffffffe07e",
            "R0x7fffffffe07f", "R0x7fffffffe080", "R0x7fffffffe081", "R0x7fffffffe082", "R0x7fffffffe083",
            "R0x7fffffffe084", "R0x7fffffffe085", "R0x7fffffffe086", "R0x7fffffffe087", "W0x7fffffffd7b8",
            "R0x7fffffffe07c", "R0x7ffff7ff1279", "R0x7fffffffe07d", "R0x7ffff7ffca98"
    };
    for (auto alg : my_algs) {
        int pfaults = 0;
        for (const auto& ta : test_accesses) {
            uint64_t const mem_address = std::stoull(ta.substr(1), nullptr, 16);
            uint64_t const page_start = page_start_from_mem_address(mem_address);
            if (alg->is_page_fault(page_start)) {
                pfaults++;
            }
            alg->consume(page_start);
        }
        auto tl = alg->get_page_cache_copy();
        std::sort(tl->begin(), tl->end(), [&](page_t a, page_t b){return alg->get_temperature(a,std::nullopt) < alg->get_temperature(b,std::nullopt);});
        std::cout << "Temperature List: [";
        for (auto e : *tl) {
            std::cout << " " << std::hex << e;
        }
        std::cout << " ] Pfaults: " << pfaults << std::endl;
    }
}

static void test_ma_all(){
    test_ma_small();
    test_ma_diverse();
    test_ma_med();
}
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void perf_separator_test1(std::unordered_map<uint64_t, LRU_page_data> &am, const page_t page_start) { am[page_start]; }

static void perf_separator_test2(boost::unordered_flat_map<uint64_t, LRU_page_data> &bm, const page_t page_start) { bm[page_start]; }

static void perf_separator_test3(boost::unordered_map<uint64_t, LRU_page_data> &cm, const page_t page_start) { cm[page_start]; }

static void test_file_maps(const std::string& path_to_mem_trace){
    std::ifstream f(path_to_mem_trace);
    if (f.is_open()) {
        std::unordered_map<uint64_t, LRU_page_data> am{};
        boost::unordered_flat_map<uint64_t,LRU_page_data> bm{};
        boost::unordered_map<uint64_t, LRU_page_data> cm{};
        std::string line;
        for (unsigned long long i = 0; i < 10*FUZZ_SIZE; i++) {
            std::getline(f,line);
            const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
            const auto page_start = page_start_from_mem_address(mem_address);
            perf_separator_test1(am, page_start);
            perf_separator_test2(bm, page_start);
            perf_separator_test3(cm, page_start);
        }
        std::cout<< "am:"<<am.size() << " "<<am.bucket_count()<<'\n';
        std::cout<< "bm:"<<bm.size() << " "<<bm.bucket_count()<<'\n';
        std::cout<< "cm:"<<cm.size() << " "<<cm.bucket_count()<<'\n';
    }
    f.close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

static void test_fuzz(){
    auto actual_size = 8*1024;
    std::vector<GenericAlgorithm*> const my_algs = {
            new LRU_K(actual_size, 2),
            new CLOCK(actual_size, 2),
            new ARC(actual_size),
            new CAR(actual_size)
    };

    unsigned long long smth = 0;

    for (auto alg : my_algs) {
        int pfaults = 0;
#ifdef CLIONDONT
        for (const auto& a : random_addresses) {
            auto page_start = page_start_from_mem_address(a);
#else
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<unsigned long long> dis(std::numeric_limits<std::uint64_t>::min(),std::numeric_limits<std::uint64_t>::max());
        constexpr auto REPETITION_FACTOR = 100;

        for(size_t i =0; i<FUZZ_SIZE;i+=REPETITION_FACTOR){
            const auto page_start = page_start_from_mem_address(dis(gen));
            for(auto j = 0; j<REPETITION_FACTOR;j++) {
#endif
                if (alg->is_page_fault(page_start)) {
                    pfaults++;
                }
                alg->consume(page_start);
#ifndef CLIONDONT
            }
            /*auto tl = alg->get_page_cache_copy();
            std::sort(tl->begin(), tl->end(), [&](page_t a, page_t b){return alg->get_temperature(a,std::nullopt) < alg->get_temperature(b,std::nullopt);});
            smth += (*tl)[0];*/
#endif
        }
    }
    std::cout<<smth;
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

struct BOOST_TEST_LRU_nd{
    boost::unordered_flat_map<page_t,LRU_page_data> prev_page_to_data;
};

namespace test {
    class BOOST_TEST_LRU {
    public:
        explicit BOOST_TEST_LRU(uint16_t page_cache_size) : page_cache_size(page_cache_size) {};

        bool consume(page_t page_start) {
            bool changed = true;
            count_stamp += 1;
            auto page_fault = is_page_fault(page_start);
            auto &page_data = page_to_data[page_start];
            auto &histories = page_data.history;
            //For K!=2, if(histories.size() != K) {/*set K empty elements*/}
            histories.pop_back();
            histories.push_front(count_stamp);
            if (page_fault) {
                if (page_cache.size() == page_cache_size) { //Full, must replace
                    auto victim_page_it = std::prev(page_cache.end());
                    page_to_data.erase(*victim_page_it);
                    page_cache.erase(victim_page_it);
                }
                page_data.index = page_cache.size();
                page_data.at_iterator = page_cache.insert(page_cache.end(), page_start);
                //Keeps sort since page_fault --> not in history --> K-1 = 0 == min
            } else { //page in cache
                //Keep the page cache sorted; page_data[K-1] must be compared to all subsequent page_data[K-1]
                auto it = page_data.at_iterator;
                if (it != page_cache.begin()) {
                    auto page_before = std::prev(page_cache.erase(it));
                    while (page_before != page_cache.begin() &&
                           page_to_data[*page_before].history[2 - 1] < histories[2 - 1]) {
                        page_to_data[*page_before].index++;
                        page_before = std::prev(page_before);
                        page_data.index--;
                    }
                    if (page_before != page_cache.begin() ||
                        /*page_before == page_cache.begin()*/ page_to_data[*page_before].history[2 - 1] >
                                                              histories[2 - 1]) {
                        page_before = std::next(page_before);
                    } else {//: we're at begin AND begin is smaller than us --> we must insert at the beginning, aka before begin
                        page_to_data[*page_before].index++;
                        page_data.index--;
                    }
                    page_data.at_iterator = page_cache.insert(page_before, page_start);
                } else {
                    changed = false;
                }
            }
            return changed;
        };

        temp_t get_temperature(page_t page, std::optional<std::shared_ptr<BOOST_TEST_LRU_nd>> necessary_data) const {
            if (necessary_data == std::nullopt) return page_cache.size() - 1 - page_to_data.at(page).index;
            else {
                auto &nd = *necessary_data.value();
                return nd.prev_page_to_data.size() - 1 - nd.prev_page_to_data.at(page).index;
            }
        }; // 0 = cold
        std::unique_ptr<BOOST_TEST_LRU_nd> get_necessary_data() {
            BOOST_TEST_LRU_nd lrutnd{page_to_data};
            return std::make_unique<BOOST_TEST_LRU_nd>(std::move(lrutnd));
        }

        inline bool is_page_fault(page_t page) const { return !page_to_data.contains(page); };

        temp_t compare_to_previous(std::shared_ptr<BOOST_TEST_LRU_nd> prev_nd) {
            temp_t sum = 0;
            auto &prevptd = prev_nd->prev_page_to_data;
            for (const auto &page: page_cache) {
                if (prevptd.contains(page)) {
                    sum += std::abs(static_cast<long long>(get_temperature(page, std::nullopt)) -
                                    static_cast<long long>(get_temperature(page, prev_nd)));
                }
            }
            return sum;
        };

        const lru_cache_t *get_cache_iterable() const { return &page_cache; }

        virtual std::string toString() { return name() + " : cache = " + cache_to_string(10); };
    private:
        std::string name() { return "BOOST_TEST_LRU"; }

        lru_cache_t page_cache{}; // idx 0 = MRU; idx size-1 = LRU, sorted by second history
        boost::unordered_flat_map<page_t, LRU_page_data> page_to_data;
        uint64_t count_stamp = 0;
        const size_t page_cache_size;

        std::string cache_to_string(size_t num_elements) {
            if (num_elements > page_cache.size()) num_elements = page_cache.size();
            std::string ret("[");
            ret += page_iterable_to_str(page_cache.begin(), num_elements, page_cache.end());
            ret += ']';
            return ret;
        };

        template<typename Iterator>
        std::string page_iterable_to_str(Iterator start, size_t num_elements, Iterator max) {
            std::ostringstream oss;
            if (start != max) { //== non-empty
                while (num_elements-- && start != max) {
                    oss << std::hex << *start << ',';
                    start = std::next(start);
                }
            }
            return oss.str();
        };

    };
}
static void test_realistic(const std::string& path_to_mem_trace){
    const size_t page_cache_size = 16*1024;

    std::ifstream f(path_to_mem_trace);
    if (f.is_open()) {
        std::string line;
        {
            LRU_K normal_lru(page_cache_size,2);
            test::BOOST_TEST_LRU test_lru(page_cache_size);
            auto start = std::chrono::system_clock::now();
            for (unsigned long long i = 0; i < 20 * FUZZ_SIZE; i++) {
                std::getline(f, line);
                const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
                const auto page_start = page_start_from_mem_address(mem_address);
                normal_lru.consume(page_start);
            }
            auto end = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsed_seconds = end - start;
            

            std::cout << "STD finished computation" << " "
                      << "elapsed time: " << elapsed_seconds.count() << "s"
                      << std::endl;
            f.seekg(0, std::ios_base::beg);
            start = std::chrono::system_clock::now();
            for (unsigned long long i = 0; i < 20 * FUZZ_SIZE; i++) {
                std::getline(f, line);
                const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
                const auto page_start = page_start_from_mem_address(mem_address);
                test_lru.consume(page_start);
            }
            end = std::chrono::system_clock::now();
            elapsed_seconds = end - start;
            std::cout << "BOOST finished computation" << " "
                      << "elapsed time: " << elapsed_seconds.count() << "s"
                      << std::endl;
            std::cout << normal_lru.toString() << std::endl;
            std::cout << test_lru.toString() << std::endl;
        }
        f.seekg(0, std::ios_base::beg);
        {
            LRU_K normal_lru(page_cache_size,2);
            test::BOOST_TEST_LRU test_lru(page_cache_size);
            auto start = std::chrono::system_clock::now();
            for (unsigned long long i = 0; i < 20 * FUZZ_SIZE; i++) {
                std::getline(f, line);
                const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
                const auto page_start = page_start_from_mem_address(mem_address);
                test_lru.consume(page_start);
            }
            auto end = std::chrono::system_clock::now();
            std::chrono::duration<double> elapsed_seconds = end - start;
            

            std::cout << "BOOST finished computation" << " "
                      << "elapsed time: " << elapsed_seconds.count() << "s"
                      << std::endl;
            f.seekg(0, std::ios_base::beg);
            start = std::chrono::system_clock::now();

            for (unsigned long long i = 0; i < 20 * FUZZ_SIZE; i++) {
                std::getline(f, line);
                const uint64_t mem_address = std::stoull(line.substr(1), nullptr, 16);
                const auto page_start = page_start_from_mem_address(mem_address);
                normal_lru.consume(page_start);
            }
            end = std::chrono::system_clock::now();
            elapsed_seconds = end - start;
            std::cout << "STD finished computation" << " "
                      << "elapsed time: " << elapsed_seconds.count() << "s"
                      << std::endl;
            std::cout << normal_lru.toString() << std::endl;
            std::cout << test_lru.toString() << std::endl;
        }
    }
    f.close();
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

#define LINE_SIZE 15 //W0x7fffffffd988

static void test_file_read_speed(const std::string &path_to_mem_trace) {

    const size_t page_cache_size = 16*1024;

    LRU_K c1(page_cache_size,2);
//
    bool is_load;
    uint64_t address;
    auto end = std::chrono::system_clock::now();
    auto start = std::chrono::system_clock::now();

    start = std::chrono::system_clock::now();

    int fd = open(path_to_mem_trace.c_str(), O_RDONLY);
    if (fd == -1)
        std::cout<<"Couldn't syscall open the file"<<std::endl;

    // obtain file size
    struct stat sb;
    if (fstat(fd, &sb) == -1)
        std::cout<<"Couldn't fstat the file"<<std::endl;

    auto length = sb.st_size;

    const char* addr = static_cast<const char*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0u));
    if (addr == MAP_FAILED)
        std::cout<<"Couldn't mmap the file"<<std::endl;

    end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "mmap finished open " << "elapsed time: " << elapsed_seconds.count() << "s" << std::endl;

    uint64_t at = 0;
    start = std::chrono::system_clock::now();
    while(at<length){
        is_load = addr[at++] == 'R';
        char* str_end = nullptr;
        address = std::strtoull(addr+at,&str_end,16);
        at+= (str_end+1-(addr+at));
    }
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;

    std::cout << "MMAP setup finished computation" << " "
              << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:" << is_load << address
              << std::endl;

    at = 0;
    start = std::chrono::system_clock::now();
    while(at<length){
        is_load = addr[at++] == 'R';
        char* str_end = nullptr;
        address = std::strtoull(addr+at,&str_end,16);
        at+= (str_end+1-(addr+at));
        c1.consume(page_start_from_mem_address(address));
    }
    end = std::chrono::system_clock::now();
    elapsed_seconds = end - start;

    std::cout << "MMAP read finished computation" << " "
              << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:" << is_load << address
              << std::endl;

    auto ret = munmap((void *) addr, length);
    if(ret)
        std::cout<<"Couldn't munmap the file"<<std::endl;
    close(fd);

    //////////////////////

    LRU_K c2(page_cache_size,2);

    start = std::chrono::system_clock::now();

    std::ifstream f(path_to_mem_trace);
    if (f.is_open()) {
        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "C++ getline open finished" << " "
                  << "elapsed time: " << elapsed_seconds.count() << std::endl;

        std::string line;
        //Setup first ; put some data in cache
        start = std::chrono::system_clock::now();
        while(std::getline(f,line)){
            is_load = line[0]=='R';
            address = std::stoull(line.substr(1), nullptr, 16);
        }
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;

        std::cout << "C++ getline setup finished computation" << " "
                  << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:" << is_load << address
                  << std::endl;

        f.clear();
        f.seekg(0,std::ios_base::beg);

        start = std::chrono::system_clock::now();
        while(std::getline(f,line)){
            is_load = line[0]=='R';
            address = std::stoull(line.substr(1), nullptr, 16);
            c2.consume(page_start_from_mem_address(address));
        }
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;

        std::cout << "C++ getline read finished computation" << " "
                  << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:" << is_load << address
                  << std::endl;
    }
    f.close();

    //////////////////////


  /*  start = std::chrono::system_clock::now();
    std::FILE* fpointer = std::fopen(path_to_mem_trace.c_str(), "rb");
    if(fpointer != nullptr){

        end = std::chrono::system_clock::now();
        std::chrono::duration<double> elapsed_seconds = end - start;
        std::cout << "C-style open finished" << " "
                  << "elapsed time: " << elapsed_seconds.count() << std::endl;

        char buf[LINE_SIZE] = {0};
        start = std::chrono::system_clock::now();
        while(!std::feof(fpointer)){
            (void)std::fread(buf, sizeof(char), LINE_SIZE, fpointer);
            is_load = buf[0]=='R';
            address = std::strtoull(buf+1, nullptr,16);
            std::fseek(fpointer, 1, SEEK_CUR);
        }
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;

        std::cout << "C-style setup finished computation" << " "
                  << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:" << is_load << address
                  << std::endl;

        fseek(fpointer, 0, SEEK_SET);
        start = std::chrono::system_clock::now();
        while(!std::feof(fpointer)){
            (void)std::fread(buf, sizeof(char), LINE_SIZE, fpointer);
            is_load = buf[0]=='R';
            address = std::strtoull(buf+1, nullptr,16);
            std::fseek(fpointer, 1, SEEK_CUR);
        }
        end = std::chrono::system_clock::now();
        elapsed_seconds = end - start;

        std::cout << "C-style read finished computation" << " "
                  << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:" << is_load << address
                  << std::endl;
        std::fclose(fpointer);
    }
    else{
        std::cout<<"Couldn't open the file"<<std::endl;
    }
*/

}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void arc_t(__off_t length, const char *addr) {
    const size_t page_cache_size = 16 * 1024;
    ARC a1(page_cache_size);
    uint64_t address;
    auto end = std::chrono::system_clock::now();
    auto start = std::chrono::system_clock::now();
    uint64_t at = 7'000'000;
    start = std::chrono::system_clock::now();
    while(at<length){
        char* str_end = nullptr;
        address = strtoull(addr+at,&str_end,16);
        at+= (str_end+1-(addr+at));
        a1.consume(page_start_from_mem_address(address));
    }
    end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << "ARC finished" << " "
              << "elapsed time: " << elapsed_seconds.count() << "s"
              << std::endl;
}
void car_t(__off_t length, const char *addr) {
    const size_t page_cache_size = 16 * 1024;
    CAR a1(page_cache_size);
    uint64_t address;
    auto end = std::chrono::system_clock::now();
    auto start = std::chrono::system_clock::now();
    uint64_t at = 7'000'000;
    start = std::chrono::system_clock::now();
    while(at<length){
        char* str_end = nullptr;
        address = strtoull(addr+at,&str_end,16);
        at+= (str_end+1-(addr+at));
        a1.consume(page_start_from_mem_address(address));
    }
    end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << "CAR finished" << " "
              << "elapsed time: " << elapsed_seconds.count() << "s"
              << std::endl;
}
void lru_t(__off_t length, const char *addr) {
    const size_t page_cache_size = 16 * 1024;
    LRU_K a1(page_cache_size,2);
    uint64_t address;
    auto end = std::chrono::system_clock::now();
    auto start = std::chrono::system_clock::now();
    uint64_t at = 7'000'000;
    start = std::chrono::system_clock::now();
    while(at<length){
        char* str_end = nullptr;
        address = strtoull(addr+at,&str_end,16);
        at+= (str_end+1-(addr+at));
        a1.consume(page_start_from_mem_address(address));
    }
    end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << "LRU finished" << " "
              << "elapsed time: " << elapsed_seconds.count() << "s"
              << std::endl;
}
void c_t(__off_t length, const char *addr) {
    const size_t page_cache_size = 16 * 1024;
    CLOCK a1(page_cache_size,2);
    uint64_t address;
    auto end = std::chrono::system_clock::now();
    auto start = std::chrono::system_clock::now();
    uint64_t at = 7'000'000;
    start = std::chrono::system_clock::now();
    while(at<length){
        char* str_end = nullptr;
        address = strtoull(addr+at,&str_end,16);
        at+= (str_end+1-(addr+at));
        a1.consume(page_start_from_mem_address(address));
    }
    end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;

    std::cout << "CLOCK finished" << " "
              << "elapsed time: " << elapsed_seconds.count() << "s" << "; data:"
              << std::endl;
}

void test_full_all_mt(const std::string &path_to_mem_trace) {

    auto end = std::chrono::system_clock::now();
    auto start = std::chrono::system_clock::now();

    int fd = open(path_to_mem_trace.c_str(), O_RDONLY);
    if (fd == -1)
        std::cout<<"Couldn't syscall open the file"<<std::endl;

    // obtain file size
    struct stat sb;
    if (fstat(fd, &sb) == -1)
        std::cout<<"Couldn't fstat the file"<<std::endl;

    size_t length = 9'000'000 * (LINE_SIZE+1);

    const char* addr = static_cast<const char*>(mmap(nullptr, length, PROT_READ, MAP_PRIVATE, fd, 0u));
    if (addr == MAP_FAILED)
        std::cout<<"Couldn't mmap the file"<<std::endl;

    end = std::chrono::system_clock::now();
    std::chrono::duration<double> elapsed_seconds = end - start;
    std::cout << "mmap finished open " << "elapsed time: " << elapsed_seconds.count() << "s" << std::endl;


    std::array a = {std::jthread(arc_t,length,addr),std::jthread(car_t,length,addr),std::jthread(c_t,length,addr),std::jthread(lru_t,length,addr)};

    for(auto& t : a) t.join();

    auto ret = munmap((void *) addr, length);
    if(ret)
        std::cout<<"Couldn't munmap the file"<<std::endl;
    close(fd);
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

void test_all(const std::string& path_to_mem_trace){

    test_ma_all();
    test_file_maps(path_to_mem_trace);
    test_fuzz();
}


void test_latest(const std::string& path_to_mem_trace){
    test_full_all_mt(path_to_mem_trace);
}