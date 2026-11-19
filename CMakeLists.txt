cmake_minimum_required(VERSION 3.10)
project(logilinux VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Threads REQUIRED)

add_subdirectory(lib)

add_subdirectory(examples)

add_subdirectory(tools)
