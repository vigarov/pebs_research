//
// Created by vigarov on 26/03/23.
//

#include "CLOCK.h"

void CLOCK::consume(page_t page_start){
    auto page_fault = is_page_fault(page_start);
    auto& page_data = page_to_data[page_start];
    if(page_fault){
        if(page_cache.size() == page_cache_size){
            find_victim();
            auto page_to_erase = page_cache[head];
            page_to_data.erase(page_to_erase);
            num_count_i[page_to_data[page_to_erase].counter]--;
            page_cache[head] = page_start;
            page_data.index = head;
        }
        else{
            page_data.index = page_cache.size();
            page_cache.push_back(page_start);
        }
    }
    num_count_i[page_data.counter = i]++;
    page_to_data[page_start] = page_data;
}

void CLOCK::find_victim() {
    while(auto& counter = page_to_data[page_cache[head]].counter){
        num_count_i[counter--]--;
        num_count_i[counter]++;
        head = (head+1)%page_cache.size();
    }
}
