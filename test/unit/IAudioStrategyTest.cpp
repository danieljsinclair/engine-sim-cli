// IAudioStrategyTest.cpp - Unit tests for IAudioStrategy interface
// TDD approach: RED -> GREEN -> REFACTOR
// Tests the new IAudioStrategy interface with ThreadedStrategy and SyncPullStrategy

#include "audio/strategies/IAudioStrategy.h"
#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"
#include "audio/state/BufferContext.h"
#include "audio/common/CircularBuffer.h"
#include "AudioPlayer.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <vector>
#include <gtest/gtest.h>

using namespace test::constants;

// Test fixture for IAudioStrategy tests
class IAudioStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        threadedStrategy = std::make_unique<ThreadedStrategy>();
        syncPullStrategy = std::make_unique<SyncPullStrategy>();
        context = std::make_unique<BufferContext>();
    }

    void TearDown() override {
        threadedStrategy.reset();
        syncPullStrategy.reset();
        context.reset();
    }

    // Helper: Create and initialize circular buffer for ThreadedStrategy
    void initCircularBuffer(int capacity) {
        circularBuffer = std::make_unique<CircularBuffer>();
        ASSERT_TRUE(circularBuffer->initialize(capacity))
            << "Failed to initialize circular buffer with capacity " << capacity;
        context->circularBuffer = circularBuffer.get();
        context->bufferState.capacity = capacity;
        // Initialize logical read pointer for cursor-chasing
        context->bufferState.readPointer.store(0);
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

    std::unique_ptr<IAudioStrategy> threadedStrategy;
    std::unique_ptr<IAudioStrategy> syncPullStrategy;
    std::unique_ptr<BufferContext> context;
    std::unique_ptr<CircularBuffer> circularBuffer;  // Owns the circular buffer
};

// ============================================================================
// HAPPY PATH TESTS - Core functionality
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_ReturnsCorrectName) {
    // Act & Assert: ThreadedStrategy name matches expected
    EXPECT_STREQ(threadedStrategy->getName(), "Threaded");
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_ReturnsCorrectName) {
    // Act & Assert: SyncPullStrategy name matches expected
    EXPECT_STREQ(syncPullStrategy->getName(), "SyncPull");
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_IsEnabled) {
    // Act & Assert: ThreadedStrategy is always enabled
    EXPECT_TRUE(threadedStrategy->isEnabled());
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_IsEnabled) {
    // Act & Assert: SyncPullStrategy is always enabled
    EXPECT_TRUE(syncPullStrategy->isEnabled());
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_Render_WithUninitializedBuffer_ReturnsFalse) {
    // Arrange: Context with uninitialized circular buffer
    circularBuffer = std::make_unique<CircularBuffer>();
    context->circularBuffer = circularBuffer.get();
    // Don't initialize the buffer

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render with uninitialized buffer
    bool result = threadedStrategy->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_Render_WithContextWithoutSyncPullAudio_ReturnsFalse) {
    // Arrange: Context without SyncPullAudio
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render without SyncPullAudio
    bool result = syncPullStrategy->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// STRATEGY-SPECIFIC BEHAVIOR TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_CursorChasing_ReadsAvailableFrames) {
    // Arrange: Buffer with DEFAULT_BUFFER_CAPACITY frames
    // Note: CircularBuffer manages its own internal read/write pointers
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    // Fill buffer to TEST_FRAME_COUNT frames
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    size_t framesWritten = context->circularBuffer->write(input.data(), TEST_FRAME_COUNT);
    ASSERT_EQ(framesWritten, static_cast<size_t>(TEST_FRAME_COUNT));

    // Set context pointers to match CircularBuffer's internal state
    context->bufferState.writePointer.store(context->circularBuffer->getWritePointer());
    context->bufferState.readPointer.store(context->circularBuffer->getReadPointer());

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Render available frames
    bool result = threadedStrategy->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should succeed and return the expected frames
    EXPECT_TRUE(result);

    // Verify buffer contains expected data
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < TEST_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], TEST_SIGNAL_VALUE_1);
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_CursorChasing_ReadsLessWhenNotEnough) {
    // Arrange: Buffer with DEFAULT_BUFFER_CAPACITY frames
    // Note: CircularBuffer manages its own internal read/write pointers
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    // Fill buffer only halfway
    std::vector<float> input((TEST_FRAME_COUNT / 2) * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);
    size_t framesWritten = context->circularBuffer->write(input.data(), TEST_FRAME_COUNT / 2);
    ASSERT_EQ(framesWritten, static_cast<size_t>(TEST_FRAME_COUNT / 2));

    // Set context pointers to match CircularBuffer's internal state
    context->bufferState.writePointer.store(context->circularBuffer->getWritePointer());
    context->bufferState.readPointer.store(context->circularBuffer->getReadPointer());

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Request more frames than available
    bool result = threadedStrategy->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should succeed but only read available frames
    EXPECT_TRUE(result);

    // Verify only TEST_FRAME_COUNT/2 frames contain data
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < (TEST_FRAME_COUNT / 2) * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], TEST_SIGNAL_VALUE_2);
    }

    // Verify remaining frames are silence
    for (int i = (TEST_FRAME_COUNT / 2) * STEREO_CHANNELS; i < TEST_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], 0.0f);
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_CursorChasing_WrapAroundRead) {
    // Arrange: Test wrap-around behavior by writing near buffer end and then reading
    // Note: This test demonstrates that CircularBuffer handles wrap-around internally
    initCircularBuffer(DEFAULT_BUFFER_CAPACITY);

    // Write data to fill most of the buffer
    int initialWrite = DEFAULT_BUFFER_CAPACITY - (TEST_FRAME_COUNT / 2);
    std::vector<float> input1(initialWrite * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    context->circularBuffer->write(input1.data(), initialWrite);

    // Read some frames to advance read pointer
    std::vector<float> tempBuffer1((TEST_FRAME_COUNT / 2) * STEREO_CHANNELS);
    context->circularBuffer->read(tempBuffer1.data(), TEST_FRAME_COUNT / 2);

    // Write more frames that will cause wrap-around
    std::vector<float> input2((TEST_FRAME_COUNT / 2) * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    context->circularBuffer->write(input2.data(), TEST_FRAME_COUNT / 2);

    // Set context pointers to match CircularBuffer's internal state
    context->bufferState.writePointer.store(context->circularBuffer->getWritePointer());
    context->bufferState.readPointer.store(context->circularBuffer->getReadPointer());

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Render frames that were written after wrap-around
    bool result = threadedStrategy->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should succeed
    EXPECT_TRUE(result);

    // Verify buffer contains the data that was written after wrap-around
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < (TEST_FRAME_COUNT / 2) * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], TEST_SIGNAL_VALUE_3);
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_AlwaysReturnsTrueOnAddFrames) {
    // Arrange: Context with any configuration
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Add frames to SyncPullStrategy
    bool result = syncPullStrategy->AddFrames(context.get(), const_cast<float*>(TEST_SIGNAL_BUFFER), TEST_FRAME_COUNT);

    // Assert: Should always return true
    EXPECT_TRUE(result);

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// STRATEGY FACTORY TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, Factory_CreateThreadedStrategy) {
    // Act: Create ThreadedStrategy via factory
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, nullptr);

    // Assert: Should create ThreadedStrategy
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
}

