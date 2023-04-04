#ifndef C_REWRITE_LRU_H
#define C_REWRITE_LRU_H

#include "GenericAlgorithm.h"
#include <list>
#include <optional>

class LRU_K : public GenericAlgorithm{
public:
    LRU_K(uint16_t page_cache_size,uint8_t K) : GenericAlgorithm(page_cache_size),K(K){};
    bool consume(page_t page_start) override;
    temp_t get_temperature(page_t page,std::optional<std::shared_ptr<nd_t>> necessary_data) const  override {
        if(necessary_data == std::nullopt) return page_cache.size() - 1 - page_to_data.at(page).index;
        else{
            auto& nd = std::get<LRU_temp_necessary_data>(*  necessary_data.value());
            return nd.prev_page_to_data.size() - 1 - nd.prev_page_to_data.at(page).index;
        }
    }; // 0 = cold
    std::unique_ptr<nd_t> get_necessary_data() override{
        LRU_temp_necessary_data lrutnd{page_to_data};
        nd_t act_nd = std::move(lrutnd);
        return std::make_unique<nd_t>(std::move(act_nd));
    }
    inline bool is_page_fault(page_t page) const override {return !page_to_data.contains(page);};
    std::string name() override {return "LRU_"+std::to_string(K);};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    temp_t compare_to_previous(std::shared_ptr<nd_t> prev_nd) override;
    const lru_cache_t * get_cache_iterable() const {return &page_cache;}
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),num_elements,page_cache.end());
        ret += ']';
        return ret;
    };
    lru_cache_t page_cache{}; // idx 0 = MRU; idx size-1 = LRU, sorted by second history
    const uint8_t K;
    std::unordered_map<page_t,LRU_page_data> page_to_data;
    uint64_t count_stamp = 0;
};


#endif //C_REWRITE_LRU_H
