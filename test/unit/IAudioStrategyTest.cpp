// IAudioStrategyTest.cpp - Unit tests for IAudioStrategy interface
// TDD approach: RED -> GREEN -> REFACTOR
// Tests the IAudioStrategy interface with ThreadedStrategy and SyncPullStrategy
// Strategies own their own state -- no BufferContext needed

#include "IAudioStrategy.h"
#include "ThreadedStrategy.h"
#include "SyncPullStrategy.h"
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
    }

    void TearDown() override {
        threadedStrategy.reset();
        syncPullStrategy.reset();
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
};

// ============================================================================
// HAPPY PATH TESTS - Core functionality
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_ReturnsCorrectName) {
    EXPECT_STREQ(threadedStrategy->getName(), "Threaded");
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_ReturnsCorrectName) {
    EXPECT_STREQ(syncPullStrategy->getName(), "SyncPull");
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_IsEnabled) {
    EXPECT_TRUE(threadedStrategy->isEnabled());
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_IsEnabled) {
    EXPECT_TRUE(syncPullStrategy->isEnabled());
}

TEST_F(IAudioStrategyTest, ThreadedStrategy_Render_WithUninitializedBuffer_ReturnsFalse) {
    // Arrange: ThreadedStrategy without initialization -- internal buffer not created
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render without calling initialize()
    bool result = threadedStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_Render_WithoutEngineAPI_FillsSilence) {
    // Arrange: SyncPullStrategy without engine API
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render without engine API (not set since no initialize)
    bool result = syncPullStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fill silence and return true (safe shutdown behavior)
    EXPECT_TRUE(result);

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// STRATEGY-SPECIFIC BEHAVIOR TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_CursorChasing_ReadsAvailableFrames) {
    // Arrange: Initialize strategy so it creates its internal circular buffer
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(threadedStrategy->initialize(config));

    // Fill strategy's internal buffer with test data via AddFrames
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    ASSERT_TRUE(threadedStrategy->AddFrames(input.data(), TEST_FRAME_COUNT));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Render available frames
    bool result = threadedStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

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
    // Arrange: Initialize strategy
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(threadedStrategy->initialize(config));

    // Fill buffer only halfway
    std::vector<float> input((TEST_FRAME_COUNT / 2) * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);
    ASSERT_TRUE(threadedStrategy->AddFrames(input.data(), TEST_FRAME_COUNT / 2));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Request more frames than available
    bool result = threadedStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

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
    // Arrange: Initialize strategy with a small sample rate for manageable buffer
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(threadedStrategy->initialize(config));

    // Write data to fill most of the buffer
    int halfFrames = TEST_FRAME_COUNT / 2;
    std::vector<float> input1(halfFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    ASSERT_TRUE(threadedStrategy->AddFrames(input1.data(), halfFrames));

    // Read some frames to advance read pointer
    AudioBufferList readBuffer = createAudioBufferList(halfFrames);
    ASSERT_TRUE(threadedStrategy->render(&readBuffer, halfFrames));
    freeAudioBufferList(readBuffer);

    // Write more frames
    std::vector<float> input2(halfFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    ASSERT_TRUE(threadedStrategy->AddFrames(input2.data(), halfFrames));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Render frames that were written after advancing
    bool result = threadedStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should succeed (even if partially silent)
    EXPECT_TRUE(result);

    // Verify the first half contains the signal
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < halfFrames * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], TEST_SIGNAL_VALUE_3);
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_AlwaysReturnsTrueOnAddFrames) {
    // Arrange: AddFrames is a no-op for SyncPull, always returns true
    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: Add frames to SyncPullStrategy
    bool result = syncPullStrategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);

    // Assert: Should always return true
    EXPECT_TRUE(result);
}

// ============================================================================
// STRATEGY FACTORY TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, Factory_CreateThreadedStrategy) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, nullptr);
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
}

TEST_F(IAudioStrategyTest, Factory_CreateSyncPullStrategy) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, nullptr);
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
}

TEST_F(IAudioStrategyTest, Factory_CreateUnknownMode_ReturnsNull) {
    auto strategy = IAudioStrategyFactory::createStrategy(static_cast<AudioMode>(999), nullptr);
    EXPECT_EQ(strategy, nullptr);
}

// ============================================================================
// ERROR CONDITION TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_AddFrames_WithNullBuffer_ReturnsFalse) {
    // Arrange: Initialize strategy first
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(threadedStrategy->initialize(config));

    // Act: Add null buffer
    bool result = threadedStrategy->AddFrames(nullptr, TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(result);
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_Render_WithNullBuffer_ReturnsFalse) {
    // Arrange: null buffer
    bool result = syncPullStrategy->render(nullptr, TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(result);
}

// ============================================================================
// STRATEGY-SPECIFIC METHOD TESTS
// ============================================================================

TEST_F(IAudioStrategyTest, ThreadedStrategy_ShouldDrainDuringWarmup) {
    EXPECT_TRUE(threadedStrategy->shouldDrainDuringWarmup());
}

TEST_F(IAudioStrategyTest, SyncPullStrategy_ShouldNotDrainDuringWarmup) {
    EXPECT_FALSE(syncPullStrategy->shouldDrainDuringWarmup());
}
