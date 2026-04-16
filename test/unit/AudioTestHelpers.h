// AudioTestHelpers.h - Shared helper functions for audio tests
// DRY: Extracted from 7 test files that duplicated these helpers

#ifndef AUDIO_TEST_HELPERS_H
#define AUDIO_TEST_HELPERS_H

#include <vector>
#include <gtest/gtest.h>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include "AudioTestConstants.h"

// Forward declarations
class CircularBuffer;

// Helper: Create AudioBufferList for testing
inline AudioBufferList createAudioBufferList(UInt32 frames) {
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = test::constants::STEREO_CHANNELS;
    bufferList.mBuffers[0].mDataByteSize = frames * test::constants::STEREO_CHANNELS * sizeof(float);
    bufferList.mBuffers[0].mData = new float[frames * test::constants::STEREO_CHANNELS]();
    return bufferList;
}

// Helper: Free AudioBufferList created by createAudioBufferList
inline void freeAudioBufferList(AudioBufferList& bufferList) {
    if (bufferList.mBuffers[0].mData) {
        delete[] static_cast<float*>(bufferList.mBuffers[0].mData);
        bufferList.mBuffers[0].mData = nullptr;
    }
}

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

// Helper: Verify all samples in a buffer are silence (0.0f)
inline void verifySilence(const float* data, int frames, const char* context = "buffer") {
    for (int i = 0; i < frames * constants::STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(data[i], 0.0f) << context << " should be silence at sample " << i;
    }
}

} // namespace test

#endif // AUDIO_TEST_HELPERS_H
