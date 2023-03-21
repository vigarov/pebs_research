from algorithms.GenericAlgorithm import *


class LRU_K(Algorithm):
    """
    Implementation of the LRU-K algorithm.

    Since, for our research, the benchmarks run for a relatively small amount
    of time, the time component of LRU-K is not suitable: when searching for a victim to evict (in a full cache),
    none of the pages would have satisfied `t-LAST(q) > Correlated_Reference_Period` if this value were to be set to
    high. We thus assume any page is suitable for replacement by setting Correlated_Reference_Period to 0,
    and do not implement a retained information daemon --> always have correlated reference, and LAST(p) == HIST(p,1)

    Since our ground truth traces do not hold timestamps (and even if they would, the latter would not be relevant as
    PIN adds magnitudes of overhead ot execution time) we redefine HIST(p,i) = x <=> page p has been accessed i^th last
    at the x^th memory access. Thus, time locality is replaced by execution locality.

    """

    def __init__(self, page_cache_size, K, CRP, infinite_history):
        super().__init__(page_cache_size)
        assert K >= 1 and page_cache_size > 0
        self.page_cache = []
        self.histories = {}
        self.K = K
        self.CRP = CRP
        self.infinite_history = infinite_history
        self.count_stamp = 0

    def __cache_full(self):
        return len(self.page_cache) == self.page_cache_size

    def consume(self, associated_page: int):
        hist_p = self.histories.get(associated_page)
        self.count_stamp += 1
        if hist_p is None:
            hist_p = [self.count_stamp] + [0] * (self.K - 1)

        if associated_page in self.page_cache:
            # Always have correlated references, see comment in class doc
            for i in range(1,self.K):
                hist_p[i] = hist_p[i - 1]
            hist_p[0] = self.count_stamp
        else:
            if self.__cache_full():
                # Select replacement victim
                victim = self.find_victim(self.page_cache)
                assert victim is not None
                # Remove from cache
                self.page_cache.remove(victim)
                # And remove from history if set up
                if not self.infinite_history:
                    del self.histories[victim]
            # (also with the else:)
            # Add current page to page cache ; always done if page cache hasn't been filled yet
            self.page_cache.append(associated_page)
        self.histories[associated_page] = hist_p  # propagate history changes

    def find_victim(self, cache):
        min_stamp = self.count_stamp
        victim = None
        for q in cache:
            hist_q = self.histories[q]
            max_bw_k_dist = hist_q[-1]
            if max_bw_k_dist < min_stamp:
                victim = q
                min_stamp = max_bw_k_dist
        return victim

    def __str__(self):
        return "LRU_K: pages in cache (showing bases): [" + ",".join(
            [hex(page_base) for page_base in self.page_cache]) + ']'

    def get_temperature_list(self):
        temperature_list = []
        cache_copy = self.page_cache.copy()
        while len(cache_copy) != 0:
            victim = self.find_victim(cache_copy)
            temperature_list.append(victim)
            cache_copy.remove(victim)  # Ok to do so, since cache should be a set, aka a page is present only once
        return temperature_list

    def is_page_fault(self, page: int):
        return page not in self.page_cache

    def name(self):
        return "LRU_"+str(self.K)
