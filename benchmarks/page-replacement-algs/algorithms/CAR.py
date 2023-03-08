from GenericAlgorithm import *

T1, T2, B1, B2 = tuple(range(4))


class CAR(Algorithm):
    def __init__(self, page_cache_size):
        super().__init__(page_cache_size)
        self.p = 0
        self.q = 0
        self.ns = 0
        self.nl = 0
        self.lists = [[], [], [], []]  # t1,t2,b1,b2
        self.reference_bits = {}
        self.filter_bits = {}

    def consume(self, x: MemoryAddress, count_stamp: int):
        associated_page = Page(page_start_from_mem_address(x))
        in_li = [(associated_page in tb_i) for tb_i in self.lists]
        if in_li[T1] or in_li[T2]:
            # Cache hit
            self.reference_bits[associated_page] = 1
        else:
            # Cache miss
            if len(self.lists[T1]) + len(self.lists[T2]) == self._page_cache_size:
                # cache full
                self.__replace()
                # History replacement
                if (not in_li[B1] and not in_li[B2]) and (
                        len(self.lists[B1]) + len(self.lists[B2]) == self._page_cache_size + 1):
                    if (len(self.lists[B1]) > max(0, self.q)) or len(self.lists[B2]) == 0:
                        # Remove B1 LRU
                        self.lists[B1].pop()
                    else:
                        # Remove B2 LRU
                        self.lists[B2].pop()
            if not in_li[B1] and not in_li[B2]:
                # history miss
                self.lists[T1].append(associated_page)
                self.reference_bits[associated_page] = 0
                self.filter_bits[associated_page] = 'S'
                self.ns += 1
            else:
                # History hit
                # Adapt
                if in_li[B1]:
                    self.p = min(self.p + max(1., self.ns / len(self.lists[B1])), self._page_cache_size)
                else:
                    self.p = max(self.p - max(1., self.nl / len(self.lists[B2])), 0)
                # Move page to tail of t1
                self.lists[T1].append(associated_page)
                self.reference_bits[associated_page] = 0
                self.filter_bits[associated_page] = 'L'
                self.nl += 1
                if not in_li[B1] and (len(self.lists[T1]) + len(self.lists[T2]) + len(
                        self.lists[B2]) - self.ns >= self._page_cache_size):
                    self.q = min(self.q + 1, 2 * self._page_cache_size - len(self.lists[T1]))

    def __replace(self):
        while len(self.lists[T2]) != 0:
            t2_head = self.lists[T2][0]
            if self.reference_bits[t2_head] != 1:
                break
            # Move T2 head to T1 tail
            self.lists[T1].append(self.lists[T2].pop(0))
            self.reference_bits[t2_head] = 0
            if len(self.lists[T1]) + len(self.lists[T2]) + len(self.lists[B2]) - self.ns >= self._page_cache_size:
                self.q = min(self.q + 1, 2 * self._page_cache_size - len(self.lists[T1]))
        while len(self.lists[T1]) != 0:
            t1_head = self.lists[T1][0]
            if not (self.filter_bits[t1_head] == 'L' or self.reference_bits[t1_head] == 1):
                break
            if self.reference_bits[t1_head] == 1:
                # Move head of T1 to tail of T1
                self.lists[T1].append(self.lists[T1].pop(0))
                self.reference_bits[t1_head] = 0
                if len(self.lists[T1]) >= min(self.p+1,len(self.lists[B1])) and self.filter_bits[t1_head] == 'S':
                    self.filter_bits[t1_head] = 'L'
                    self.ns -= 1
                    self.nl += 1
            else:
                # Move head of T1 to tail of T2
                self.lists[T2].append(self.lists[T1].pop(0))
                self.reference_bits[t1_head] = 0
                self.q = max(self.q-1, self._page_cache_size-len(self.lists[T1]))
        if len(self.lists[T1]) >= max(1,self.p):
            # Head T1 -> Head B1
            self.lists[B1].insert(0,self.lists[T1].pop(0))
            self.ns -=1
        else:
            # Head T2 -> Head B2
            self.lists[B2].insert(0,self.lists[T2].pop(0))
            self.nl -=1