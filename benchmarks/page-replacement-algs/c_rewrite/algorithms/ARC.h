#ifndef C_REWRITE_ARC_H
#define C_REWRITE_ARC_H


#include "GenericAlgorithm.h"
#include <list>
#include <iostream>


class ARC : public GenericAlgorithm{
public:
    explicit ARC(uint16_t page_cache_size) : GenericAlgorithm(page_cache_size){};
    bool consume(page_t page_start) override;
    temp_t get_temperature(page_t page,std::optional<std::shared_ptr<nd_t>> necessary_data) const  override {
        if (necessary_data == std::nullopt) {
            const auto &page_data = page_to_data.at(page);
            auto temp = page_data.index;
            if (page_data.in_list == T2) temp += caches[T1].size();
            return temp;
        }
        else{
            auto& nd = std::get<ARC_temp_necessary_data>(*  necessary_data.value());
            const auto& page_data = nd.prev_page_to_data.at(page);
            auto temp = page_data.index;
            if (page_data.in_list == T2) temp += nd.prev_T1_cache_size;
            return temp;
        }
    }// 0 = coldest
    std::unique_ptr<nd_t> get_necessary_data() override{
        ARC_temp_necessary_data arctnd{page_to_data,caches[T1].size()};
        return std::make_unique<nd_t>(std::move(arctnd));
    }
    inline bool is_page_fault(page_t page) const  override {return !page_to_data.contains(page) || page_to_data.at(page).in_list == B1 || page_to_data.at(page).in_list == B2;};
    std::string name() override {return "ARC";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    temp_t compare_to_previous(std::shared_ptr<nd_t> prev_nd) override;
    const dual_container_range<arc_cache_t>* get_cache_iterable() const {return &dcr;}
private:
    std::string cache_to_string(size_t num_elements) override{
        std::string ret;
        long long num_left_elements = static_cast<long>(num_elements);
        for (int i = T1; i <= T2 && num_left_elements>0; ++i) {
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

    dual_container_range<car_cache_t> dcr{caches[T1],caches[T2]};
    void lru_to_mru(cache_list_idx from, cache_list_idx to);
};



#endif //C_REWRITE_ARC_H
