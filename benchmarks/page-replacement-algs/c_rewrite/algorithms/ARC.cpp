#include "ARC.h"

evict_return_t ARC::consume_tracked(page_t page_start) {
    auto &page_data_internal = page_to_data_internal[page_start];
    auto in_cache = (page_data_internal.in_list == T1 || page_data_internal.in_list == T2);

    evict_return_t ret = std::nullopt;
    if (in_cache) {
        caches[page_data_internal.in_list].erase(page_data_internal.at_iterator);
        page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(), page_start);
        page_data_internal.in_list = T2;
        ret = std::nullopt;
    }
    else {
        if (page_data_internal.in_list == B1) {
            const double delta_1 =
                    caches[B1].size() >= caches[B2].size() ? 1. : static_cast<double>(caches[B2].size()) /
                                                                  static_cast<double>(caches[B1].size());
            p = std::min(p + delta_1, static_cast<double>(max_page_cache_size));
            if(page_cache_full()) {
                if(U->size() == 0) {
                    ret = replace(false);
                }
                else{
                    ret = U->evict();
                }
            }
            //Remove from B1, no need to update indices as B_i is not considered for TL
            caches[B1].erase(page_data_internal.at_iterator);
            //Insert into T2 MRU
            page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(), page_start);
            page_data_internal.in_list = T2;

        }
        else if (page_data_internal.in_list == B2) {
            const double delta_2 = caches[B2].size() >= caches[B1].size() ? 1. : static_cast<double>(caches[B1].size()) /
                                                                                 static_cast<double>(caches[B2].size());
            p = std::max(p - delta_2, 0.);
            if(page_cache_full()) {
                if(U->size() == 0) {
                    ret = replace(true);
                }
                else{
                    ret = U->evict();
                }
            }
            //Remove from B2, no need to update indices as B_i is not considered for TL
            caches[B2].erase(page_data_internal.at_iterator);
            //Insert into T2 MRU
            page_data_internal.at_iterator = caches[T2].insert(caches[T2].end(), page_start);
            page_data_internal.in_list = T2;
        } else {
            if (caches[T1].size() + caches[B1].size() == max_page_cache_size) {
                if (caches[T1].size() < max_page_cache_size) {
                    auto it_to_remove = caches[B1].begin();
                    page_to_data_internal.erase(*it_to_remove);
                    caches[B1].erase(it_to_remove);
                    ret = replace(false);
                } else {
                    auto it_to_remove = caches[T1].begin();
                    auto page = *it_to_remove;
                    caches[T1].erase(it_to_remove);
                    page_to_data_internal.erase(page);
                    ret = page;
                }
            } else {
                //This is the branch taken until |T1|+|T2| fills up (as nothing is "demoted" to B_i before
                const auto total_size = caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size();
                if (page_cache_full() && U->size() != 0) ret = U->evict(); // This implies that |T1|+|T2| < max_page_cache_size
                else if (total_size >= max_page_cache_size) {
                    if (total_size == 2 * max_page_cache_size) {
                        auto it_to_remove = caches[B2].begin();
                        page_to_data_internal.erase(*it_to_remove);
                        caches[B2].erase(it_to_remove);
                    }
                    ret = replace(false);
                }
            }
            //Put in T1 MRU, and update relevant indices
            auto ins_it = caches[T1].insert(caches[T1].end(), page_start);
            page_data_internal.at_iterator = ins_it;
            page_data_internal.in_list = T1;
        }
    }
    return ret;
}

page_t ARC::replace(bool inB2) {
    const auto t1_s = caches[T1].size();
    page_t ret;
    if( t1_s!=0 && ( (t1_s >= static_cast<size_t>(p)) || (inB2 && t1_s == static_cast<size_t>(p)) ) ){
        ret = lru_to_mru(T1, B1);
    }
    else{
        ret = lru_to_mru(T2,B2);
    }
    return ret;
}

page_t ARC::lru_to_mru(cache_list_idx from, cache_list_idx to) {
    auto page_it = caches.at(from).begin();
    const auto page = *page_it;
    auto & page_data_internal = page_to_data_internal[page];
    caches[from].erase(page_it);
    page_data_internal.in_list = to;
    page_data_internal.at_iterator = caches.at(to).insert(caches.at(to).end(), page);
    return page;
}

std::unique_ptr<page_cache_copy_t> ARC::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.end(), caches[T1].begin(), caches[T1].end());
    concatenated_list.insert(concatenated_list.end(), caches[T2].begin(), caches[T2].end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}

evict_return_t ARC::evict_from_tracked() {
    //This will be called iff a memory access is done, and is not part of the mem trace, yet the cache is full, and U is empty
    //--> we must call `replace(false)`, as if we were in the last clause of ARC's consume (page of fault of some page that is not referenced in neither lists
    //even though it can be - as ARC is technically not aware of it
    const auto total_size = caches[T1].size() + caches[T2].size() + caches[B1].size() + caches[B2].size();
    if(tracked_size()==0) return std::nullopt;

    auto ret = replace(false);

    //Make size in the directory if needed
    if(total_size >= 2 * max_page_cache_size){
        page_to_data_internal.erase(caches[B2].front());
        caches[B2].pop_front();
    }
    else if(caches[T1].size() + caches[B1].size() >= max_page_cache_size){
        page_to_data_internal.erase(caches[B1].front());
        caches[B1].pop_front();
    }
    return ret;
}