// AudioTestConstants.cpp - Test constants implementation
// Defines test signal buffers for deterministic audio tests

#include "AudioTestConstants.h"

namespace test {
namespace constants {

// Test signal buffer (30 frames, 2 channels = 60 samples)
constexpr int TEST_SIGNAL_BUFFER_SIZE = TEST_FRAME_COUNT * STEREO_CHANNELS;
const float TEST_SIGNAL_BUFFER[TEST_SIGNAL_BUFFER_SIZE] = {
    // Frame 0
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 1
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 2
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 3
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 4
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 5
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 6
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 7
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 8
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 9
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 10
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 11
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 12
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 13
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 14
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 15
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 16
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 17
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 18
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 19
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 20
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 21
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 22
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 23
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 24
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 25
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 26
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 27
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 28
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1,
    // Frame 29
    TEST_SIGNAL_VALUE_1, TEST_SIGNAL_VALUE_1
};

} // namespace constants
} // namespace test
