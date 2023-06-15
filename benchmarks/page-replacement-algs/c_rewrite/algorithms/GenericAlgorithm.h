#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <sstream>
#include <iterator>
#include <functional>
#include <deque>
#include <utility>
#include <vector>
#include <memory>
#include <list>
#include <optional>
#include <variant>

typedef uint64_t ptr_t;
typedef ptr_t page_t;

typedef uint32_t temp_t;

static constexpr uint16_t PAGE_SIZE = 4096;

enum cache_list_idx{T1=0,T2,B1,B2,NUM_CACHES};

inline static page_t page_start_from_mem_address(ptr_t x) {
    return x & ~(PAGE_SIZE - 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef std::list<page_t> lru_cache_t;

struct LRU_page_data_internal{
    std::deque<uint64_t> history;
    lru_cache_t::iterator at_iterator;
    LRU_page_data_internal(): LRU_page_data_internal(2) {}
    explicit LRU_page_data_internal(uint8_t K) : history(K){}
};

///~~~~

typedef std::list<page_t> gclock_cache_t;

struct CLOCK_page_data_internal{
    uint8_t counter = 0;
    gclock_cache_t::iterator at_iterator;
};

///~~~~

typedef std::list<page_t> car_cache_t;

struct CAR_page_data_internal {
    uint8_t referenced = 0;
    cache_list_idx in_list = NUM_CACHES;
    car_cache_t::iterator at_iterator;
};

///~~~~

typedef std::list<page_t> arc_cache_t;

struct ARC_page_data_internal{
    cache_list_idx in_list = NUM_CACHES;
    arc_cache_t::iterator at_iterator;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef std::vector<page_t> page_cache_copy_t;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class dual_container_iterator : public std::iterator<std::forward_iterator_tag, typename T::value_type> {
public:
    using iterator_type = typename T::const_iterator;

    dual_container_iterator() : current{}, end1{}, begin2{}, end2{}  {}

    dual_container_iterator(iterator_type begin1, iterator_type end1, iterator_type begin2, iterator_type end2)
            : current(begin1 != end1 ? begin1 : begin2), end1(end1), begin2(begin2), end2(end2) {}

    dual_container_iterator([[maybe_unused]] iterator_type begin1, iterator_type end1, iterator_type begin2, iterator_type end2, iterator_type current)
            : current(current), end1(end1), begin2(begin2), end2(end2) {}

    dual_container_iterator& operator++() {
        auto next = std::next(current);
        if (next == end1) {
            current = begin2;
        } else if(current != end2){
            ++current;
        }
        return *this;
    }

    bool operator==(const dual_container_iterator& other) const {
        return current == other.current;
    }

    bool operator!=(const dual_container_iterator& other) const {
        return !(*this == other);
    }

    auto& operator*() const {
        return *current;
    }

    auto& operator->() const {
        return current;
    }

private:
    iterator_type current;
    iterator_type end1;
    iterator_type begin2;
    iterator_type end2;
};

template <typename T>
class dual_container_range {

public:
    dual_container_range(T& container1, T& container2)
            : container1(container1), container2(container2) {}

    explicit dual_container_range(T& unique_container):container1(unique_container),container2(unique_container){}

    auto begin() const {
        return dual_container_iterator<T>(container1.begin(), container1.end(),
                                          container2.begin(), container2.end());
    }

    auto end() const {
        return dual_container_iterator<T>(container1.begin(), container1.end(),
                                          container2.begin(), container2.end(),container2.end());
    }

private:
    const T& container1;
    const T& container2;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class GenericAlgorithm{
public:
    explicit GenericAlgorithm(size_t page_cache_size) : page_cache_size(page_cache_size) {};
    virtual bool consume(page_t page_start) = 0;
    [[nodiscard]] virtual inline bool is_page_fault(page_t page) const  = 0;
    virtual std::string name() = 0;
    virtual std::string toString() {return name() + " : cache = " +cache_to_string(10);};
    virtual std::unique_ptr<page_cache_copy_t> get_page_cache_copy() = 0;
    virtual ~GenericAlgorithm() = default;
protected:
    virtual std::string cache_to_string(size_t num_elements) = 0;
    template<typename Iterator>
    std::string page_iterable_to_str(Iterator start, size_t num_elements, Iterator max) {
        std::ostringstream oss;
        if(start != max) { //== non-empty
            while(num_elements-- && start!=max){
                oss << std::hex << *start << ',';
                start = std::next(start);
            }
        }
        return oss.str();
    };
    size_t page_cache_size;
};