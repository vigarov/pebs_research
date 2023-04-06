#ifndef C_REWRITE_CPRNG_H
#define C_REWRITE_CPRNG_H

//Credit to Jason Turner : https://www.youtube.com/watch?v=rpn_5Mrrxf8

#include <cstdint>
#include <limits>


constexpr auto seed()
{
    /*std::uint64_t shifted = 0;

    for( const auto c : __TIME__ )
    {
        shifted <<= 8;
        shifted |= c;
    }

    return shifted;*/
    return static_cast<std::uint64_t>(791625679);
}

template <typename result_type>
struct PCG
{
    struct pcg32_random_t { std::uint64_t state=0;  std::uint64_t inc=seed(); };
    pcg32_random_t rng;

    constexpr result_type operator()()
    {
        return pcg32_random_r();
    }

    static result_type constexpr min()
    {
        return std::numeric_limits<result_type>::min();
    }

    static result_type constexpr max()
    {
        return std::numeric_limits<result_type>::max();
    }

private:
    constexpr result_type pcg32_random_r()
    {
        std::uint64_t oldstate = rng.state;
        // Advance internal state
        rng.state = oldstate * 6364136223846793005ULL + (rng.inc|1);
        // Calculate output function (XSH RR), uses old state for max ILP
        result_type xorshifted = ((oldstate >> 18u) ^ oldstate) >> 27u;
        result_type rot = oldstate >> 59u;
        return (xorshifted >> rot) | (xorshifted << ((-rot) & 31));
    }

};

template <typename result_type>
constexpr auto get_random(int count)
{
    PCG<result_type> pcg;
    while(count > 0){
        pcg();
        --count;
    }

    return pcg();
}

//And https://stackoverflow.com/questions/45940284/array-initialisation-compile-time-constexpr-sequence


template <typename T = std::uint64_t>
constexpr T generate_ith_random_number(const std::size_t index) {
    static_assert(std::is_integral<T>::value, "T must to be an integral type");

    return get_random<T>(index);
}

template <std::size_t... Is>
constexpr auto make_sequence_impl(std::index_sequence<Is...>)
{
    return std::index_sequence<generate_ith_random_number(Is)...>{};
}

template <std::size_t N>
constexpr auto make_sequence()
{
    return make_sequence_impl(std::make_index_sequence<N>{});
}

template <std::size_t... Is>
constexpr auto make_array_from_sequence_impl(std::index_sequence<Is...>)
{
    return std::array{Is...};
}

template <typename Seq>
constexpr auto make_array_from_sequence(Seq)
{
    return make_array_from_sequence_impl(Seq{});
}

constexpr static uint64_t FUZZ_SIZE = 1'000'000;
#if CLIONDONT == 1


constexpr static auto random_addresses = make_array_from_sequence(make_sequence<FUZZ_SIZE>());


#endif


#endif //C_REWRITE_CPRNG_H
