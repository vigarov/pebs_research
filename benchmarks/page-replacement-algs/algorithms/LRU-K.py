from GenericAlgorithm import *


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

    def __init__(self, K, page_cache_size):
        super().__init__(page_cache_size)
        assert K >= 1 and page_cache_size > 0
        self.page_cache = set()
        self.histories = {}
        self.K = K

    def __cache_full(self):
        return len(self.page_cache) == super().page_cache_size

    def consume(self, x: MemoryAddress, count_stamp: int):
        associated_page = Page(page_start_from_mem_address(x))
        hist_p = self.histories[associated_page]
        if hist_p is None:
            hist_p = [count_stamp] + [0] * (self.K - 1)
        if self.__cache_full():
            if associated_page in self.page_cache:
                # Always have correlated references, see comment in class doc
                for i in range(len(hist_p)):
                    hist_p[i] = hist_p[i - 1]
                hist_p[0] = count_stamp
            else:  # Select replacement victim
                min_stamp = count_stamp
                victim = None
                for q in self.page_cache:
                    hist_q = self.histories[q]
                    max_bw_k_dist = hist_q[-1]
                    if max_bw_k_dist < min_stamp:
                        victim = q
                        min_stamp = max_bw_k_dist
                self.page_cache.remove(victim)
        # Add current page to page cache ; always done if page cache hasn't been filled yet
        self.page_cache.add(associated_page)
        self.histories[associated_page] = hist_p  # propagate history changes

    def __str__(self):
        return "LRU_K: pages in cache (showing bases): [" + ",".join(
            [str(MemoryAddress(page.base)) for page in self.page_cache]) + ']'
