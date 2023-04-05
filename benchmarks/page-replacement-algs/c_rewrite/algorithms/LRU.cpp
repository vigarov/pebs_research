#include "LRU.h"
#include <cstdlib>

bool LRU_K::consume(page_t page_start){
    bool changed = true;
    count_stamp+=1;
    auto page_fault = is_page_fault(page_start);
    auto& page_data = page_to_data[page_start];
    auto& histories = page_data.history;
    //For K!=2, if(histories.size() != K) {/*set K empty elements*/}
    histories.pop_back();
    histories.push_front(count_stamp);
    if(page_fault){
        if(page_cache.size()==page_cache_size){ //Full, must replace
            auto victim_page_it = std::prev(page_cache.end());
            page_to_data.erase(*victim_page_it);
            page_cache.erase(victim_page_it);
        }
        page_data.index = page_cache.size();
        page_data.at_iterator=page_cache.insert(page_cache.end(),page_start);
        //Keeps sort since page_fault --> not in history --> K-1 = 0 == min
    }
    else{ //page in cache
        //Keep the page cache sorted; page_data[K-1] must be compared to all subsequent page_data[K-1]
        auto it = page_data.at_iterator;
        if(it != page_cache.begin()) {
            auto page_before = std::prev(page_cache.erase(it));
            while (page_before != page_cache.begin() && page_to_data[*page_before].history[K - 1] < histories[K - 1]) {
                page_to_data[*page_before].index++;
                page_before = std::prev(page_before);
                page_data.index--;
            }
            if(page_before != page_cache.begin() || /*page_before == page_cache.begin()*/ page_to_data[*page_before].history[K - 1] > histories[K - 1]){
                page_before = std::next(page_before);
            }
            else {//: we're at begin AND begin is smaller than us --> we must insert at the beginning, aka before begin
                page_to_data[*page_before].index++;
                page_data.index--;
            }
            page_data.at_iterator = page_cache.insert(page_before, page_start);
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

temp_t LRU_K::compare_to_previous(std::shared_ptr<nd_t> prev_nd){
    temp_t sum = 0;
    auto& prevptd = std::get<LRU_temp_necessary_data>(*prev_nd).prev_page_to_data;
    for(const auto& page: page_cache){
        if(prevptd.contains(page)){
            sum+=std::abs(static_cast<long long>(get_temperature(page,std::nullopt)) - static_cast<long long>(get_temperature(page,prev_nd)));
        }
    }
    return sum;
}