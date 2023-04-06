#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <sstream>
#include <iterator>
#include <functional>
#include <deque>
#include <vector>
#include <memory>
#include <list>
#include <optional>
#include <variant>

typedef uint64_t ptr_t;
typedef ptr_t page_t;

typedef size_t temp_t;

static constexpr uint16_t PAGE_SIZE = 4096;

enum cache_list_idx{T1=0,T2,B1,B2,NUM_CACHES};

inline static page_t page_start_from_mem_address(ptr_t x) {
    return x & ~(PAGE_SIZE - 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef std::list<page_t> lru_cache_t;

struct LRU_page_data{
    size_t index=0;
    std::deque<uint64_t> history;
    lru_cache_t::iterator at_iterator;
    LRU_page_data(): LRU_page_data(2) {}
    explicit LRU_page_data(uint8_t K) : history(K){}
};

struct LRU_temp_necessary_data{
    std::unordered_map<page_t,LRU_page_data> prev_page_to_data;
};

typedef std::deque<page_t> gclock_cache_t;

struct CLOCK_page_data{
    size_t relative_index=0;
    size_t index=0;
    uint8_t counter = 0;
};

struct CLOCK_temp_necessary_data{
    std::unordered_map<page_t,CLOCK_page_data> prev_page_to_data;
    std::vector<page_t> prev_num_count_i;
};

typedef std::list<page_t> car_cache_t;

struct CAR_page_data{
    size_t relative_index=0;
    uint8_t referenced = 0;
    cache_list_idx in_list = NUM_CACHES;
    car_cache_t::iterator at_iterator;
};

struct CAR_temp_necessary_data{
    std::unordered_map<page_t,CAR_page_data> prev_page_to_data;
    std::array<size_t,2> prev_num_unreferenced;
    size_t prev_t1_cs;
};

typedef std::list<page_t> arc_cache_t;

struct ARC_page_data{
    size_t index=0;
    cache_list_idx in_list = NUM_CACHES;
    arc_cache_t::iterator at_iterator;
};

struct ARC_temp_necessary_data{
    std::unordered_map<page_t,ARC_page_data> prev_page_to_data;
    size_t prev_T1_cache_size;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef std::variant<LRU_temp_necessary_data,CLOCK_temp_necessary_data,ARC_temp_necessary_data,CAR_temp_necessary_data> nd_t;

typedef std::vector<page_t> page_cache_copy_t;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


template <typename T>
class dual_container_iterator : public std::iterator<std::forward_iterator_tag, typename T::value_type> {
public:
    using iterator_type = typename T::const_iterator;

    dual_container_iterator() : current{}, end1{}, end2{} {}

    dual_container_iterator(iterator_type begin1, iterator_type end1, iterator_type begin2, iterator_type end2)
            : current(begin1 != end1 ? begin1 : begin2), end1(end1), end2(end2), begin2(begin2) {}

    dual_container_iterator(iterator_type begin1, iterator_type end1, iterator_type begin2, iterator_type end2, iterator_type current)
            : current(current), end1(end1), end2(end2), begin2(begin2) {}

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
    iterator_type begin2;
    iterator_type end1;
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

typedef std::variant<const lru_cache_t*,const gclock_cache_t*,const dual_container_range<std::list<page_t>>*> iterable_t;

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class GenericAlgorithm{
public:
    GenericAlgorithm(size_t page_cache_size) : page_cache_size(page_cache_size) {};
    virtual bool consume(ptr_t page_start) = 0;
    virtual temp_t get_temperature(page_t page,std::optional<std::shared_ptr<nd_t>> necessary_data) const = 0;
    virtual std::unique_ptr<nd_t> get_necessary_data() = 0;
    virtual inline bool is_page_fault(page_t page) const  = 0;
    virtual std::string name() = 0;
    virtual std::string toString() {return name() + " : cache = " +cache_to_string(10);};
    virtual std::unique_ptr<page_cache_copy_t> get_page_cache_copy() = 0;
    virtual temp_t compare_to_previous(std::shared_ptr<nd_t> prev_nd) = 0;
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