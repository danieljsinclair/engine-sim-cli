# Unit-level deterministic audio tests
# TDD approach: RED -> GREEN -> REFACTOR

# Unit test executable
add_executable(unit_tests
    CircularBufferTest.cpp
    ThreadedRendererTest.cpp
    # Source files under test (direct inclusion for unit tests)
    ../../src/audio/common/CircularBuffer.cpp
    ../../src/audio/renderers/ThreadedRenderer.cpp
    ../../src/SyncPullAudio.cpp
    # Add more test files here as we implement them:
    # SyncPullRendererTest.cpp
)

target_link_libraries(unit_tests
    PRIVATE
        gtest
        gtest_main
        engine-sim-bridge
        ${AUDIO_LIBRARY}
)

target_include_directories(unit_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/test/unit
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
