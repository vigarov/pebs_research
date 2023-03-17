from algorithms.GenericAlgorithm import *

T1, T2, B1, B2 = tuple(range(4))


class CAR(Algorithm):
    def __init__(self, page_cache_size):
        super().__init__(page_cache_size)
        self.p = 0
        self.lists = [[], [], [], []]  # t1,t2,b1,b2
        self.reference_bits = {}

    def consume(self, associated_page: Page):
        in_li = [(associated_page in tb_i) for tb_i in self.lists]
        if in_li[T1] or in_li[T2]:
            # Cache hit
            self.reference_bits[associated_page] = True
        else:
            # Cache miss
            if len(self.lists[T1]) + len(self.lists[T2]) == self.page_cache_size:
                # cache full
                self.__replace()
                # History replacement
                if not in_li[B1] and not in_li[B2]:
                    if len(self.lists[T1]) + len(self.lists[B1]) == self.page_cache_size:
                        # Discard LRU in B1
                        del self.lists[B1][len(self.lists[B1]) - 1]
                    elif len(self.lists[T1]) + len(self.lists[T2]) + len(self.lists[B1]) + len(
                            self.lists[B2]) == 2 * self.page_cache_size:
                        # Discard LRU in B2
                        del self.lists[B2][len(self.lists[B2]) - 1]
            in_li = [(associated_page in tb_i) for tb_i in self.lists]  # update `in` check as we might've modified B_i
            if not in_li[B1] and not in_li[B2]:
                # history miss
                self.lists[T1].append(associated_page)
                self.reference_bits[associated_page] = 0
            else:
                # History hit
                # Adapt
                if in_li[B1]:
                    self.p = min(self.p + max(1., len(self.lists[B2]) // len(self.lists[B1])), self.page_cache_size)
                else:
                    self.p = max(self.p - max(1., len(self.lists[B1]) // len(self.lists[B2])), 0)
                # Move page to tail of T2
                self.lists[T2].append(associated_page)
                self.reference_bits[associated_page] = 0

    def __replace(self):
        found = False
        while not found:
            if len(self.lists[T1]) >= max(1, self.p):
                # T1 is oversized
                t1_head = self.lists[T1].pop(0)
                if not self.reference_bits[t1_head]:
                    found = True
                    self.lists[B1].insert(0, t1_head)
                else:
                    self.reference_bits[t1_head] = False
                    self.lists[T2].append(t1_head)
            else:
                # T2 is oversized
                t2_head = self.lists[T2].pop(0)
                if not self.reference_bits[t2_head]:
                    found = True
                    self.lists[B2].insert(0, t2_head)
                else:
                    self.reference_bits[t2_head] = False
                    self.lists[T2].append(t2_head)

    def get_temperature_list(self):
        # The order is T_1^0,T_2^0,T_1^1,T2^1
        t1, t2 = self.lists[T1].copy(), self.lists[T2].copy()
        return [page for page in t1 if not self.reference_bits[page]] + \
            [page for page in t2 if not self.reference_bits[page]] + \
            [page for page in t1 if self.reference_bits[page]] + \
            [page for page in t2 if self.reference_bits[page]]

    def is_page_fault(self, page: Page):
        return page not in self.lists[T1] and page not in self.lists[T2]

    def name(self):
        return "CAR"

    def __str__(self):
        return "CAR: pages in cache (T1 U T2) (showing bases): [" + ",".join(
            [str(page) for page in self.lists[T1] + self.lists[T2]]) + ']'
