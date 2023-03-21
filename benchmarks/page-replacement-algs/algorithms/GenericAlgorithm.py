from abc import ABC, abstractmethod

PAGE_SIZE = 4096  # 4K

def page_start_from_mem_address(x):
    # https://stackoverflow.com/questions/6387771/get-starting-address-of-a-memory-page-in-linux
    return x & ~(PAGE_SIZE - 1)


class Algorithm(ABC):

    def __init__(self, page_cache_size):
        super().__init__()
        self.page_cache_size = page_cache_size

    @abstractmethod
    def consume(self, page: int):
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
    def is_page_fault(self, page: int):
        raise NotImplementedError

    @abstractmethod
    def name(self):
        raise NotImplementedError
