#ifndef C_REWRITE_LRU_K_H
#define C_REWRITE_LRU_K_H

#include "GenericAlgorithm.h"
#include <list>
#include <optional>

class LRU_K : public GenericAlgorithm{
public:
    LRU_K(size_t page_cache_size,uint8_t K,untracked_eviction::type evictionType) : GenericAlgorithm(page_cache_size,evictionType),K(K){};
    bool consume_tracked(page_t page_start) override;
    inline bool is_tracked_page_fault(page_t page) const override {return !page_to_data_internal.contains(page);};
    std::string name() override {return "LRU_"+std::to_string(K);};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    const lru_k_cache_t * get_cache_iterable() const {return &page_cache;}
    void evict_from_tracked() override {//TODO
         }
    size_t tracked_size() override{return page_cache.size();};
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),num_elements,page_cache.end());
        ret += ']';
        return ret;
    };
    lru_k_cache_t page_cache{}; // idx 0 = MRU; idx size-1 = LRU, sorted by second history
    const uint8_t K;
    std::unordered_map<page_t,LRU_K_page_data_internal> page_to_data_internal;
    uint64_t count_stamp = 0;
    lru_k_cache_t::iterator latest_first_access_page = page_cache.end();

};


#endif //C_REWRITE_LRU_K_H
