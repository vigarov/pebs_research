#include "LRU_K.h"

evict_return_t LRU_K::consume_tracked(page_t page_start){
    // TODO: adapt for evict_return_t
    return std::nullopt;
    //END TODO
    bool changed = true;
    count_stamp+=1;
    auto page_fault = is_tracked_page_fault(page_start); //TODO
    auto& page_data_internal = page_to_data_internal[page_start];
    auto& histories = page_data_internal.history;
    //For K!=2, if(histories.size() != K) {/*set K empty elements*/}
    histories.pop_back();
    histories.push_front(count_stamp);
    if(page_fault){
        if(page_cache.size() == max_page_cache_size) { //Full, must replace
            auto victim_page_it = std::prev(page_cache.end());
            auto modify = latest_first_access_page == victim_page_it;
            page_to_data_internal.erase(*victim_page_it);
            page_cache.erase(victim_page_it);
            if (modify) {
                latest_first_access_page = page_cache.end();
            }
        }
        page_data_internal.at_iterator = page_cache.insert(latest_first_access_page,page_start);
        latest_first_access_page = page_data_internal.at_iterator;
    }
    else{ //page in cache
        //Keep the page cache sorted; page_data[K-1] must be compared to all subsequent page_data[K-1]
        auto it = page_data_internal.at_iterator;
        if(it != page_cache.begin()) {
            if(it == latest_first_access_page){
                latest_first_access_page = std::next(it);
            }
            auto page_before = std::prev(page_cache.erase(it));
            //We must now find where to place the already seen page in our list, which is sorted by K-1th history
            while (page_before != page_cache.begin() && page_to_data_internal[*page_before].history[K - 1] < histories[K - 1]) {
                page_before = std::prev(page_before);
            }
            if(page_before != page_cache.begin() || /*page_before == page_cache.begin()*/ page_to_data_internal[*page_before].history[K - 1] > histories[K - 1]){
                page_before = std::next(page_before);
            }
            page_data_internal.at_iterator = page_cache.insert(page_before, page_start);
        }else{
            changed = false;
        }
    }
    return changed;
}

std::unique_ptr<page_cache_copy_t> LRU_K::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.begin(),page_cache.begin(),page_cache.end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}
