# Integration-level audio tests
# Tests at Simulator interface level using SineWaveSimulator
# TDD: Tests real audio bugs at the right abstraction level
# Phase F: Strategy/buffer/hardware sources now come from engine-sim-bridge library

include(GoogleTest)

# Mock objects for DI injection
set(MOCK_SOURCES
    ../mocks/MockDataSimulator.cpp
    ../mocks/MockAudioUnit.cpp
)

# Integration test executable
add_executable(integration_tests
    SimulatorLevelAudioTest.cpp
    ${MOCK_SOURCES}
    # Required support code
    ../../src/config/ANSIColors.cpp
)

target_link_libraries(integration_tests
    PRIVATE
        gtest
        gtest_main
        engine-sim-bridge
        ${AUDIO_LIBRARY}
)

target_include_directories(integration_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/test/unit
        ${CMAKE_SOURCE_DIR}/test/mocks
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/include
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/dependencies/dr_libs
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/include
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/scripting/include
)

# Set rpath for test executable to find dylibs in bridge build directory
if(APPLE)
    set_target_properties(integration_tests PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "${CMAKE_SOURCE_DIR}/engine-sim-bridge/build"
        BUILD_RPATH "${CMAKE_SOURCE_DIR}/engine-sim-bridge/build"
    )
endif()

# Register integration tests per test case so CTest can parallelize safely.
# Avoid fixed gtest JSON output here to prevent artifact collisions between
# concurrently running test cases.
gtest_discover_tests(integration_tests
    DISCOVERY_TIMEOUT 30
    EXTRA_ARGS
        --gtest_color=yes
    PROPERTIES
        TIMEOUT 60
        LABELS "integration"
)
