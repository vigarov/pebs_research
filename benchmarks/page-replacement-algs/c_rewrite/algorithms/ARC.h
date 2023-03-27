#ifndef C_REWRITE_ARC_H
#define C_REWRITE_ARC_H


#include "GenericAlgorithm.h"

typedef std::deque<page_t> arc_cache_t;

struct ARC_page_data{
    size_t index=0;
};

class ARC : public GenericAlgorithm{
public:
    ARC(uint16_t page_cache_size) : GenericAlgorithm(page_cache_size){};
    void consume(page_t page_start) override;
    temp_function get_temparature_function() override { return [this](page_t page_base){return page_to_data[page_base].index;}; };
    inline bool is_page_fault(page_t page) override {return page_to_data.contains(page);};
    std::string name() override {return "ARC";};
private:
    std::string cache_to_string(size_t num_elements) override{
        std::string ret;
        long num_left_elements = static_cast<long>(num_elements);
        for (int i = T1; i <= T2; ++i && num_elements>0) {
            if(i == T2 && num_left_elements>caches[i].size()) num_left_elements = static_cast<long>(caches[i].size());
            ret += std::to_string(i)+": ";
            std::string cache_answ("[");
            cache_answ += page_iterable_to_str(caches[i].begin(), caches[i].begin() + num_left_elements);
            cache_answ += ']';
            ret += cache_answ;
            num_left_elements -= static_cast<long>(caches[i].size());
        }
        return ret;
    };
    arc_cache_t caches[4]{}; // idx 0 = LRU; idx size-1 = MRU
    std::unordered_map<page_t,ARC_page_data> page_to_data;
    double p = 0.;
    void replace(bool inB2);
};


#endif //C_REWRITE_ARC_H
