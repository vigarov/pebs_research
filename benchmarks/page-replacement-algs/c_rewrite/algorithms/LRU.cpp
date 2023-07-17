//
// Created by vigarov on 19/06/23.
//

#include "LRU.h"

evict_return_t LRU::consume_tracked(page_t page_start){
    auto page_fault = is_tracked_page_fault(page_start);
    auto& page_data_internal = page_to_data_internal[page_start];

    evict_return_t ret;
    if(page_fault) {
        if (page_cache_full()) { //Full, must replace
            ret = evict();
        }

        page_data_internal.at_iterator = page_cache.insert(page_cache.begin(),page_start);
    }
    else { //page in cache
        auto& it = page_data_internal.at_iterator;
        if(it != page_cache.begin()){
            //Put at front; accessed page becomes MRU
            auto page_corresponding = *it;
            page_cache.erase(it);
            page_data_internal.at_iterator = page_cache.insert(page_cache.begin(),page_corresponding);
        }
        ret = std::nullopt;
    }
    return ret;
}

std::unique_ptr<page_cache_copy_t> LRU::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.begin(),page_cache.begin(),page_cache.end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}

evict_return_t LRU::evict_from_tracked(){
    if(tracked_size()==0) return std::nullopt;
    auto victim_page_it = std::prev(page_cache.end()); //LRU
    auto ret = *victim_page_it;
    page_to_data_internal.erase(*victim_page_it);
    page_cache.erase(victim_page_it);
    return ret;
}
