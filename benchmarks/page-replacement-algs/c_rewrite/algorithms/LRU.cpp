//
// Created by vigarov on 19/06/23.
//

#include "LRU.h"

bool LRU::consume(page_t page_start){
    bool change = true;
    auto page_fault = is_page_fault(page_start);
    auto& page_data_internal = page_to_data_internal[page_start];

    if(page_fault) {
        if (page_cache.size() == page_cache_size) { //Full, must replace
            auto victim_page_it = std::prev(page_cache.end());
            page_to_data_internal.erase(*victim_page_it);
            page_cache.erase(victim_page_it);
        }

        page_data_internal.at_iterator = page_cache.insert(page_cache.begin(),page_start);
    }
    else { //page in cache
        auto& it = page_data_internal.at_iterator;
        change = it != page_cache.begin();
        if(change){
            //Put at front; accessed page becomes MRU
            auto page_corresponding = *it;
            page_cache.erase(it);
            page_data_internal.at_iterator = page_cache.insert(page_cache.begin(),page_corresponding);
        }
    }
    return change;
}

std::unique_ptr<page_cache_copy_t> LRU::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.begin(),page_cache.begin(),page_cache.end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}
