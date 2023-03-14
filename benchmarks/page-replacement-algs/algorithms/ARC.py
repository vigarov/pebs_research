from algorithms.GenericAlgorithm import *

T1, T2, B1, B2 = tuple(range(4))


class ARC(Algorithm):
    def __init__(self, page_cache_size):
        super().__init__(page_cache_size)
        self.p = 0
        self.lists = [[], [], [], []]  # t1,t2,b1,b2; 0=MRU, len=LRU

    def consume(self, associated_page: Page):
        in_li = [(associated_page in tb_i) for tb_i in self.lists]
        if in_li[T1] or in_li[T2]:
            assert not in_li[B1] and not in_li[B2]  # Sanity check
            # Cache hit
            # Move to MRU of T2
            if in_li[T1]:
                self.lists[T1].remove(associated_page)
                assert associated_page not in in_li[T2]  # Sanity check
            elif associated_page in in_li[T2]:
                self.lists[T2].remove(associated_page)
            self.lists[T2].insert(0, associated_page)
        elif in_li[B1]:
            assert not in_li[B2]  # Sanity check
            # Adapt p
            delta_1 = 1 if len(self.lists[B1]) >= len(self.lists[B2]) else len(self.lists[B2]) // len(self.lists[B1])
            self.p = min(self.p + delta_1, self.page_cache_size)
            # Replace
            self.__replace(associated_page)
            # Move from B1 to MRU in T2
            self.lists[B1].remove(associated_page)
            self.lists[T2].insert(0, associated_page)
        elif in_li[B2]:
            # Adapt p
            delta_2 = 1 if len(self.lists[B2]) >= len(self.lists[B1]) else len(self.lists[B1]) // len(self.lists[B2])
            self.p = min(self.p - delta_2, self.page_cache_size)
            # Replace
            self.__replace(associated_page)
            # Move from B2 to MRU in T2
            self.lists[B2].remove(associated_page)
            self.lists[T2].insert(0, associated_page)
        else:
            if len(self.lists[T1]) + len(self.lists[B1]) == self.page_cache_size:
                if len(self.lists[T1]) < self.page_cache_size:
                    self.lists[B1].pop()  # Remove B1 LRU
                    self.__replace(associated_page)
                else:
                    # B1 is empty
                    self.lists[T1].pop()  # Delete LRU in T1
            else:
                assert len(self.lists[T1]) + len(self.lists[B1]) < self.page_cache_size
                total_len = len(self.lists[T1]) + len(self.lists[B1]) + len(self.lists[T2]) + len(self.lists[B2])
                if total_len >= self.page_cache_size:
                    if total_len == 2 * self.page_cache_size:
                        self.lists[B2].pop()  # Remove B2 LRU
                    self.__replace(associated_page)
            # Put in MRU of T1
            self.lists[T1].insert(0, associated_page)

    def __replace(self, page):
        if len(self.lists[T1]) != 0 and (
                len(self.lists[T1]) > self.p or (page in self.lists[B2] and len(self.lists[T1]) == self.p)):
            # Move LRU in T1 to MRU in B1
            self.lists[B1].insert(0, self.lists[T1].pop())
        else:
            # Move LRU in T2 to MRU in B1
            self.lists[B2].insert(0, self.lists[T2].pop())

    def __str__(self):
        return "ARC: pages in cache (T1 U T2) (showing bases): [" + ",".join(
            [str(page) for page in self.lists[T1] + self.lists[T2]]) + ']'

    def get_temperature_list(self):
        # By order of eviction, T1's pages will leave first, then T2 --> temperature is very simply T1 U T2
        # (in order, LRU is last = coldest )
        return self.lists[T1][::-1] + self.lists[T2][::-1]

    def is_page_fault(self, page: Page):
        return page not in self.lists[T1] and page not in self.lists[T2]

    def name(self):
        return "ARC"
