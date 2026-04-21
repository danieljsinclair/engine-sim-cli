# Unit-level deterministic audio tests
# TDD approach: RED -> GREEN -> REFACTOR
# Phase F: Strategy/buffer/hardware sources now come from engine-sim-bridge library

# Unit test executable
add_executable(unit_tests
    CircularBufferTest.cpp
    CircularBufferTelemetryTest.cpp
    IAudioBufferTest.cpp
    IntegrationAudioPlayerTest.cpp
    ThreadedStrategyIntegrationTest.cpp
    CommandLineParserTest.cpp
    AudioTestConstants.cpp
    StrategyPipelineTest.cpp
    BufferContextEradicationTest.cpp
    ISimulatorTest.cpp
    # Required support code
    ../../src/config/CLIconfig.cpp
    ../../src/config/ANSIColors.cpp # Required for display
    ../../test/mocks/MockDataSimulator.cpp
    ../../test/mocks/MockAudioUnit.cpp
)

target_link_libraries(unit_tests
    PRIVATE
        gtest
        gtest_main
        engine-sim-bridge
        ${AUDIO_LIBRARY}
        CLI11::CLI11
)

# SimulationLoop and EngineConfig are now in engine-sim-bridge library
# (linked via target_link_libraries above)

target_include_directories(unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/test/unit
        ${CMAKE_SOURCE_DIR}/test/mocks
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/include
)

# Register unit tests
add_test(NAME unit_tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/unit_tests
        --gtest_color=yes
        --gtest_output=json:${CMAKE_CURRENT_BINARY_DIR}/unit_tests.json
)

set_tests_properties(unit_tests PROPERTIES
    TIMEOUT 30
    LABELS "unit"
)
