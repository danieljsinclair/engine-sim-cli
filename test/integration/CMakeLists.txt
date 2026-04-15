# Integration-level audio tests
# Tests at Simulator interface level using SineWaveSimulator
# TDD: Tests real audio bugs at the right abstraction level

# Mock objects for DI injection
set(MOCK_SOURCES
    ../mocks/MockDataSimulator.cpp
    ../mocks/MockAudioUnit.cpp
)

# Integration test executable
add_executable(integration_tests
    SimulatorLevelAudioTest.cpp
    ${MOCK_SOURCES}
    # Source files under test (Simulator-level integration)
    ../../src/audio/common/CircularBuffer.cpp
    ../../src/AudioPlayer.cpp
    ../../src/AudioSource.cpp
    ../../src/config/ANSIColors.cpp
    # Audio strategies
    ../../src/audio/strategies/IAudioStrategyFactory.cpp
    ../../src/audio/strategies/ThreadedStrategy.cpp
    ../../src/audio/strategies/SyncPullStrategy.cpp
    # Audio hardware providers
    ../../src/audio/hardware/CoreAudioHardwareProvider.cpp
    ../../src/audio/hardware/AudioHardwareProviderFactory.cpp
)

target_link_libraries(integration_tests
    PRIVATE
        gtest
        gtest_main
        engine-sim
        engine-sim-bridge
        ${AUDIO_LIBRARY}
)

target_include_directories(integration_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/test/unit
        ${CMAKE_SOURCE_DIR}/test/mocks
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/include
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/include
)

# Register integration tests
add_test(NAME integration_tests
    COMMAND ${CMAKE_CURRENT_BINARY_DIR}/integration_tests
        --gtest_color=yes
        --gtest_output=json:${CMAKE_CURRENT_BINARY_DIR}/integration_tests.json
)

set_tests_properties(integration_tests PROPERTIES
    TIMEOUT 60
    LABELS "integration"
)
