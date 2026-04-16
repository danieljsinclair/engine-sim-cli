# Telemetry ISP tests
# Tests the per-component telemetry write/read interfaces

add_executable(telemetry_isp_tests
    test_telemetry_isp.cpp
)

target_link_libraries(telemetry_isp_tests
    PRIVATE
        gtest
        gtest_main
        engine-sim-bridge
        Threads::Threads
)

target_include_directories(telemetry_isp_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/include
)

add_test(NAME telemetry_isp_tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/telemetry_isp_tests
        --gtest_color=yes
)

set_tests_properties(telemetry_isp_tests PROPERTIES
    TIMEOUT 30
    LABELS "telemetry"
)
