from abc import ABC, abstractmethod


class MemoryAddress:
    def __init__(self, x):
        try:
            self.address = int(x, 16)
        except ValueError as e:
            print(e, "Couldn't create Memory Address with x=", x)
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
        return self.base <= mem_address.address < self.base + self.__size

    def __eq__(self, other):
        return self.base == other.base

    def __str__(self):
        return str(self.base)


def page_start_from_mem_address(x):
    # https://stackoverflow.com/questions/6387771/get-starting-address-of-a-memory-page-in-linux
    assert isinstance(x, MemoryAddress)
    return x.address & ~(PAGE_SIZE - 1)


class Algorithm(ABC):

    def __init__(self, page_cache_size):
        super().__init__()
        self.page_cache_size = page_cache_size

    @abstractmethod
    def consume(self, page: Page):
        """
        Method used for the algorithm to update itself whenever a memory access is issued
        :param page: the page the accessed memory address is in
        :return: nothing
        """
        raise NotImplementedError

    @abstractmethod
    def __str__(self):
        raise NotImplementedError

    @abstractmethod
    def get_temperature_list(self):
        raise NotImplementedError

    @abstractmethod
    def is_page_fault(self, page: Page):
        raise NotImplementedError

    @abstractmethod
    def name(self):
        raise NotImplementedError
