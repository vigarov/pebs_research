#ifndef C_REWRITE_ARC_H
#define C_REWRITE_ARC_H


#include "GenericAlgorithm.h"
#include <list>
#include <iostream>


class ARC : public GenericAlgorithm{
public:
    explicit ARC(size_t page_cache_size,untracked_eviction::type evictionType) : GenericAlgorithm(page_cache_size,evictionType){};
    evict_return_t consume_tracked(page_t page_start) override;
    inline bool is_tracked_page_fault(page_t page) const  override {return !page_to_data_internal.contains(page) || page_to_data_internal.at(page).in_list == B1 || page_to_data_internal.at(page).in_list == B2;};
    evict_return_t evict_from_tracked() override;
    size_t tracked_size() override{return caches[T1].size()+caches[T2].size();};
    std::string name() override {return "ARC";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    const auto* get_cache_iterable() const {return &dcr;}
private:
    std::string cache_to_string(size_t num_elements) override{
        std::string ret;
        long long num_left_elements = static_cast<long>(num_elements);
        for (int i = T1; i <= T2 && num_left_elements>0; ++i) {
            auto& relevant_cache = caches.at(i);
            if(i == T2 && num_left_elements>static_cast<long long>(relevant_cache.size())) num_left_elements = static_cast<long>(relevant_cache.size());
            ret += std::to_string(i)+": ";
            std::string cache_answ("[");
            cache_answ += page_iterable_to_str(relevant_cache.begin(),num_left_elements,relevant_cache.end());
            cache_answ += ']';
            ret += cache_answ;
            num_left_elements -= static_cast<long>(relevant_cache.size());
        }
        return ret;
    };
    std::array<arc_cache_t,NUM_CACHES> caches{}; // idx 0 = LRU; idx size-1 = MRU
    std::unordered_map<page_t,ARC_page_data_internal> page_to_data_internal;
    double p = 0.;
    page_t replace(bool inB2);

    dual_container_range<arc_cache_t,arc_cache_t> dcr{caches[T1],caches[T2]};
    page_t lru_to_mru(cache_list_idx from, cache_list_idx to);
};



#endif //C_REWRITE_ARC_H