TEST_F(IAudioStrategyTest, Factory_CreateSyncPullStrategy) {
    // Act: Create SyncPullStrategy via factory
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, nullptr);

    // Assert: Should create SyncPullStrategy
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
}

TEST_F(IAudioStrategyTest, Factory_CreateUnknownMode_ReturnsNull) {
    // Act: Try to create unknown mode
    auto strategy = IAudioStrategyFactory::createStrategy(static_cast<AudioMode>(999), nullptr);

    // Assert: Should return nullptr
    EXPECT_EQ(strategy, nullptr);
}

// ============================================================================
// ERROR CONDITION TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_AddFrames_WithNullContext_ReturnsFalse) {
    // Arrange: null context
    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: Add frames with null context
    bool result = threadedStrategy->AddFrames(nullptr, buffer.data(), TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(result);
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_AddFrames_WithNullBuffer_ReturnsFalse) {
    // Arrange: null buffer
    circularBuffer = std::make_unique<CircularBuffer>();
    context->circularBuffer = circularBuffer.get();
    context->circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY);

    // Act: Add null buffer
    bool result = threadedStrategy->AddFrames(context.get(), nullptr, TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(result);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_Render_WithNullContext_ReturnsFalse) {
    // Arrange: null context
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Render with null context
    bool result = syncPullStrategy->render(nullptr, &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// STRATEGY-SPECIFIC METHOD TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_ShouldDrainDuringWarmup) {
    // Act: Check drain behavior
    bool shouldDrain = threadedStrategy->shouldDrainDuringWarmup();

    // Assert: ThreadedStrategy should drain during warmup
    EXPECT_TRUE(shouldDrain);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_ShouldNotDrainDuringWarmup) {
    // Act: Check drain behavior
    bool shouldDrain = syncPullStrategy->shouldDrainDuringWarmup();

    // Assert: SyncPullStrategy should not drain during warmup
    EXPECT_FALSE(shouldDrain);
}