add_executable(mx-keypad-monitor-restart-test
    mx_keypad_monitor_restart_test.cpp
)

target_include_directories(mx-keypad-monitor-restart-test PRIVATE
    ${CMAKE_SOURCE_DIR}/lib/src
)

target_link_libraries(mx-keypad-monitor-restart-test PRIVATE logilinux)

add_test(
    NAME mx-keypad-monitor-restart
    COMMAND mx-keypad-monitor-restart-test
)
