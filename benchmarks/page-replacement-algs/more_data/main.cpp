#include <string>
#include <iostream>
#include <fstream>
#include <functional>
#include <unordered_map>
#include <cstring>
#include <sstream>

#define BIN_LINE_SIZE_BYTES 9
#define PAGE_SIZE 4096

typedef uint64_t ptr_t;
typedef ptr_t page_t;

typedef uint64_t timestamp;

inline static page_t page_start_from_mem_address(ptr_t x) {
    return x & ~(PAGE_SIZE - 1);
}

typedef std::vector<timestamp> ts_t;
struct page_data{
     ts_t timestamps;
};

typedef std::pair<page_t,page_data> map_kv_t;

std::string print_vector(const std::vector<map_kv_t>& map_kvs){
    std::stringstream ss;
    for(const auto& item : map_kvs) {
        ss << item.first << ": "<<item.second.timestamps.size();
    }
    return ss.str();
}

int main(int argc, char* argv[]){
    if(argc != 2) return -1;
    auto mem_trace_file = std::string(argv[1]);
    std::ifstream mtf(mem_trace_file,std::ios::in | std::ios::binary);
    if (!mtf.is_open()) {
        std::cerr << "Failed to open memory trace file" << std::endl;
        exit(-1);
    }
    uint64_t lds = 0,strs = 0;
    std::function<bool(std::string&)> read;
    std::function<std::pair<uint8_t,page_t>(std::string&)> parse;
    std::string line;


    read = [&mtf](std::string &line) { return bool(mtf.read(&line[0], BIN_LINE_SIZE_BYTES)); };
    parse = [](std::string &line){
        const uint8_t is_load = line[0]==0;
        const uint64_t mem_address = 0;
        memcpy((void *) &mem_address, &line[1], sizeof(uint64_t));
        return std::pair{is_load,mem_address};
    };

    timestamp at = 0;
    std::unordered_map<page_t,page_data> all_pages;
    while(read(line)){

        auto[is_load,address] = parse(line);
        if (is_load) {
            lds += 1;
        } else {
            strs += 1;
        }
        all_pages[page_start_from_mem_address(address)].timestamps.push_back(at++);
    }
    mtf.close();

#define N 20

    std::vector<map_kv_t> top_n(N);
    std::partial_sort_copy(all_pages.begin(),
                           all_pages.end(),
                           top_n.begin(),
                           top_n.end(),
                           [](map_kv_t const& l,
                              map_kv_t const& r)
                           {
                               return l.second.timestamps.size() > r.second.timestamps.size();
                           });

    std::cout<< "n_unique pages" << all_pages.size() << "\nn_unique addresses" << lds+strs <<"\nTop N pages" << print_vector(top_n) <<std::endl;

    return 0;
}