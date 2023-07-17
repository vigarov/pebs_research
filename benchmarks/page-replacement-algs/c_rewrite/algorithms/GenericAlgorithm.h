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
#include <random>
#include <iostream>

typedef uint64_t ptr_t;
typedef ptr_t page_t;
typedef std::optional<page_t> evict_return_t;

typedef uint32_t temp_t;

static constexpr uint16_t PAGE_SIZE = 4096;

enum cache_list_idx{T1=0,T2,B1,B2,NUM_CACHES};

inline static page_t page_start_from_mem_address(ptr_t x) {
    return x & ~(PAGE_SIZE - 1);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////

typedef std::list<page_t> lru_k_cache_t;

struct LRU_K_page_data_internal{
    std::deque<uint64_t> history;
    lru_k_cache_t::iterator at_iterator;
    LRU_K_page_data_internal(): LRU_K_page_data_internal(2) {}
    explicit LRU_K_page_data_internal(uint8_t K) : history(K){}
};

///~~~~
typedef std::list<page_t> lru_cache_t;

struct LRU_page_data_internal{
    lru_cache_t::iterator at_iterator;
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


template <typename T_const_iterator_type,typename T_value_type>
class dual_container_iterator : public std::iterator<std::forward_iterator_tag, T_value_type> {
public:
    using iterator_type = T_const_iterator_type;

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
            current = next;
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

template <typename T1, typename T2>
requires (std::is_same<typename T1::value_type,typename T2::value_type>::value and std::is_same<typename T1::const_iterator,typename T2::const_iterator>::value)
class dual_container_range {
public:
    using value_type = typename T1::value_type;
    using iterator = dual_container_iterator<typename T1::const_iterator,value_type>;
    dual_container_range(T1& container1, T2& container2)
            : container1(container1), container2(container2) {}

    explicit dual_container_range(T1& unique_container)
            : container1(unique_container), container2(unique_container) {}

    auto begin() const {
        return iterator(container1.begin(), container1.end(), container2.begin(), container2.end());
    }

    auto end() const {
        return iterator(container1.begin(), container1.end(), container2.begin(), container2.end(), container2.end());
    }

    [[nodiscard]] size_t size() const {
        return container1.size() + container2.size();
    }

private:
    const T1& container1;
    const T2& container2;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

template<typename T>
class SimpleContainer{
public:
    SimpleContainer() = default;
    virtual ~SimpleContainer() = default;
    virtual bool contains(const T& element) = 0;
    virtual bool insert(const T& element) = 0;
    virtual bool erase(const T& element) = 0;
    [[nodiscard]] virtual evict_return_t evict() = 0;
    virtual size_t size() = 0;
};


//Implementation of https://stackoverflow.com/questions/5682218/data-structure-insert-remove-contains-get-random-element-all-at-o1

struct __attribute__((packed)) RandomSetInfo{
    size_t index;
};

template<typename T>
class RandomSet : public SimpleContainer<T>{
public:
    RandomSet(){
        std::random_device dev;
        rng = std::mt19937(dev());
    }
    bool contains(const T& element) override{
        return elem_info.contains(element);
    };
    bool insert(const T& element) override{
        if(!contains(element)){
            elem_info[element] = {elems.size()};
            elems.push_back(element);
            return true;
        }
        return false;
    };
    bool erase(const T& element) override{
        if(contains(element)){
            auto delete_elem_info = elem_info[element];
            if(delete_elem_info.index != (elems.size()-1)) {
                auto last_elem = elems[elems.size() - 1];
                auto &last_elem_info = elem_info[last_elem];
                elems[(last_elem_info.index = delete_elem_info.index)] = last_elem;
            }
            elems.pop_back();
            elem_info.erase(element);
            return true;
        }
        return false;
    };

    evict_return_t get_random(bool andErase = false){
        auto elem_size =elems.size();
        if(elem_size == 0) return std::nullopt;
        std::uniform_int_distribution<std::mt19937::result_type> dist(0,elem_size-1);
        auto ret = elems[dist(rng)];
        if(andErase) erase(ret);
        return ret;
    };
    evict_return_t pop_random(){
        return get_random(true);
    };

    evict_return_t evict() override{
        return pop_random();
    };

    size_t size() override{return elems.size();}
private:
    std::unordered_map<T,RandomSetInfo> elem_info;
    std::vector<T> elems;
    std::mt19937 rng;
};

template<typename T>
struct ListAdapterInfo{
    std::list<T>::iterator it;
};

template<typename T>
class ListAdapter : public SimpleContainer<T>{
public:
    ListAdapter() = default;
    bool contains(const T& element) override{
        return elem_info.contains(element);
    };
    bool insert(const T& element) override{
        if(!contains(element)){
            elem_info[element] = {elems.insert(elems.end(),element)};
            return true;
        }
        return false;
    };
    bool erase(const T& element) override{
        if(contains(element)){
            elems.erase(elem_info[element].it);
            elem_info.erase(element);
            return true;
        }
        return false;
    };
    evict_return_t evict() override{
        if(elems.empty()) return std::nullopt;
        auto elem = elems.front();
        elem_info.erase(elem);
        elems.pop_front();
        return elem;
    };
    size_t size() override{
        return elems.size();
    };
private:
    std::unordered_map<T,ListAdapterInfo<T>> elem_info;
    std::list<T> elems;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

namespace untracked_eviction {
    enum type : uint8_t {
        FIFO = 0, RANDOM
    };
    static constexpr auto all = std::array{FIFO,RANDOM};
    inline std::string get_prefix(type t){
        switch (t) {
            case FIFO:
                return "fifo";
            case RANDOM:
                return "random";
            default:
                return "unknown";
        }
    }
}


class GenericAlgorithm{
public:
    explicit GenericAlgorithm(size_t page_cache_size,untracked_eviction::type evictionType) : max_page_cache_size(page_cache_size) {
        if (evictionType==untracked_eviction::FIFO){
            U = dynamic_cast<SimpleContainer<page_t>*>(new ListAdapter<page_t>());
        }
        else if(evictionType==untracked_eviction::RANDOM){
            U = dynamic_cast<SimpleContainer<page_t>*>(new RandomSet<page_t>());
        }
        else{
            std::cerr<<"Unknown eviction type" << std::endl;
        }
    };
    ~GenericAlgorithm(){
      delete U;
    };

    [[nodiscard]] evict_return_t consume(page_t page_start, bool from_partial_mt){
        evict_return_t ret = std::nullopt;
        if(!from_partial_mt){
            //Must be a page fault
            if(page_cache_full()){
                ret = evict();
            }
            auto evicted_from_consumption = consume_untracked(page_start);
            if(ret == std::nullopt) ret=evicted_from_consumption;
        }
        else{
            //Not necessarily a page fault
            if(U->contains(page_start)){
                ret = U->erase(page_start);
            }
            auto evicted_from_consumption = consume_tracked(page_start);
            if(ret == std::nullopt) ret=evicted_from_consumption;
        }
        return ret;
    }

    [[nodiscard]] virtual inline bool is_page_fault(page_t page) { //TODO rename to `is_page_fault` after refactor
        return !U->contains(page) && is_tracked_page_fault(page);
    };

    //TODO remove next two methods
    size_t get_total_size(){
        return tracked_size() + U->size();
    }
    size_t get_max_page_cache_size(){
        return max_page_cache_size;
    }

    virtual std::string name() = 0;
    virtual std::string toString() {return name() + " : cache = " +cache_to_string(10);};
    virtual std::unique_ptr<page_cache_copy_t> get_page_cache_copy() = 0;


    SimpleContainer<page_t>* U;
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
    size_t max_page_cache_size;


    [[nodiscard]] evict_return_t consume_untracked(page_t page_start) const{
        U->insert(page_start); //never removes from tracked caches // full case taken care of in generic `consume`
        return std::nullopt;
    }
    [[nodiscard]] virtual evict_return_t consume_tracked(page_t page_start) = 0;

    [[nodiscard]] virtual inline bool is_tracked_page_fault(page_t page) const  = 0;
    virtual size_t tracked_size() = 0;
    bool page_cache_full(){
        return get_total_size() == max_page_cache_size;
    }


    virtual evict_return_t evict(){
        auto ret = U->evict();
        if(ret == std::nullopt){ //U->evict returns false iff U->size() == 0 <=> must evict from list of "tracked" pages+
            ret = evict_from_tracked();
        }
        return ret;
    }
    [[nodiscard]] virtual evict_return_t evict_from_tracked() = 0;
};