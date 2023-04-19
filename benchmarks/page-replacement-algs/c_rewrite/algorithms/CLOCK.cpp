//
// Created by vigarov on 26/03/23.
//

#include "CLOCK.h"

bool CLOCK::consume(page_t page_start){
    auto page_fault = is_page_fault(page_start);
    auto& page_data = page_to_data[page_start];
    auto& page_data_internal = page_to_data_internal[page_start];
    redundant_pfault = false;
    known_value = 0;
    if(page_fault){
        if(page_cache.size() == page_cache_size){
            find_victim();
            auto head_page = page_cache[head];
            page_to_data.erase(head_page);
            page_to_data_internal.erase(head_page);
            page_cache[page_data_internal.index = head] = page_start;
            advance_head();
        }
        else{
            page_data_internal.index = page_cache.size();
            page_cache.push_back(page_start);
            redundant_pfault = true;
        }
    }
    bool const changed = page_data.counter!=i;
    update_page_counter_and_relevant_indices(page_data); // TODO page_data.counter = i;  
    return changed;
}

void CLOCK::find_victim() {
    while(auto& counter = page_to_data[page_cache[head]].counter){
        counter--;
        advance_head();
    }
}

std::unique_ptr<page_cache_copy_t> CLOCK::get_page_cache_copy() {
    page_cache_copy_t concatenated_list;
    concatenated_list.insert(concatenated_list.end(), page_cache.begin(), page_cache.end());
    return std::make_unique<page_cache_copy_t>(concatenated_list);
}

void CLOCK::update_page_counter_and_relevant_indices(CLOCK_page_data& page_data) {
    if (page_data.counter != i) {
        page_data.counter = i;
        std::for_each(num_count_i.begin(), num_count_i.end(),[](auto& num){num=0;});
        const size_t cache_size = page_cache.size();
        for(size_t at = head, num_iterations =0; num_iterations<cache_size;++num_iterations,at = (at+1)%cache_size){
            auto& pd = page_to_data[page_cache[at]];
            pd.relative_index = num_count_i[pd.counter]++;
        }
        for(size_t j = 1;j<num_count_i.size();j++){
            deltas[j] = num_count_i[j-1] + deltas[j-1];
        }
    }
}

temp_t CLOCK::compare_to_previous_internal(std::shared_ptr<nd_t> prev_nd) {
    temp_t sum = 0;
    auto& prevptd = std::get<CLOCK_temp_necessary_data>(*prev_nd).prev_page_to_data;
    for(const auto& page: page_cache){
        if(prevptd.contains(page)){
            sum+=std::abs(static_cast<long long>(get_temperature(page,std::nullopt)) - static_cast<long long>(get_temperature(page,prev_nd)));
        }
    }
    return sum;
}
