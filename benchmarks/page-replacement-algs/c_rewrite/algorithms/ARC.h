#ifndef C_REWRITE_ARC_H
#define C_REWRITE_ARC_H


#include "GenericAlgorithm.h"
#include <list>
#include <iostream>

typedef std::list<page_t> arc_cache_t;

struct ARC_page_data{
    size_t index=0;
    cache_list_idx in_list = NUM_CACHES;
    arc_cache_t::iterator at_iterator;
};


class ARC : public GenericAlgorithm{
public:
    ARC(uint16_t page_cache_size) : GenericAlgorithm(page_cache_size){};
    void consume(page_t page_start) override;
    temp_function get_temparature_function() override { return [this](page_t page_base){
        if(is_page_fault(page_base)) std::cerr<<"Error";
        const auto& page_data = page_to_data[page_base];
        auto temp = page_data.index;
        if(page_data.in_list == T2) temp += caches[T1].size()-1;
        return temp;
    }; }; // 0 = coldest
    inline bool is_page_fault(page_t page) override {return !page_to_data.contains(page) || page_to_data[page].in_list == B1 || page_to_data[page].in_list == B2;};
    std::string name() override {return "ARC";};
private:
    std::string cache_to_string(size_t num_elements) override{
        std::string ret;
        long num_left_elements = static_cast<long>(num_elements);
        for (int i = T1; i <= T2; ++i && num_elements>0) {
            auto& relevant_cache = caches.at(i);
            if(i == T2 && num_left_elements>relevant_cache.size()) num_left_elements = static_cast<long>(relevant_cache.size());
            ret += std::to_string(i)+": ";
            std::string cache_answ("[");
            cache_answ += page_iterable_to_str(relevant_cache.begin(),num_left_elements,relevant_cache.end());
            cache_answ += ']';
            ret += cache_answ;
            num_left_elements -= static_cast<long>(relevant_cache.size());
        }
        return ret;
    };
    std::array<arc_cache_t,NUM_CACHES> caches{}; // idx 0 = LRU; idx size-1 = MRU
    std::unordered_map<page_t,ARC_page_data> page_to_data;
    double p = 0.;
    void replace(bool inB2);
    void remove_from_cache_and_update_indices(cache_list_idx cache, arc_cache_t::iterator page_it);

    void lru_to_mru(cache_list_idx from, cache_list_idx to);
};


#endif //C_REWRITE_ARC_H
