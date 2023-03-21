from algorithms.GenericAlgorithm import *


class CLOCK(Algorithm):
    def __init__(self, page_cache_size, K):
        super().__init__(page_cache_size)
        self.reference_counter = {}
        self.page_cache = []
        self.head = 0
        self.K = K

    def consume(self, associated_page: int):
        if associated_page not in self.page_cache:
            # Cache miss
            if len(self.page_cache) == self.page_cache_size:
                # Cache full choose victim
                victim, self.head = self.find_victim(self.page_cache, self.head, self.reference_counter)
                assert victim is not None and self.page_cache[self.head] == victim
                # head is positioned at victim
                self.page_cache[self.head] = associated_page
            else:
                # Add accessed page as MRU
                self.page_cache.append(associated_page)
        self.__reset_counter(associated_page)

    def __reset_counter(self, page):
        self.reference_counter[page] = self.K

    def find_victim(self, page_cache, head, reference_counter):
        victim = None
        """
            Start at head = idx 0, traverse to tail, pick the first who's ref bit is unset
        """
        while victim is None:
            page = page_cache[head]  # take head
            if reference_counter[page] == 0:
                victim = page
            else:
                reference_counter[page] -= 1
                head = (head + 1) % len(page_cache)
        return victim, head

    def get_temperature_list(self):
        cache_head_displaced = self.page_cache[self.head:] + self.page_cache[:self.head]
        tl = []
        # Starting at head, order is cache^0 -> cache^1 -> ... -> cache^K
        for i in range(self.K + 1):
            tl += [page for page in cache_head_displaced if self.reference_counter[page] == i]
        return tl

    def is_page_fault(self, page: int):
        return page not in self.page_cache

    def __str__(self):
        return "CLOCK: pages in cache (showing bases): [" + ",".join(
            [hex(page_base) for page_base in self.page_cache]) + ']'

    def name(self):
        return "GCLOCK"
