#ifndef C_REWRITE_CAR_H
#define C_REWRITE_CAR_H

#include "GenericAlgorithm.h"
#include <list>
#include <iostream>



class CAR : public GenericAlgorithm{
public:
    explicit CAR(uint16_t page_cache_size) : GenericAlgorithm(page_cache_size){};
    bool consume(page_t page_start) override;
    temp_t get_temperature(page_t page,std::optional<std::shared_ptr<nd_t>> necessary_data) const  override {
        if (necessary_data == std::nullopt) {
            const auto &page_data = page_to_data.at(page);
            //The order is T_1^0,T_2^0,T_1^1,T_2^1
            if (!page_data.referenced) {
                if (page_data.in_list == T1) return page_data.relative_index;
                else return num_unreferenced[T1] + page_data.relative_index;
            } else {
                if (page_data.in_list == T1)
                    return num_unreferenced[T1] + num_unreferenced[T2] + page_data.relative_index;
                else return caches[T1].size() + num_unreferenced[T2] + page_data.relative_index;
            }
        }
        else{
            auto& nd = std::get<CAR_temp_necessary_data>(*  necessary_data.value());
            const auto& page_data = nd.prev_page_to_data.at(page);
            if (!page_data.referenced) {
                if (page_data.in_list == T1) return page_data.relative_index;
                else return nd.prev_num_unreferenced[T1] + page_data.relative_index;
            } else {
                if (page_data.in_list == T1)
                    return nd.prev_num_unreferenced[T1] + nd.prev_num_unreferenced[T2] + page_data.relative_index;
                else return nd.prev_t1_cs + nd.prev_num_unreferenced[T2] + page_data.relative_index;
            }
        }
    }// 0 = coldest
    std::unique_ptr<nd_t> get_necessary_data() override{
        CAR_temp_necessary_data cartnd{page_to_data,num_unreferenced,caches[T1].size()};
        return std::make_unique<nd_t>(std::move(cartnd));
    }
    inline bool is_page_fault(page_t page) const override {return !page_to_data.contains(page) || page_to_data.at(page).in_list == B1 || page_to_data.at(page).in_list == B2;};
    std::string name() override {return "CAR";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    temp_t compare_to_previous_internal(std::shared_ptr<nd_t> prev_nd) override;
    const dual_container_range<car_cache_t>* get_cache_iterable() const {return &dcr;}
private:
    std::string cache_to_string(size_t num_elements) override{
        std::string ret;
        long long num_left_elements = static_cast<long>(num_elements);
        for (int i = T1; i <= T2 && num_left_elements>0; ++i) {
            auto& relevant_cache = caches.at(i);
            if(i == T2 && num_left_elements>static_cast<long long>(relevant_cache.size())) num_left_elements = static_cast<long>(relevant_cache.size());
            ret += std::to_string(i)+": ";
            std::string cache_answ("[");
            cache_answ += page_iterable_to_str(relevant_cache.begin(),num_left_elements,relevant_cache.end());
            cache_answ += ']';
            ret += cache_answ;
            num_left_elements -= static_cast<long>(relevant_cache.size());
        }
        return ret;
    };
    std::array<car_cache_t,NUM_CACHES> caches{}; // idx 0 = LRU; idx size-1 = MRU ; tentative for L1, L2 (=== 0 = head, size-1 = tail)
    std::unordered_map<page_t,CAR_page_data> page_to_data;
    double p = 0.;
    std::array<size_t,2> num_unreferenced{};
    void replace();
    dual_container_range<car_cache_t> dcr{caches[T1],caches[T2]};

    void update_relative_indices(const CAR_page_data& data);
};


#endif //C_REWRITE_CAR_H
