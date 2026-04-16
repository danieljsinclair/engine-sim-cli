# Unit-level deterministic audio tests
# TDD approach: RED -> GREEN -> REFACTOR
# Phase F: Strategy/buffer/hardware sources now come from engine-sim-bridge library

# Unit test executable
add_executable(unit_tests
    CircularBufferTest.cpp
    CircularBufferTelemetryTest.cpp
    IAudioStrategyTest.cpp
    IntegrationAudioPlayerTest.cpp
    ThreadedStrategyIntegrationTest.cpp
    AudioTestConstants.cpp
    StrategyPipelineTest.cpp
    BufferContextEradicationTest.cpp
    ISimulatorTest.cpp
    # Required support code
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
)

# Add source files that stay in CLI to be compiled with the unit tests
target_sources(unit_tests
    PRIVATE
        ../../src/audio/strategies/IAudioStrategyFactory.cpp
        ../../src/audio/hardware/AudioHardwareProviderFactory.cpp
        ../../src/simulation/EngineConfig.cpp
        ../../src/simulation/SimulationLoop.cpp
)

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
