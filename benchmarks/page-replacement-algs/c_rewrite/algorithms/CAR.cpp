//
// Created by vigarov on 31/03/23.
//

#include "CAR.h"

bool CAR::consume(page_t page_start) {
    bool changed = true;
    auto& page_data = page_to_data[page_start];
    auto in_cache = (page_data.in_list == T1 || page_data.in_list == T2);
    if (in_cache){
        if(!page_data.referenced) {
            update_relative_indices(page_data);
            page_data.relative_index = caches[page_data.in_list].size()-(num_unreferenced[page_data.in_list]--);
            page_data.referenced = true;
        }
        //else we already have page_data.referenced == 1
        else{
            changed=false;
        }
    }
    else{
        if(caches[T1].size() + caches[T2].size() == page_cache_size){
            replace();
            if(page_data.in_list != B1 && page_data.in_list != B2 ){
                if (caches[T1].size() + caches[B1].size() == page_cache_size) {
                    // Discard LRU in B1
                    page_to_data.erase(caches[B1].front());
                    caches[B1].pop_front();
                }
                else if (caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size() == 2 * page_cache_size) {
                    // Discard LRU in B2
                    page_to_data.erase(caches[B2].front());
                    caches[B2].pop_front();
                }
            }
        }
        if(page_data.in_list != B1 && page_data.in_list != B2){
            //History Miss
            page_data.at_iterator = caches[T1].insert(caches[T1].end(),page_start);
            page_data.in_list = T1;
            page_data.relative_index = num_unreferenced[T1]++;
        }
        else{
            //History Hit
            if(page_data.in_list == B1){
                p = std::min(p + std::max(1., static_cast<double>(caches[B2].size()) /  static_cast<double>(caches[B1].size())), static_cast<double>(page_cache_size));
            }
            else{
                p = std::max(p - std::max(1., static_cast<double>(caches[B1].size()) / static_cast<double>(caches[B2].size()) ), 0.);
            }
            caches.at(page_data.in_list).erase(page_data.at_iterator);
            page_data.at_iterator = caches[T2].insert(caches[T2].end(),page_start);
            page_data.in_list = T2;
            page_data.relative_index = num_unreferenced[T2]++;
        }
    }
    return changed;
}

void CAR::update_relative_indices(const CAR_page_data& data) {
    auto start = std::next(data.at_iterator);
    std::for_each(start, caches[data.in_list].end(), [&](const auto &page) {
        auto &maybe_modify_data = page_to_data[page];
        if (maybe_modify_data.referenced == data.referenced) maybe_modify_data.relative_index--;
    });
}

void CAR::replace() {
    bool found = false;
    while(!found){
        if(caches[T1].size() >= static_cast<size_t>(std::max(1., p))){
            // T1 is oversized
            auto& t1_head_data = page_to_data[caches[T1].front()];
            auto t1_head = *t1_head_data.at_iterator;
            update_relative_indices(t1_head_data);
            if(!t1_head_data.referenced){
                t1_head_data.at_iterator = caches[B1].insert(caches[B1].end(),t1_head);
                t1_head_data.in_list = B1;
                num_unreferenced[T1]--;
                found = true;
            }else{
                t1_head_data.referenced = false;
                t1_head_data.at_iterator = caches[T2].insert(caches[T2].end(),t1_head);
                t1_head_data.in_list = T2;
                t1_head_data.relative_index = num_unreferenced[T2]++;
            }
            caches[T1].pop_front();
        }else{
            // T2 is oversized
            auto& t2_head_data = page_to_data[caches[T2].front()];
            auto t2_head = *t2_head_data.at_iterator;
            update_relative_indices(t2_head_data);
            if(!t2_head_data.referenced){
                t2_head_data.at_iterator = caches[B2].insert(caches[B2].end(),t2_head);
                t2_head_data.in_list = B2;
                num_unreferenced[T2]--;
                found = true;
            }else{
                t2_head_data.referenced = false;
                t2_head_data.at_iterator = caches[T2].insert(caches[T2].end(),t2_head);
                t2_head_data.in_list = T2;
                t2_head_data.relative_index = num_unreferenced[T2]++;
            }
            caches[T2].pop_front();
        }
    }
}

std::unique_ptr<page_cache_copy_t> CAR::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;

    concatenated_list.insert(concatenated_list.end(), caches[T1].begin(), caches[T1].end());
    concatenated_list.insert(concatenated_list.end(), caches[T2].begin(), caches[T2].end());

    return std::make_unique<page_cache_copy_t>(concatenated_list);
}

temp_t CAR::compare_to_previous(std::shared_ptr<nd_t> prev_nd) {
    temp_t sum = 0;
    auto& prevptd = std::get<CAR_temp_necessary_data>(*prev_nd).prev_page_to_data;
    for(const auto& page: caches[T1]){
        if(prevptd.contains(page)){
            sum+=std::abs(static_cast<long long>(get_temperature(page,std::nullopt)) - static_cast<long long>(get_temperature(page,prev_nd)));
        }
    }
    for(const auto& page: caches[T2]){
        if(prevptd.contains(page)){
            sum+=std::abs(static_cast<long long>(get_temperature(page,std::nullopt)) - static_cast<long long>(get_temperature(page,prev_nd)));
        }
    }
    return sum;
}
