#include "ARC.h"

bool ARC::consume(page_t page_start) {
    auto& page_data_internal = page_to_data_internal[page_start];
    auto in_cache = (page_data_internal.in_list == T1 || page_data_internal.in_list == T2);
    const bool not_changed = page_data_internal.in_list==T2 && (std::next(page_data_internal.at_iterator) == caches[T2].end());

    if (in_cache){
        caches[page_data_internal.in_list].erase(page_data_internal.at_iterator);
        page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(),page_start);
        page_data_internal.in_list = T2;
    }
    else if(page_data_internal.in_list == B1){
        const double delta_1 = caches[B1].size() >= caches[B2].size() ? 1. : static_cast<double>(caches[B2].size())  / static_cast<double>(caches[B1].size());
        p = std::min(p+delta_1,static_cast<double>(page_cache_size));
        replace(false);
        //Remove from B1, no need to update indices as B_i is not considered for TL
        caches[B1].erase(page_data_internal.at_iterator);
        //Insert into T2 MRU
        page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(),page_start);
        page_data_internal.in_list = T2;
    }
    else if(page_data_internal.in_list == B2){
        const double delta_2 = caches[B2].size() >= caches[B1].size() ? 1. : static_cast<double>(caches[B1].size())  / static_cast<double>(caches[B2].size());
        p = std::max(p-delta_2,0.);
        replace(true);
        //Remove from B2, no need to update indices as B_i is not considered for TL
        caches[B2].erase(page_data_internal.at_iterator);
        //Insert into T2 MRU
        page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(),page_start);
        page_data_internal.in_list = T2;
    }
    else{
        if(caches[T1].size() + caches[B1].size() == page_cache_size){
            if(caches[T1].size() < page_cache_size){
                auto it_to_remove = caches[B1].begin();
                page_to_data_internal.erase(*it_to_remove);
                caches[B1].erase(it_to_remove);
                replace(false);
            }
            else{
                auto it_to_remove = caches[T1].begin();
                auto page = *it_to_remove;
                caches[T1].erase(it_to_remove);
                page_to_data_internal.erase(page);
            }
        }
        else{
            const auto total_size = caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size();
            if(total_size >= page_cache_size){
                if(total_size == 2*page_cache_size){
                    auto it_to_remove = caches[B2].begin();
                    page_to_data_internal.erase(*it_to_remove);
                    caches[B2].erase(it_to_remove);
                }
                replace(false);
            }
        }
        //Put in T1 MRU, and update relevant indices
        auto ins_it = caches[T1].insert(caches[T1].end(),page_start);
        page_data_internal.at_iterator = ins_it;
        page_data_internal.in_list = T1;
    }
    return !not_changed;
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
    auto & page_data_internal = page_to_data_internal[page];
    caches[from].erase(page_it);
    page_data_internal.in_list = to;
    page_data_internal.at_iterator = caches.at(to).insert(caches.at(to).end(), page);
}

std::unique_ptr<page_cache_copy_t> ARC::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.end(), caches[T1].begin(), caches[T1].end());
    concatenated_list.insert(concatenated_list.end(), caches[T2].begin(), caches[T2].end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}