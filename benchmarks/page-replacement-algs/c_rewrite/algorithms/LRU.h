//
// Created by vigarov on 19/06/23.
//

#ifndef C_REWRITE_LRU_H
#define C_REWRITE_LRU_H

#include "GenericAlgorithm.h"

class LRU : public GenericAlgorithm {
public:
    LRU(size_t page_cache_size) : GenericAlgorithm(page_cache_size){};
    bool consume(page_t page_start) override;
    inline bool is_page_fault(page_t page) const override {return !page_to_data_internal.contains(page);};
    std::string name() override {return "LRU";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    const lru_cache_t * get_cache_iterable() const {return &page_cache;}
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),num_elements,page_cache.end());
        ret += ']';
        return ret;
    };
    lru_cache_t page_cache{}; // idx 0 = MRU; idx size-1 = LRU
    std::unordered_map<page_t,LRU_page_data_internal> page_to_data_internal;
    uint64_t count_stamp = 0;

    std::list<lru_cache_t::const_iterator> iterators = {page_cache.end()};
};


#endif //C_REWRITE_LRU_H
