// SyncPullRendererTest.cpp - Unit tests for SyncPullRenderer
// Tests on-demand rendering, deterministic output, pre-buffer handling
// TDD approach: RED -> GREEN -> REFACTOR

#include "audio/renderers/SyncPullRenderer.h"
#include "SyncPullAudio.h"
#include "AudioPlayer.h"  // For AudioUnitContext
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <vector>
#include <gtest/gtest.h>

using namespace test::constants;

// Test fixture for SyncPullRenderer tests
class SyncPullRendererTest : public ::testing::Test {
protected:
    SyncPullRenderer renderer;
};

// ============================================================================
// HAPPY PATH TESTS - Core functionality
// ============================================================================

TEST_F(SyncPullRendererTest, Renderer_ReturnsCorrectName) {
    // Act & Assert: Renderer name matches expected
    EXPECT_STREQ(renderer.getName(), "SyncPullRenderer");
}

TEST_F(SyncPullRendererTest, IsEnabled_ReturnsTrue) {
    // Act & Assert: SyncPullRenderer is always enabled
    EXPECT_TRUE(renderer.isEnabled());
}

TEST_F(SyncPullRendererTest, Render_WithNullContext_ReturnsFalse) {
    // Arrange: Create audio buffer list
    const UInt32 frames = 256;
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = STEREO_CHANNELS;
    bufferList.mBuffers[0].mDataByteSize = frames * STEREO_CHANNELS * sizeof(float);
    bufferList.mBuffers[0].mData = new float[frames * STEREO_CHANNELS]();

    // Act: Try to render with null context
    bool result = renderer.render(nullptr, &bufferList, frames);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    // Cleanup
    delete[] static_cast<float*>(bufferList.mBuffers[0].mData);
}

TEST_F(SyncPullRendererTest, Render_WithContextWithoutSyncPullAudio_ReturnsFalse) {
    // Arrange: Context without syncPullAudio initialized
    AudioUnitContext context;
    context.syncPullAudio = nullptr;

    const UInt32 frames = 256;
    AudioBufferList bufferList;
    bufferList.mNumberBuffers = 1;
    bufferList.mBuffers[0].mNumberChannels = STEREO_CHANNELS;
    bufferList.mBuffers[0].mDataByteSize = frames * STEREO_CHANNELS * sizeof(float);
    bufferList.mBuffers[0].mData = new float[frames * STEREO_CHANNELS]();

    // Act: Try to render without syncPullAudio
    bool result = renderer.render(&context, &bufferList, frames);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    // Cleanup
    delete[] static_cast<float*>(bufferList.mBuffers[0].mData);
}

TEST_F(SyncPullRendererTest, AddFrames_AlwaysReturnsTrue) {
    // Arrange: Test data
    std::vector<float> testData(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: AddFrames is a no-op for sync-pull mode
    bool result = renderer.AddFrames(nullptr, testData.data(), TEST_FRAME_COUNT);

    // Assert: Should return true (no-op implementation)
    EXPECT_TRUE(result);
}
