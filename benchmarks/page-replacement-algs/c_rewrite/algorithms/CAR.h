#ifndef C_REWRITE_CAR_H
#define C_REWRITE_CAR_H

#include "GenericAlgorithm.h"
#include <list>
#include <iostream>



class CAR : public GenericAlgorithm{
public:
    explicit CAR(size_t page_cache_size) : GenericAlgorithm(page_cache_size){};
    bool consume(page_t page_start) override;
    inline bool is_page_fault(page_t page) const override {return !page_to_data_internal.contains(page) || page_to_data_internal.at(page).in_list == B1 || page_to_data_internal.at(page).in_list == B2;};
    std::string name() override {return "CAR";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
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
    std::unordered_map<page_t,CAR_page_data_internal> page_to_data_internal;
    double p = 0.;
    std::array<size_t,2> num_unreferenced{};
    void replace();
    dual_container_range<car_cache_t> dcr{caches[T1],caches[T2]};
    std::list<dual_container_iterator<car_cache_t>> iterators = {dcr.end()};
};


#endif //C_REWRITE_CAR_H
