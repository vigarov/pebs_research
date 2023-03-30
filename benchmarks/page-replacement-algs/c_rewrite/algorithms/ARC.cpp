#include "ARC.h"

void ARC::consume(page_t page_start) {
    auto& page_data = page_to_data[page_start];
    auto in_cache = (page_data.in_list == T1 || page_data.in_list == T2);
    auto page = *page_data.at_iterator;
    if (in_cache){
        remove_from_cache_and_update_indices(page_data.in_list,page_data.at_iterator);
        page_data.at_iterator = caches[T2].insert(caches[T2].end(),page);
    }
    else if(page_data.in_list == B1){
        const double delta_1 = caches[B1].size() >= caches[B2].size() ? 1. : static_cast<double>(caches[B2].size())  / static_cast<double>(caches[B1].size());
        p = std::min(p+delta_1,static_cast<double>(page_cache_size));
        replace(false);
        //Remove from B1, no need to update indices as B_i is not considered for TL
        caches[B1].erase(page_data.at_iterator);
        //Insert into T2 MRU
        page_data.at_iterator = caches[T2].insert(caches[T2].end(),page);
        page_data.index = caches[T2].size()-1;
        page_data.in_list = T2;
    }
    else if(page_data.in_list == B2){
        const double delta_2 = caches[B2].size() >= caches[B1].size() ? 1. : static_cast<double>(caches[B1].size())  / static_cast<double>(caches[B2].size());
        p = std::max(p-delta_2,0.);
        replace(true);
        //Remove from B2, no need to update indices as B_i is not considered for TL
        caches[B2].erase(page_data.at_iterator);
        //Insert into T2 MRU
        page_data.at_iterator = caches[T2].insert(caches[T2].end(),page);
        page_data.index = caches[T2].size()-1;
        page_data.in_list = T2;
    }
    else{
        if(caches[T1].size() + caches[B1].size() == page_cache_size){
            if(caches[T1].size() < page_cache_size){
                auto it_to_remove = caches[B1].begin();
                page_to_data.erase(*it_to_remove);
                caches[B1].erase(it_to_remove);
                replace(false);
            }
            else{
                auto it_to_remove = caches[T1].begin();
                page_to_data.erase(*it_to_remove);
                caches[T1].erase(it_to_remove);
            }
        }
        else{
            const auto total_size = caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size();
            if(total_size >= page_cache_size){
                if(total_size == 2*page_cache_size){
                    auto it_to_remove = caches[B2].begin();
                    page_to_data.erase(*it_to_remove);
                    caches[B1].erase(it_to_remove);
                }
                replace(false);
            }
        }
        //Put in T1 MRU, and update relevant indices
        auto ins_it = caches[T1].insert(caches[T1].begin(),page);
        page_data.at_iterator = ins_it;
        page_data.index = 0;
        page_data.in_list = T1;
        ins_it = std::next(ins_it);
        while(ins_it != caches[T1].end()){
            page_to_data[*ins_it].index++;
            ins_it = std::next(ins_it);
        }
    }
}

void ARC::remove_from_cache_and_update_indices(cache_list_idx cache, arc_cache_t::iterator page_it) {
    auto& relevant_cache = caches.at(cache);
    auto next = relevant_cache.erase(page_it);
    while(next!=relevant_cache.end()){
        page_to_data[*next].index--;
        next = std::next(next);
    }
}

void ARC::replace(bool inB2) {
    const auto t1_s = caches[T1].size();
    if( t1_s!=0 && ( (t1_s >= static_cast<size_t>(p)) || (inB2 && t1_s == static_cast<size_t>(p)) ) ){
        lru_to_mru(T1, B1);
    }
    else{
        lru_to_mru(T2,B2);
    }
}

void ARC::lru_to_mru(cache_list_idx from, cache_list_idx to) {
    auto page_it = caches.at(from).begin();
    const auto page = *page_it;
    auto & page_data = page_to_data[page];
    remove_from_cache_and_update_indices(from, page_it);
    page_data.in_list = to;
    page_data.at_iterator = caches.at(to).insert(caches.at(to).end(), page);
}
