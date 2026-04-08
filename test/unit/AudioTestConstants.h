// AudioTestConstants.h - Test constants for deterministic audio tests
// Eliminates magic numbers in tests

#ifndef AUDIO_TEST_CONSTANTS_H
#define AUDIO_TEST_CONSTANTS_H

namespace test {
namespace constants {

// Buffer sizes (frames)
constexpr int DEFAULT_BUFFER_CAPACITY = 100;
constexpr int SMALL_BUFFER_CAPACITY = 16;
constexpr int TINY_BUFFER_CAPACITY = 10;

// Frame counts for test operations
constexpr int STANDARD_FRAME_COUNT = 50;
constexpr int LARGE_FRAME_COUNT = 90;
constexpr int TEST_FRAME_COUNT = 30;

// Audio parameters
constexpr int STEREO_CHANNELS = 2;
constexpr int DEFAULT_SAMPLE_RATE = 48000;
constexpr float FLOAT_TOLERANCE = 0.0001f;

// Test signal values
constexpr float TEST_SIGNAL_VALUE_1 = 1.0f;
constexpr float TEST_SIGNAL_VALUE_2 = 2.0f;
constexpr float TEST_SIGNAL_VALUE_3 = 3.0f;
constexpr float SILENCE_VALUE = 0.0f;

// Test signal buffers
extern const float TEST_SIGNAL_BUFFER[];

} // namespace constants
} // namespace test

#endif // AUDIO_TEST_CONSTANTS_H
