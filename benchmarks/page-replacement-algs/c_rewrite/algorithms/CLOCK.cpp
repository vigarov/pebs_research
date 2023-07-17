//
// Created by vigarov on 26/03/23.
//

#include "CLOCK.h"

evict_return_t CLOCK::consume_tracked(page_t page_start){
    auto page_fault = is_tracked_page_fault(page_start);
    auto& page_data_internal = page_to_data_internal[page_start];

    evict_return_t ret;
    if(page_fault){
        if(page_cache_full() && U->size()==0){
            find_victim();
            auto head_page = *head;
            ret = head_page;
            page_to_data_internal.erase(head_page);
            *head = page_start;
            advance_head();
        }
        else{
            if(page_cache_full()) ret = evict(); //Guaranteed to evict from U
            auto it = page_cache.insert(page_cache.end(),page_start);
            if(page_cache.size() == 1) head = it;
        }
    }
    page_data_internal.counter = i;
    return ret;
}

void CLOCK::find_victim() {
    while(auto& counter = page_to_data_internal[*head].counter){
        counter--;
        advance_head();
    }
}

std::unique_ptr<page_cache_copy_t> CLOCK::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.end(), page_cache.begin(), page_cache.end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}

evict_return_t CLOCK::evict_from_tracked() {
    return evict_by_removing_from_list();
}

evict_return_t CLOCK::evict_by_removing_from_list(){
    find_victim();
    auto head_page = *head;
    page_to_data_internal.erase(head_page);
    advance_head();
    page_cache.erase(std::prev(head == page_cache.begin()? page_cache.end() : head));
    return head_page;
}
