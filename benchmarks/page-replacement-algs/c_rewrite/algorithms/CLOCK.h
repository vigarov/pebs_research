#ifndef C_REWRITE_CLOCK_H
#define C_REWRITE_CLOCK_H

#include <numeric>
#include "GenericAlgorithm.h"




class CLOCK : public GenericAlgorithm{
public:
    CLOCK(uint16_t page_cache_size,uint8_t i) : GenericAlgorithm(page_cache_size),i(i),num_count_i(i+1,0){};
    bool consume(page_t page_start) override;

    temp_t get_temperature(page_t page,std::optional<std::shared_ptr<nd_t>> necessary_data) const  override {
        if(necessary_data == std::nullopt){
            auto& page_data = page_to_data.at(page);
            auto delta = std::accumulate(num_count_i.begin(),num_count_i.begin()+page_data.counter,static_cast<size_t>(0)); // == (page_data.counter ? num_count_i[page_data.counter]: 0);
            return page_data.relative_index + delta;
        }
        else{
            auto& nd = std::get<CLOCK_temp_necessary_data>(*  necessary_data.value());
            auto& page_data = nd.prev_page_to_data.at(page);
            auto delta = std::accumulate(nd.prev_num_count_i.begin(),nd.prev_num_count_i.begin()+page_data.counter,static_cast<size_t>(0)); // == (page_data.counter ? num_count_i[page_data.counter]: 0);
            return page_data.relative_index + delta;
        }
    }
    std::unique_ptr<nd_t> get_necessary_data() override{
        CLOCK_temp_necessary_data ctnd = {page_to_data,num_count_i};
        return std::make_unique<nd_t>(std::move(ctnd));
    }
    inline bool is_page_fault(page_t page) const override {return !page_to_data.contains(page);};
    std::string name() override {return "GLOCK";};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    temp_t compare_to_previous_internal(std::shared_ptr<nd_t> prev_nd) override;
    const gclock_cache_t * get_cache_iterable() const {return &page_cache;}
private:
    std::string cache_to_string(size_t num_elements) override{
        if(num_elements>page_cache.size()) num_elements = page_cache.size();
        std::string ret("[");
        ret += page_iterable_to_str(page_cache.begin(),num_elements, page_cache.end());
        ret += ']';
        return ret;
    };
    gclock_cache_t page_cache{}; // idx 0 = should-be LRU; idx size-1 = should-be MRU
    std::unordered_map<page_t,CLOCK_page_data> page_to_data;
    void find_victim();
    size_t head = 0;
    const uint8_t i;
    std::vector<page_t> num_count_i;

    void update_page_counter_and_relevant_indices(CLOCK_page_data &page_data);
    inline void advance_head(){head = (head + 1) % page_cache.size();};
};


#endif //C_REWRITE_CLOCK_H
