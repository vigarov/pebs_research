cmake_minimum_required(VERSION 3.24)
project(c_rewrite)

set(CMAKE_CXX_STANDARD 20)

add_executable(c_rewrite main.cpp algorithms/GenericAlgorithm.h algorithms/LRU.cpp algorithms/LRU.h algorithms/CLOCK.cpp algorithms/CLOCK.h algorithms/ARC.cpp algorithms/ARC.h algorithms/CAR.cpp algorithms/CAR.h nlohmann/json.hpp)
