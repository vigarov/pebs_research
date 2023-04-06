cmake_minimum_required(VERSION 3.24)
project(c_rewrite)

set(CMAKE_CXX_STANDARD 20)

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DBUILD_TYPE=0)
elseif(CMAKE_BUILD_TYPE MATCHES "Release")
    add_definitions(-DBUILD_TYPE=1)
endif(CMAKE_BUILD_TYPE MATCHES "Debug")
find_package(ZLIB)
find_package(Boost)

add_executable(c_rewrite main.cpp algorithms/GenericAlgorithm.h algorithms/LRU.cpp algorithms/LRU.h algorithms/CLOCK.cpp algorithms/CLOCK.h algorithms/ARC.cpp algorithms/ARC.h algorithms/CAR.cpp algorithms/CAR.h nlohmann/json.hpp tests/cprng.h tests/linux_crc16.h tests/test.cpp tests/test.h)

target_link_libraries(c_rewrite ZLIB::ZLIB)
