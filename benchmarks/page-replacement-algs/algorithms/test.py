import timeit

from ARC import ARC
from CAR import CAR
from CLOCK import CLOCK
from LRU_K import LRU_K
from GenericAlgorithm import *


class I_CLOCK:
    def __init__(self):
        pass

    def findAndUpdate(self, x, arr, second_chance, frames):

        for i in range(frames):

            if arr[i] == x:
                # Mark that the page deserves a second chance
                second_chance[i] = True

                # Return 'true', that is there was a hit
                # and so there's no need to replace any page
                return True

        # Return 'false' so that a page
        # for replacement is selected
        # as he reuested page doesn't
        # exist in memory
        return False

    # Updates the page in memory
    # and returns the pointer
    def replaceAndUpdate(self, x, arr, second_chance, frames, pointer):
        while True:

            # We found the page to replace
            if not second_chance[pointer]:
                # Replace with new page
                arr[pointer] = x

                # Return updated pointer
                return (pointer + 1) % frames

            # Mark it 'false' as it got one chance
            # and will be replaced next time unless accessed again
            second_chance[pointer] = False

            # Pointer is updated in round robin manner
            pointer = (pointer + 1) % frames


def overall_manhattan_distance(tl1, baseline_tl, punish=False):
    sum_ = 0
    page_sorted_tl1 = sorted(list(enumerate(tl1)), key=lambda tpl: tpl[1])
    page_sorted_tl2 = sorted(list(enumerate(baseline_tl)), key=lambda tpl: tpl[1])

    def bsearch(page_start, slist):
        low = 0
        high = len(slist) - 1
        mid = 0
        while low <= high:
            mid = (high + low) // 2
            if slist[mid][1] < page_start:
                low = mid + 1
            elif slist[mid][1] > page_start:
                high = mid - 1
            else:
                return mid
        return -1

    for hotness, page in page_sorted_tl2:
        idx_in_tl1 = bsearch(page, page_sorted_tl1)
        if idx_in_tl1 != -1:
            sum_ += abs(hotness - page_sorted_tl2[idx_in_tl1][0])
        elif punish:
            sum_ += hotness
    return sum_


