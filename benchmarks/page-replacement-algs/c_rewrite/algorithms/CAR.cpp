//
// Created by vigarov on 31/03/23.
//

#include "CAR.h"

evict_return_t CAR::consume_tracked(page_t page_start) {
    auto& page_data_internal = page_to_data_internal[page_start];
    auto in_cache = (page_data_internal.in_list == T1 || page_data_internal.in_list == T2);

    evict_return_t ret;
    if (in_cache){
        if(!page_data_internal.referenced) {
            page_data_internal.referenced = true;
        }
        ret = std::nullopt;
    }
    else{
        if(tracked_size() == max_page_cache_size){
            ret = replace();
            if(page_data_internal.in_list != B1 && page_data_internal.in_list != B2 ){
                if (caches[T1].size() + caches[B1].size() == max_page_cache_size) {
                    // Discard LRU in B1
                    page_to_data_internal.erase(caches[B1].front());
                    caches[B1].pop_front();
                }
                else if (caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size() == 2 * max_page_cache_size) {
                    // Discard LRU in B2
                    page_to_data_internal.erase(caches[B2].front());
                    caches[B2].pop_front();
                }
            }
        }
        else if(page_cache_full()){
            ret = U->evict();
        }


        if(page_data_internal.in_list != B1 && page_data_internal.in_list != B2){
            //History Miss
            page_data_internal.at_iterator = caches[T1].insert(caches[T1].end(),page_start);
            page_data_internal.in_list = T1;
        }
        else{
            //History Hit
            if(page_data_internal.in_list == B1){
                p = std::min(p + std::max(1., static_cast<double>(caches[B2].size()) /  static_cast<double>(caches[B1].size())), static_cast<double>(max_page_cache_size));
            }
            else{
                p = std::max(p - std::max(1., static_cast<double>(caches[B1].size()) / static_cast<double>(caches[B2].size()) ), 0.);
            }
            caches.at(page_data_internal.in_list).erase(page_data_internal.at_iterator);
            page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(),page_start);
            page_data_internal.in_list = T2;
        }
    }
    return ret;
}

page_t CAR::replace() {
    bool found = false;
    page_t ret;
    while(!found){
        if(caches[T1].size() >= static_cast<size_t>(std::max(1., p))){
            // T1 is oversized
            auto& t1_head_data = page_to_data_internal[caches[T1].front()];
            auto& t1_head_data_internal = page_to_data_internal[caches[T1].front()];
            auto t1_head = *t1_head_data_internal.at_iterator;
            if(!t1_head_data.referenced){
                t1_head_data_internal.at_iterator = caches[B1].insert(caches[B1].end(),t1_head);
                t1_head_data.in_list = B1;
                num_unreferenced[T1]--;
                found = true;
            }else{
                t1_head_data.referenced = false;
                t1_head_data_internal.at_iterator = caches[T2].insert(caches[T2].end(),t1_head);
                t1_head_data.in_list = T2;
            }
            ret = caches[T1].front();
            caches[T1].pop_front();
        }else{
            // T2 is oversized
            auto& t2_head_data = page_to_data_internal[caches[T2].front()];
            auto& t2_head_data_internal = page_to_data_internal[caches[T2].front()];
            auto t2_head = *t2_head_data_internal.at_iterator;
            if(!t2_head_data.referenced){
                t2_head_data_internal.at_iterator = caches[B2].insert(caches[B2].end(),t2_head);
                t2_head_data.in_list = B2;
                num_unreferenced[T2]--;
                found = true;
            }else{
                t2_head_data.referenced = false;
                t2_head_data_internal.at_iterator = caches[T2].insert(caches[T2].end(),t2_head);
                t2_head_data.in_list = T2;
            }
            ret = caches[T2].front();
            caches[T2].pop_front();
        }
    }
    return ret;
}

std::unique_ptr<page_cache_copy_t> CAR::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;

    concatenated_list.insert(concatenated_list.end(), caches[T1].begin(), caches[T1].end());
    concatenated_list.insert(concatenated_list.end(), caches[T2].begin(), caches[T2].end());

    return std::make_unique<page_cache_copy_t>(concatenated_list);
}


evict_return_t CAR::evict_from_tracked() {
    if(tracked_size()==0)return std::nullopt;
    auto ret = replace();
    if (caches[T1].size() + caches[B1].size() >= max_page_cache_size) {
        // Discard LRU in B1
        page_to_data_internal.erase(caches[B1].front());
        caches[B1].pop_front();
    }
    else if (caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size() >= 2 * max_page_cache_size) {
        // Discard LRU in B2
        page_to_data_internal.erase(caches[B2].front());
        caches[B2].pop_front();
    }
    return ret;
}