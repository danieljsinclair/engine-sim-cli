// AudioTestHelpers.h - Minimal helper functions for audio tests
// Add helpers ONLY when actual duplication is observed (YAGNI)

#ifndef AUDIO_TEST_HELPERS_H
#define AUDIO_TEST_HELPERS_H

#include <vector>
#include <gtest/gtest.h>
#include "AudioTestConstants.h"

// Forward declarations
class CircularBuffer;

namespace test {

// Helper: Validate exact float match (no tolerance) for buffer math tests
inline void validateExactMatch(
    const float* actual,
    const float* expected,
    int frames,
    const char* message = "Audio samples don't match exactly"
) {
    for (int i = 0; i < frames * constants::STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(actual[i], expected[i]) << message << " at sample index " << i;
    }
}

// Helper: Validate near match (with tolerance) for renderer tests
inline void validateNearMatch(
    const float* actual,
    const float* expected,
    int frames,
    float tolerance = constants::FLOAT_TOLERANCE,
    const char* message = "Audio samples don't match within tolerance"
) {
    for (int i = 0; i < frames * constants::STEREO_CHANNELS; ++i) {
        EXPECT_NEAR(actual[i], expected[i], tolerance) << message << " at sample index " << i;
    }
}

} // namespace test

#endif // AUDIO_TEST_HELPERS_H
