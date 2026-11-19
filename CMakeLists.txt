cmake_minimum_required(VERSION 3.10)
project(logilinux VERSION 0.1.0)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    # warning level 4
    add_compile_options(/W4)
else()
    # additional warnings
    add_compile_options(-Wall -Wextra -Wpedantic)
endif()

find_package(Threads REQUIRED)

add_subdirectory(lib)

add_subdirectory(examples)

add_subdirectory(tools)
