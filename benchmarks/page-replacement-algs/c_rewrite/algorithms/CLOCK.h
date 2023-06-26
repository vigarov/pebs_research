#ifndef C_REWRITE_CLOCK_H
#define C_REWRITE_CLOCK_H

#include <numeric>
#include "GenericAlgorithm.h"




class CLOCK : public GenericAlgorithm{
public:
    CLOCK(size_t page_cache_size,untracked_eviction::type evictionType,uint8_t i) : GenericAlgorithm(page_cache_size,evictionType),head(page_cache.begin()),i(i){};
    bool consume_tracked(page_t page_start) override;
    inline bool is_tracked_page_fault(page_t page) const override {return !page_to_data_internal.contains(page);};
    size_t tracked_size() override{return page_cache.size();};
    void evict_from_tracked() override;
    std::string name() override {return i != 1 ? "GCLOCK" : "CLOCK";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    const gclock_cache_t * get_cache_iterable() const {return &page_cache;}
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),num_elements, page_cache.end());
        ret += ']';
        return ret;
    };
    gclock_cache_t page_cache{}; // idx 0 = should-be LRU; idx size-1 = should-be MRU

    std::unordered_map<page_t,CLOCK_page_data_internal> page_to_data_internal;
    void find_victim();
    gclock_cache_t::iterator head;

    uint8_t i;
    inline void advance_head(){head = std::next(head); if(head == page_cache.end()) head = page_cache.begin();};

    void evict_by_removing_from_list();
};


#endif //C_REWRITE_CLOCK_H
