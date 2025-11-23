cmake_minimum_required(VERSION 3.10)

# Library source files
set(LOGILINUX_SOURCES
    src/core/library.cpp
    src/core/device_manager.cpp
    src/core/input_monitor.cpp
    src/devices/dialpad_device.cpp
    src/devices/mx_keypad_device.cpp
    src/util/gif_decoder.cpp
)

# Create shared library
add_library(logilinux SHARED ${LOGILINUX_SOURCES})

# Set C++17 standard
target_compile_features(logilinux PUBLIC cxx_std_17)

# Include directories
target_include_directories(logilinux
    PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
    PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link pthread for threading support
find_package(JPEG)

set(EXTRA_LIBS pthread)

# Try to find giflib using find_library (some distros don't have pkg-config for it)
find_library(GIF_LIBRARY NAMES gif)
find_path(GIF_INCLUDE_DIR NAMES gif_lib.h)

if(GIF_LIBRARY AND GIF_INCLUDE_DIR AND JPEG_FOUND)
    list(APPEND EXTRA_LIBS ${GIF_LIBRARY} ${JPEG_LIBRARIES})
    target_include_directories(logilinux PRIVATE ${GIF_INCLUDE_DIR} ${JPEG_INCLUDE_DIR})
    target_compile_definitions(logilinux PRIVATE HAVE_GIFLIB)
    message(STATUS "GIF support enabled (giflib: ${GIF_LIBRARY}, libjpeg: ${JPEG_LIBRARIES})")
else()
    if(NOT GIF_LIBRARY OR NOT GIF_INCLUDE_DIR)
        message(WARNING "giflib not found - GIF support will be disabled")
    endif()
    if(NOT JPEG_FOUND)
        message(WARNING "libjpeg not found - GIF support will be disabled")
    endif()
endif()

target_link_libraries(logilinux PRIVATE ${EXTRA_LIBS})

# Set library version
set_target_properties(logilinux PROPERTIES
    VERSION 0.1.0
    SOVERSION 0
    OUTPUT_NAME "logilinux"
)

# Installation rules
install(TARGETS logilinux
    LIBRARY DESTINATION lib
    ARCHIVE DESTINATION lib
    RUNTIME DESTINATION bin
)

# Install headers
install(DIRECTORY include/logilinux
    DESTINATION include
    FILES_MATCHING PATTERN "*.h"
)

# Generate pkg-config file
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/logilinux.pc.in
    ${CMAKE_CURRENT_BINARY_DIR}/logilinux.pc
    @ONLY
)

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/logilinux.pc
    DESTINATION lib/pkgconfig
)
