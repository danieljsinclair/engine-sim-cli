# Test CMakeLists.txt for engine-sim-cli
# CLI smoke tests only (bridge tests are managed by bridge CMakeLists)

# -----------------------------------------------------------------------------
# Smoke tests
# CLI output is written to a log file, test names shown in terminal
# -----------------------------------------------------------------------------
add_executable(smoke_tests
    smoke/test_sine_mode.cpp
    smoke/test_default_engine.cpp
    smoke/test_audio_modes.cpp
    smoke/test_silent_flag.cpp
    smoke/test_bridge_smoke.cpp
    smoke/test_path_resolution.cpp
)

target_link_libraries(smoke_tests
    PRIVATE
        gtest_main
        gmock_main
)

target_include_directories(smoke_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/include
)

# Register CLI smoke tests - pipe CLI output to log file
add_test(NAME smoke_tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/smoke_tests
        --gtest_color=yes
        --gtest_output=json:${CMAKE_CURRENT_BINARY_DIR}/smoke_tests.json
)

set_tests_properties(smoke_tests PROPERTIES
    TIMEOUT 60
    LABELS "cli"
)
