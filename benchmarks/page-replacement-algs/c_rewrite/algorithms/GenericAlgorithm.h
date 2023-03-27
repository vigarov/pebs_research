#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <sstream>
#include <iterator>
#include <functional>
#include <deque>

typedef uint64_t ptr_t;
typedef ptr_t page_t;

typedef size_t temp_t;
typedef std::function<temp_t (ptr_t)> temp_function;

static constexpr uint16_t PAGE_SIZE = 4096;

enum{T1=0,T2,B1,B2,NUM_CACHES};

ptr_t page_start_from_mem_address(ptr_t x) {
    return x & ~(PAGE_SIZE - 1);
}



class GenericAlgorithm{
public:
    GenericAlgorithm(size_t page_cache_size) : page_cache_size(page_cache_size) {};
    virtual void consume(ptr_t page_start) = 0;
    virtual temp_function get_temparature_function() = 0;
    virtual inline bool is_page_fault(page_t page) = 0;
    virtual std::string name() = 0;
    virtual std::string toString() {return name() + " : cache = " +cache_to_string(10);};
protected:
    virtual std::string cache_to_string(size_t num_elements) = 0;
    template<typename Iterator>
    std::string page_iterable_to_str(Iterator start, Iterator end) {
        std::ostringstream oss;
        if(start != end) { //== non-empty
            std::copy(start, end - 1, std::ostream_iterator<page_t>(oss, ","));
            oss << *(end-1);
        }
        return oss.str();
    };
    size_t page_cache_size;
};