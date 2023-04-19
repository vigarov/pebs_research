cmake_minimum_required(VERSION 3.22.2)
project(c_rewrite)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -Wno-unused-function")

if(CMAKE_BUILD_TYPE MATCHES "Debug")
    add_definitions(-DBUILD_TYPE=0)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g")
elseif(CMAKE_BUILD_TYPE MATCHES "Release")
    add_definitions(-DBUILD_TYPE=1)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3")
endif(CMAKE_BUILD_TYPE MATCHES "Debug")
find_package(ZLIB)
find_package(Boost)
if(Boost_VERSION_MINOR GREATER_EQUAL 80)
    add_definitions(-DUSE_BOOST_1_80P=1)
endif(Boost_VERSION_MINOR GREATER_EQUAL 80)
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(c_rewrite main.cpp algorithms/GenericAlgorithm.h algorithms/LRU.cpp algorithms/LRU.h algorithms/CLOCK.cpp algorithms/CLOCK.h algorithms/ARC.cpp algorithms/ARC.h algorithms/CAR.cpp algorithms/CAR.h nlohmann/json.hpp tests/cprng.h tests/linux_crc16.h tests/test.cpp tests/test.h)

target_link_libraries(c_rewrite PRIVATE ZLIB::ZLIB Threads::Threads)
