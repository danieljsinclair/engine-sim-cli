# Unit-level deterministic audio tests
# TDD approach: RED -> GREEN -> REFACTOR

# Unit test executable
add_executable(unit_tests
    CircularBufferTest.cpp
    CircularBufferTelemetryTest.cpp
    # IAudioHardwareProviderTest.cpp - TODO: Create this test
    # StateManagementTest.cpp - TODO: Create this test
    # ThreadedStrategyTest.cpp - TODO: Create this test
    # AudioRegressionTest.cpp - TEMPORARILY DISABLED due to missing ThreadedRenderer
    IAudioStrategyTest.cpp
    IntegrationAudioPlayerTest.cpp
    ThreadedStrategyIntegrationTest.cpp
    AudioTestConstants.cpp
    DebugTest.cpp
    MockSimulatorDebugTest.cpp
    StrategyPipelineTest.cpp
    # Source files under test (direct inclusion for unit tests)
    ../../src/audio/common/CircularBuffer.cpp
    ../../src/AudioPlayer.cpp  # Required by renderers
    ../../src/config/ANSIColors.cpp # Required for display
    # ../../test/mocks/MockIAudioHardwareProvider.cpp - TODO: Create this file
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

# Add the strategy implementations to be compiled with the unit tests
target_sources(unit_tests
    PRIVATE
        ../../src/audio/strategies/IAudioStrategyFactory.cpp
        ../../src/audio/strategies/ThreadedStrategy.cpp
        ../../src/audio/strategies/SyncPullStrategy.cpp
        ../../src/audio/hardware/AudioHardwareProviderFactory.cpp
        ../../src/audio/hardware/CoreAudioHardwareProvider.cpp
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
