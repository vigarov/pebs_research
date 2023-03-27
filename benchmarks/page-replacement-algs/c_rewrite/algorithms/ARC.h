#ifndef C_REWRITE_ARC_H
#define C_REWRITE_ARC_H


#include "GenericAlgorithm.h"

struct ARC_page_data{
    size_t index=0;
};

class ARC : public GenericAlgorithm{
public:
    ARC(uint16_t page_cache_size) : GenericAlgorithm(page_cache_size){};
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
    cache_t page_cache; // idx 0 = LRU; idx size-1 = MRU
    std::unordered_map<page_t,ARC_page_data> page_to_data;
    uint64_t count_stamp = 0;
    page_t find_victim_and_erase();

};


#endif //C_REWRITE_ARC_H
