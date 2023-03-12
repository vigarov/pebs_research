from GenericAlgorithm import *


class CLOCK(Algorithm):
    def __init__(self, page_cache_size, K):
        super().__init__(page_cache_size)
        self.reference_counter = {}
        self.page_cache = []
        self.head = 0
        self.K = K

    def consume(self, x: MemoryAddress):
        associated_page = Page(page_start_from_mem_address(x))
        if associated_page not in self.page_cache:
            # Cache miss
            if len(self.page_cache) == self.page_cache_size:
                # Cache full choose victim
                victim,self.head = self.find_victim(self.page_cache,self.head,self.reference_counter)
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
        temperature_list = []
        reference_counter = self.reference_counter.copy()
        page_cache = self.page_cache.copy()
        head = self.head
        while len(page_cache) != 0:
            victim,head = self.find_victim(page_cache, head, reference_counter)
            assert victim is not None and page_cache[head] == victim
            # head is positioned at victim
            del page_cache[head]
            temperature_list.append(victim)

        return temperature_list