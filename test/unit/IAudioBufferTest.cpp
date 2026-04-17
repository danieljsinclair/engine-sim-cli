// IAudioBufferTest.cpp - Unit tests for IAudioBuffer interface
// TDD approach: RED -> GREEN -> REFACTOR
// Tests the IAudioBuffer interface with ThreadedStrategy and SyncPullStrategy
// Strategies own their own state -- no BufferContext needed

#include "strategy/IAudioBuffer.h"
#include "strategy/ThreadedStrategy.h"
#include "strategy/SyncPullStrategy.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <vector>
#include <gtest/gtest.h>

using namespace test::constants;

// Test fixture for IAudioBuffer tests
class IAudioBufferTest : public ::testing::Test {
protected:
    void SetUp() override {
        threadedStrategy = std::make_unique<ThreadedStrategy>();
        syncPullStrategy = std::make_unique<SyncPullStrategy>();
    }

    void TearDown() override {
        threadedStrategy.reset();
        syncPullStrategy.reset();
    }

    std::unique_ptr<IAudioBuffer> threadedStrategy;
    std::unique_ptr<IAudioBuffer> syncPullStrategy;
};

// ============================================================================
// HAPPY PATH TESTS - Core functionality
// ============================================================================

TEST_F(IAudioBufferTest, ThreadedStrategy_Render_WithUninitializedBuffer_ReturnsFalse) {
    // Arrange: ThreadedStrategy without initialization -- internal buffer not created
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render without calling initialize()
    bool result = threadedStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fail gracefully
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(IAudioBufferTest, SyncPullStrategy_Render_WithoutEngineAPI_FillsSilence) {
    // Arrange: SyncPullStrategy without engine API
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: Try to render without engine API (not set since no initialize)
    bool result = syncPullStrategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should fill silence and return true (safe shutdown behavior)
    EXPECT_TRUE(result);

    // Assert: Buffer content should be all zeros
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    test::verifySilence(outputData, TEST_FRAME_COUNT, "SyncPull render without engine API");

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// STRATEGY-SPECIFIC BEHAVIOR TESTS
// ============================================================================

TEST_F(IAudioBufferTest, ThreadedStrategy_CursorChasing_ReadsAvailableFrames) {
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

TEST_F(IAudioBufferTest, ThreadedStrategy_CursorChasing_ReadsLessWhenNotEnough) {
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

TEST_F(IAudioBufferTest, ThreadedStrategy_CursorChasing_WrapAroundRead) {
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

TEST_F(IAudioBufferTest, SyncPullStrategy_AlwaysReturnsTrueOnAddFrames) {
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

TEST_F(IAudioBufferTest, Factory_CreateThreadedStrategy) {
    auto strategy = IAudioBufferFactory::createStrategy(AudioMode::Threaded, nullptr);
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
}

TEST_F(IAudioBufferTest, Factory_CreateSyncPullStrategy) {
    auto strategy = IAudioBufferFactory::createStrategy(AudioMode::SyncPull, nullptr);
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
}

TEST_F(IAudioBufferTest, Factory_CreateUnknownMode_ReturnsNull) {
    auto strategy = IAudioBufferFactory::createStrategy(static_cast<AudioMode>(999), nullptr);
    EXPECT_EQ(strategy, nullptr);
}

// ============================================================================
// ERROR CONDITION TESTS
// ============================================================================

TEST_F(IAudioBufferTest, ThreadedStrategy_AddFrames_WithNullBuffer_ReturnsFalse) {
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

TEST_F(IAudioBufferTest, SyncPullStrategy_Render_WithNullBuffer_ReturnsFalse) {
    // Arrange: null buffer
    bool result = syncPullStrategy->render(nullptr, TEST_FRAME_COUNT);

    // Assert: Should fail gracefully
    EXPECT_FALSE(result);
}

// ============================================================================
// STRATEGY-SPECIFIC METHOD TESTS
// ============================================================================

TEST_F(IAudioBufferTest, ThreadedStrategy_ShouldDrainDuringWarmup) {
    EXPECT_TRUE(threadedStrategy->shouldDrainDuringWarmup());
}

TEST_F(IAudioBufferTest, SyncPullStrategy_ShouldNotDrainDuringWarmup) {
    EXPECT_FALSE(syncPullStrategy->shouldDrainDuringWarmup());
}
