// ThreadedRendererTest.cpp - Unit tests for ThreadedRenderer
// Tests cursor-chasing logic, underrun detection, wrap-around reads
// TDD approach: RED -> GREEN -> REFACTOR

#include "audio/renderers/ThreadedRenderer.h"
#include "audio/common/CircularBuffer.h"
#include "AudioPlayer.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <vector>
#include <gtest/gtest.h>

using namespace test::constants;

// Test fixture for ThreadedRenderer tests
class ThreadedRendererTest : public ::testing::Test {
protected:
    void SetUp() override {
        renderer = std::make_unique<ThreadedRenderer>();
        context = std::make_unique<AudioUnitContext>();
    }

    void TearDown() override {
        renderer.reset();
        context.reset();
    }

    // Helper: Create and initialize circular buffer
    void initCircularBuffer(int capacity) {
        context->circularBuffer = std::make_unique<CircularBuffer>();
        ASSERT_TRUE(context->circularBuffer->initialize(capacity))
            << "Failed to initialize circular buffer with capacity " << capacity;
    }

    // Helper: Create AudioBufferList for testing
    AudioBufferList createAudioBufferList(UInt32 frames) {
        AudioBufferList bufferList;
        bufferList.mNumberBuffers = 1;
        bufferList.mBuffers[0].mNumberChannels = STEREO_CHANNELS;
        bufferList.mBuffers[0].mDataByteSize = frames * STEREO_CHANNELS * sizeof(float);
        bufferList.mBuffers[0].mData = new float[frames * STEREO_CHANNELS]();
        return bufferList;
    }

    // Helper: Free AudioBufferList
    void freeAudioBufferList(AudioBufferList& bufferList) {
        if (bufferList.mBuffers[0].mData) {
            delete[] static_cast<float*>(bufferList.mBuffers[0].mData);
            bufferList.mBuffers[0].mData = nullptr;
        }
    }

    std::unique_ptr<ThreadedRenderer> renderer;
    std::unique_ptr<AudioUnitContext> context;
};

// ============================================================================
// HAPPY PATH TESTS - Core functionality
// ============================================================================

TEST_F(ThreadedRendererTest, Renderer_ReturnsCorrectName) {
    // Act & Assert: Renderer name matches expected
    EXPECT_STREQ(renderer->getName(), "ThreadedRenderer");
}

TEST_F(ThreadedRendererTest, IsEnabled_ReturnsTrue) {
    // Act & Assert: ThreadedRenderer is always enabled
    EXPECT_TRUE(renderer->isEnabled());
}

TEST_F(ThreadedRendererTest, Render_WithUninitializedBuffer_ReturnsFalse) {
    // Arrange: Context with uninitialized circular buffer
    context->circularBuffer = std::make_unique<CircularBuffer>();
    // Don't initialize the buffer

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render with uninitialized buffer
    bool result = renderer->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}
