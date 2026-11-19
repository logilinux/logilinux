cmake_minimum_required(VERSION 3.10)

add_executable(dialpad-example dialpad-example.cpp)
target_link_libraries(dialpad-example PRIVATE logilinux)

add_executable(volume-example volume-example.cpp)
target_link_libraries(volume-example PRIVATE logilinux)

add_executable(mx-keypad-example mx-keypad-example.cpp)
target_link_libraries(mx-keypad-example PRIVATE logilinux)

add_executable(tic-tac-toe tic-tac-toe.cpp)
target_link_libraries(tic-tac-toe PRIVATE logilinux)

add_executable(gif-test gif-test.cpp)
target_link_libraries(gif-test PRIVATE logilinux)

# Video playback example (requires ffmpeg libraries)
find_package(PkgConfig QUIET)
if(PkgConfig_FOUND)
    pkg_check_modules(FFMPEG QUIET libavcodec libavformat libavutil libswscale)
endif()

if(FFMPEG_FOUND)
    add_executable(video-test video-test.cpp)
    target_include_directories(video-test PRIVATE ${FFMPEG_INCLUDE_DIRS})
    target_link_libraries(video-test PRIVATE logilinux ${FFMPEG_LIBRARIES} jpeg)
    target_compile_options(video-test PRIVATE ${FFMPEG_CFLAGS_OTHER})
    message(STATUS "Building video-test example (ffmpeg found)")
else()
    message(STATUS "Skipping video-test example (ffmpeg not found)")
    message(STATUS "  Install with: sudo apt install libavcodec-dev libavformat-dev libavutil-dev libswscale-dev")
endif()


