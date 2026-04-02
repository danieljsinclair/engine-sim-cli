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

TEST_F(ThreadedRendererTest, CursorChasing_ReadsAvailableFrames) {
    // Arrange: Buffer with DEFAULT_BUFFER_CAPACITY frames, write pointer at TEST_FRAME_COUNT, read at 0
    // This simulates cursor-chasing scenario
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    // Fill buffer to TEST_FRAME_COUNT frames
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    context->circularBuffer->write(input.data(), TEST_FRAME_COUNT);

    context->writePointer.store(TEST_FRAME_COUNT);
    context->readPointer.store(0);

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Request TEST_FRAME_COUNT frames
    bool success = renderer->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Read exactly TEST_FRAME_COUNT frames, no underrun
    EXPECT_TRUE(success);
    EXPECT_EQ(context->readPointer.load(), TEST_FRAME_COUNT);
    EXPECT_EQ(context->underrunCount.load(), 0);

    // Verify the audio data matches what we wrote
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    test::validateExactMatch(data, input.data(), TEST_FRAME_COUNT);

    freeAudioBufferList(audioBuffer);
}

TEST_F(ThreadedRendererTest, CursorChasing_PartialReadWhenLessAvailable) {
    // Arrange: Buffer with fewer frames than requested
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    // Only fill 10 frames
    constexpr int availableFrames = 10;
    std::vector<float> input(availableFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    context->circularBuffer->write(input.data(), availableFrames);

    context->writePointer.store(availableFrames);
    context->readPointer.store(0);

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Request TEST_FRAME_COUNT frames (more than available)
    bool success = renderer->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Should succeed but with underrun detected
    EXPECT_TRUE(success);
    EXPECT_EQ(context->underrunCount.load(), 1);
    // Note: bufferStatus is 2 (critical) when available < bufferSize/8 (10 < 12.5)
    // and 1 (warning) otherwise
    EXPECT_EQ(context->bufferStatus, 2);  // Critical state due to low buffer

    // Verify read pointer moved only by available frames
    EXPECT_EQ(context->readPointer.load(), availableFrames);

    // Verify first availableFrames have audio, rest are silence
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < availableFrames * STEREO_CHANNELS; i++) {
        EXPECT_FLOAT_EQ(data[i], TEST_SIGNAL_VALUE_1);
    }
    for (int i = availableFrames * STEREO_CHANNELS; i < TEST_FRAME_COUNT * STEREO_CHANNELS; i++) {
        EXPECT_FLOAT_EQ(data[i], SILENCE_VALUE);
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(ThreadedRendererTest, CursorChasing_WrapAroundRead) {
    // Arrange: Simulate wrap-around scenario where read spans buffer boundary
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    // Fill buffer to near the end to set up wrap scenario
    constexpr int initialFill = 90;
    std::vector<float> initialData(initialFill * STEREO_CHANNELS, 0.0f);
    context->circularBuffer->write(initialData.data(), initialFill);

    // Read all to advance pointers
    std::vector<float> consume(initialFill * STEREO_CHANNELS);
    context->circularBuffer->read(consume.data(), initialFill);

    // Now write frames that will wrap around (90 + 20 = 110 % 100 = 10)
    constexpr int wrapFrames = 20;
    std::vector<float> wrapData(wrapFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);
    context->circularBuffer->write(wrapData.data(), wrapFrames);

    // Set up context pointers for wrap-around read
    // We simulate: readPtr at 90, writePtr at 10 (wrapped)
    // This means data spans from 90-99 (10 frames) and 0-9 (10 frames)
    context->readPointer.store(90);
    context->writePointer.store(10);

    // Verify data is available in buffer
    EXPECT_GT(context->circularBuffer->available(), 0);

    AudioBufferList audioBuffer = createAudioBufferList(wrapFrames);

    // Act: Read wrapFrames frames (should span boundary)
    bool success = renderer->render(context.get(), &audioBuffer, wrapFrames);

    // Assert: Read succeeded without underrun
    EXPECT_TRUE(success);
    EXPECT_EQ(context->underrunCount.load(), 0);

    // Verify the audio data matches what we wrote
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    test::validateExactMatch(data, wrapData.data(), wrapFrames);

    // Verify read pointer advanced correctly (90 + 20 = 110 % 100 = 10)
    EXPECT_EQ(context->readPointer.load(), 10);

    freeAudioBufferList(audioBuffer);
}

TEST_F(ThreadedRendererTest, AddFrames_WritesToCircularBuffer) {
    // Arrange: Initialize circular buffer
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: Add frames to the buffer
    bool success = renderer->AddFrames(context.get(), input.data(), TEST_FRAME_COUNT);

    // Assert: Frames were added successfully
    EXPECT_TRUE(success);

    // Verify write pointer advanced
    EXPECT_EQ(context->writePointer.load(), TEST_FRAME_COUNT);

    // Verify data is in the buffer
    std::vector<float> output(TEST_FRAME_COUNT * STEREO_CHANNELS);
    size_t read = context->circularBuffer->read(output.data(), TEST_FRAME_COUNT);
    EXPECT_EQ(read, TEST_FRAME_COUNT);

    test::validateExactMatch(output.data(), input.data(), TEST_FRAME_COUNT);
}

TEST_F(ThreadedRendererTest, AddFrames_WithUninitializedBuffer_ReturnsFalse) {
    // Arrange: Context with uninitialized circular buffer
    context->circularBuffer = std::make_unique<CircularBuffer>();
    // Don't initialize the buffer

    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: Try to add frames
    bool success = renderer->AddFrames(context.get(), input.data(), TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(success);
}

TEST_F(ThreadedRendererTest, AddFrames_WithNullContext_ReturnsFalse) {
    // Arrange: Null context
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: Try to add frames with null context
    bool success = renderer->AddFrames(nullptr, input.data(), TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(success);
}
