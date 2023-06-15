#ifndef C_REWRITE_LRU_H
#define C_REWRITE_LRU_H

#include "GenericAlgorithm.h"
#include <list>
#include <optional>

class LRU_K : public GenericAlgorithm{
public:
    LRU_K(uint16_t page_cache_size,uint8_t K) : GenericAlgorithm(page_cache_size),K(K){};
    bool consume(page_t page_start) override;
    inline bool is_page_fault(page_t page) const override {return !page_to_data_internal.contains(page);};
    std::string name() override {return "LRU_"+std::to_string(K);};
    std::unique_ptr<page_cache_copy_t> get_page_cache_copy() override;
    const lru_cache_t * get_cache_iterable() const {return &page_cache;}
    std::optional<std::pair<lru_cache_t::const_iterator,lru_cache_t::const_iterator>> get_nth_iterator_pair(size_t n){
        if((n+1)>=iterators.size()) return std::nullopt;
        return std::pair(*std::next(iterators.cbegin(),static_cast<long>(n)),*std::next(iterators.cbegin(),static_cast<long>(n+1)));
    }
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
    std::unordered_map<page_t,LRU_page_data_internal> page_to_data_internal;
    uint64_t count_stamp = 0;
    lru_cache_t::iterator latest_first_access_page = page_cache.end();

    std::list<lru_cache_t::const_iterator> iterators = {page_cache.end()};
};


#endif //C_REWRITE_LRU_H
