from abc import ABC, abstractmethod


class MemoryAddress:
    def __init__(self, x):
        try:
            self.address = int(x)
        except ValueError:
            self.address = int(x, 16)
        else:
            print("error parsing mem adress", x)

    def __str__(self):
        return hex(self.address)

    def __int__(self):
        return self.address



PAGE_SIZE = 4096  # 4K


class Page:
    def __init__(self, base, size=PAGE_SIZE):
        self.__size = size
        self.base = base

    def __contains__(self, item):
        mem_address = MemoryAddress(item)
        return self.base < mem_address.address < self.base + self.__size

    def __eq__(self, other):
        return self.base == other.base


def page_start_from_mem_address(x):
    # https://stackoverflow.com/questions/6387771/get-starting-address-of-a-memory-page-in-linux
    assert isinstance(x,MemoryAddress)
    return x.address & ~(PAGE_SIZE-1)

class Algorithm(ABC):

    def __init__(self, page_cache_size):
        super().__init__()
        self.page_cache_size = page_cache_size

    @abstractmethod
    def consume(self, x: MemoryAddress, count_stamp:int):
        """
        Method used for the algorithm to update itself whenever a memory access is issued
        :param count_stamp: the count at which the access occurs - equivalent for timestamp
        :param x: the memory address
        :return: nothing
        """
        raise NotImplementedError

    @abstractmethod
    def __str__(self):
        raise NotImplementedError

    @abstractmethod
    def get_temperature_list(self):
        raise NotImplementedError

