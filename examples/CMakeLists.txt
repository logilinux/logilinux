cmake_minimum_required(VERSION 3.10)

add_executable(dialpad-example dialpad-example.cpp)
target_link_libraries(dialpad-example PRIVATE logilinux)

add_executable(volume-example volume-example.cpp)
target_link_libraries(volume-example PRIVATE logilinux)

add_executable(mx-keypad-example mx-keypad-example.cpp)
target_link_libraries(mx-keypad-example PRIVATE logilinux)

add_executable(gif-test gif-test.cpp)
target_link_libraries(gif-test PRIVATE logilinux)

