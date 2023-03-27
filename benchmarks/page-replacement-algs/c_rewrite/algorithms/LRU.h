#ifndef C_REWRITE_LRU_H
#define C_REWRITE_LRU_H

#include "GenericAlgorithm.h"
#include <list>


typedef std::list<page_t> lru_cache_t;

struct LRU_page_data{
    size_t index=0;
    std::deque<uint64_t> history;
    lru_cache_t::iterator at_iterator;
    LRU_page_data(): LRU_page_data(2) {}
    explicit LRU_page_data(uint8_t K) : history(K){}
};

class LRU_K : public GenericAlgorithm{
public:
    LRU_K(uint16_t page_cache_size,uint8_t K) : GenericAlgorithm(page_cache_size),K(K){};
    void consume(page_t page_start) override;
    temp_function get_temparature_function() override { return [this](page_t page_base){return page_to_data[page_base].index;}; };
    inline bool is_page_fault(page_t page) override {return page_to_data.contains(page);};
    std::string name() override {return "LRU_"+std::to_string(K);};
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),page_cache.begin()+static_cast<long>(num_elements));
        ret += ']';
        return ret;
    };
    lru_cache_t page_cache{}; // idx 0 = MRU; idx size-1 = LRU
    const uint8_t K;
    std::unordered_map<page_t,LRU_page_data> page_to_data;
    uint64_t count_stamp = 0;
    lru_cache_t::iterator find_victim();
};


#endif //C_REWRITE_LRU_H