if __name__ == "__main__":
    K = 2
    page_cache_size = 3  # ~ 128 KB mem
    my_algs = [LRU_K(page_cache_size, K, 0, False), CLOCK(page_cache_size, K), ARC(page_cache_size),
               CAR(page_cache_size)]

    test_accesses = ['W0x7fffffffd9a8', 'W0x7fffffffd980', 'W0x7ffff7ffde0e', 'W0x7ffff7ffdb78',
                     'R0x7ffff7ffcf80', 'W0x7ffff7ffdcc0', 'R0x7ffff7ffde0e', 'R0x7ffff7ffdb50', 'R0x7ffff7ffce98']

    ta_1 = ["W0x7fffffffd9a8", "W0x7fffffffd980", "W0x7ffff7ffde0e", "W0x7ffff7ffdb78",
            "R0x7ffff7ffcf80", "W0x7ffff7ffdcc0", "R0x7ffff7ffce98"]

    ta2 = ['R0x7ffff7ffc418', 'R0x7fffffffe064', 'R0x7ffff7ffc480', 'R0x7ffff7ffc488', 'R0x7ffff7ffc4f0',
           'R0x7ffff7ffc4f8', 'R0x7ffff7ffc560', 'W0x7fffffffd9a8', 'W0x7fffffffd980', 'W0x7ffff7ffde0e',
           'W0x7ffff7ffdb78', 'R0x7ffff7ffcf80', 'W0x7ffff7ffdcc0', 'R0x7ffff7ffde0e', 'R0x7ffff7ffdb50',
           'R0x7ffff7ffce98', 'R0x7ffff7ffc568', 'R0x7ffff7ffc5d0', 'R0x7ffff7ffc5d8',
           'R0x7ffff7ffc640', 'R0x7ffff7ffc648', 'R0x7ffff7ffc6b0', 'R0x7ffff7ffc6b8', 'R0x7fffffffe064',
           'R0x7ffff7ffc720', 'R0x7ffff7ffc728', 'R0x7fffffffe064', 'R0x7ffff7ffc790', 'R0x7ffff7ffc798',
           'R0x7ffff7ffc800', 'R0x7ffff7ffc808', 'R0x7ffff7ffc870', 'R0x7ffff7ffc878', 'R0x7fffffffe064',
           'R0x7ffff7ffc8e0', 'R0x7ffff7ffc8e8', 'R0x7ffff7ffc950', 'R0x7ffff7ffc958', 'R0x7ffff7ffc9c0',
           'R0x7ffff7ffc9c8', 'R0x7ffff7ffca30', 'R0x7ffff7ffca38', 'R0x7fffffffe064', 'R0x7fffffffd7b0',
           'R0x7fffffffdab8', 'R0x7fffffffe07c', 'W0x7fffffffd7b0', 'R0x7fffffffe07d', 'R0x7fffffffe07e',
           'R0x7fffffffe07f', 'R0x7fffffffe080', 'R0x7fffffffe081', 'R0x7fffffffe082', 'R0x7fffffffe083',
           'R0x7fffffffe084', 'R0x7fffffffe085', 'R0x7fffffffe086', 'R0x7fffffffe087', 'W0x7fffffffd7b8',
           'R0x7fffffffe07c', 'R0x7ffff7ff1279', 'R0x7fffffffe07d', 'R0x7ffff7ffca98']

    for alg in my_algs:
        pfaults = 0
        for ma in ta2 :
            mem_address = int(ma[1:], 16)
            page_start = page_start_from_mem_address(mem_address)
            if alg.is_page_fault(page_start):
                pfaults += 1
            alg.consume(page_start)
        tl = alg.get_temperature_list()
        print([hex(e) for e in tl], f"Pfaults: {pfaults}")

    # def gtl(algorithm):
    #     temperature_list = []
    #     reference_counter = algorithm.reference_counter.copy()
    #     page_cache = algorithm.page_cache.copy()
    #     head = algorithm.head
    #     while len(page_cache) != 0:
    #         head %= len(page_cache)
    #         victim, head = algorithm.find_victim(page_cache, head, reference_counter)
    #         assert victim is not None and page_cache[head] == victim
    #         # head is positioned at victim
    #         del page_cache[head]
    #         temperature_list.append(victim)
    #     return temperature_list
    #
    # tempC = CLOCK(page_cache_size, K)
    # tempC.consume(int('0x7ffff7ffc418',16))
    # tempC.consume(int('0x7ffff6ffe418',16))
    # tempC.consume(int('0x7fffffffe064',16))
    # tempC.consume(int('0x7fffffffd7b0',16))
    # for ma in ta2:
    #     mem_address = int(ma[1:], 16)
    #     page_start = page_start_from_mem_address(mem_address)
    #     tempC.consume(page_start)
    #     tl = tempC.get_temperature_list()
    #     cache_head_displaced = tempC.page_cache[tempC.head:] + tempC.page_cache[:tempC.head]
    #     new_technique = gtl(tempC)
    #     for p,cp in zip(tl,new_technique):
    #         assert p == cp
    #     t1 = timeit.Timer(lambda: tempC.get_temperature_list())
    #     t2 = timeit.Timer(lambda: gtl(tempC))
    #     print(t1.timeit(1),",",t2.timeit(1))
    #     print(t1.timeit(10),",",t2.timeit(10))

    MAX_PAGE_CACHE_SIZE = 507569  # pages = `$ ulimit -l`/4  ~= 2GB mem
    page_cache_size = MAX_PAGE_CACHE_SIZE // 128

    my_algs2 = [LRU_K(page_cache_size, K, 0, False), CLOCK(page_cache_size, K), ARC(page_cache_size),
                CAR(page_cache_size)]
    pfaults_arr = [0] * 4
    with open('/home/vigarov/research/benchmarks/results/pmbench/mem_trace_07-02-2023_17-36-07.log', 'r') as file:
        count = 5_000_000
        while (line := file.readline()) != '' and count > 0:
            if count % 1_000_000 == 0:
                print(f"Reached count={count}")
            for idx, alg in enumerate(my_algs2):
                mem_address = int(line[1:], 16)
                page_base = page_start_from_mem_address(mem_address)
                if alg.is_page_fault(page_base):
                    pfaults_arr[idx] += 1
                alg.consume(page_base)
            count -= 1

    for idx, alg in enumerate(my_algs2):
        tl = alg.get_temperature_list()

        print(alg.name())
        print(f"Pfaults: {pfaults_arr[idx]}",'TL[:30]: ['+','.join([hex(pb) for pb in tl[:10]])+']')
        print(str(alg)[:10*len("0x7fffd48f6000,")])
        if idx == 0:
            lru_k_tl = tl.copy()
        else:
            print(f"Distance with LRU_2 = {overall_manhattan_distance(tl, lru_k_tl)}")
        print('-' * 200)
