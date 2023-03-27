#ifndef C_REWRITE_CLOCK_H
#define C_REWRITE_CLOCK_H

#include <numeric>
#include "GenericAlgorithm.h"


typedef std::deque<page_t> gclock_cache_t;

struct CLOCK_page_data{
    size_t index=0;
    uint8_t counter = 0;
};

class CLOCK : public GenericAlgorithm{
public:
    CLOCK(uint16_t page_cache_size,uint8_t i) : GenericAlgorithm(page_cache_size),i(i),num_count_i(i,0){};
    void consume(page_t page_start) override;
    temp_function get_temparature_function() override { return [this](page_t page_base){
        auto& page_data = page_to_data[page_base];
        auto relative_idx = (head+page_data.index)%page_cache.size();
        auto delta = std::accumulate(num_count_i.begin(),num_count_i.begin()+page_data.counter,static_cast<size_t>(0)); // == (page_data.counter ? num_count_i[page_data.counter]: 0);
        return relative_idx + delta;}; };
    inline bool is_page_fault(page_t page) override {return page_to_data.contains(page);};
    std::string name() override {return "GLOCK";};
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),page_cache.begin()+static_cast<long>(num_elements));
        ret += ']';
        return ret;
    };
    gclock_cache_t page_cache{}; // idx 0 = should-be LRU; idx size-1 = should-be MRU
    std::unordered_map<page_t,CLOCK_page_data> page_to_data;
    void find_victim();
    size_t head = 0;
    std::vector<size_t> num_count_i;
};


#endif //C_REWRITE_CLOCK_H
