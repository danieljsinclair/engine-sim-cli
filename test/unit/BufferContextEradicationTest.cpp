// BufferContextEradicationTest.cpp - TDD RED-phase tests for Phase B
//
// Purpose: Assert that strategies work WITHOUT BufferContext.
// Phase B will:
//   1. Each strategy owns its own AudioState and Diagnostics as private members
//   2. ThreadedStrategy owns its own CircularBuffer (not passed via BufferContext)
//   3. SyncPullStrategy owns its own state
//   4. An AudioStrategyBase abstract class provides DRY shared code
//   5. BufferContext* is removed from ALL IAudioStrategy method signatures
//   6. BufferContext.h is deleted
//
// RED PHASE: These tests describe the TARGET interface (no BufferContext).
// IAudioStrategy.h has already been updated (BufferContext* removed).
// The concrete strategy implementations (ThreadedStrategy, SyncPullStrategy)
// still have the old signatures, so the build is RED.
// The tech-architect makes these GREEN by updating concrete implementations.

#include <gtest/gtest.h>
#include <memory>
#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>

#include "audio/strategies/IAudioStrategy.h"
#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"
#include "AudioTestConstants.h"

using namespace test::constants;

// ============================================================================
// Test Fixture -- strategies without BufferContext
// ============================================================================

class BufferContextEradicationTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<ConsoleLogger>();
    }

    void TearDown() override {}

    // Helper: Create AudioBufferList for testing
    AudioBufferList createAudioBufferList(UInt32 frames) {
        AudioBufferList bufferList;
        bufferList.mNumberBuffers = 1;
        bufferList.mBuffers[0].mNumberChannels = STEREO_CHANNELS;
        bufferList.mBuffers[0].mDataByteSize = frames * STEREO_CHANNELS * sizeof(float);
        bufferList.mBuffers[0].mData = new float[frames * STEREO_CHANNELS]();
        return bufferList;
    }

    void freeAudioBufferList(AudioBufferList& bufferList) {
        if (bufferList.mBuffers[0].mData) {
            delete[] static_cast<float*>(bufferList.mBuffers[0].mData);
            bufferList.mBuffers[0].mData = nullptr;
        }
    }

    std::unique_ptr<ConsoleLogger> logger_;
};

// ============================================================================
// GROUP 1: ThreadedStrategy can be created and used WITHOUT BufferContext
//
// TARGET: ThreadedStrategy constructor and initialize() take NO BufferContext.
// The strategy owns its own CircularBuffer, AudioState, and Diagnostics.
// ============================================================================

TEST_F(BufferContextEradicationTest, ThreadedStrategy_InitializesWithoutBufferContext) {
    // Arrange: Create strategy (no BufferContext needed)
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    // Act: Initialize with config only (no BufferContext parameter)
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    bool result = strategy->initialize(config);

    // Assert: Should succeed -- strategy owns its own state
    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_ReportsNotPlayingAfterInit) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Assert: Not playing until startPlayback is called
    EXPECT_FALSE(strategy->isPlaying());
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_RenderWithoutBufferContext) {
    // Arrange: Create, initialize, and render -- no BufferContext anywhere
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: render takes no BufferContext -- strategy uses its own internal state
    bool result = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Should succeed (may output silence if buffer empty, but not crash)
    EXPECT_TRUE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_AddFramesWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: AddFrames takes no BufferContext -- strategy uses its own CircularBuffer
    bool result = strategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);

    // Assert: Should succeed -- strategy owns its own buffer
    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_FillBufferFromEngineWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: fillBufferFromEngine takes no BufferContext
    // (Requires engine API; passing null handle/API is a valid no-op test)
    EngineSimAPI nullApi{};
    strategy->fillBufferFromEngine(nullptr, nullApi, 512);

    // Assert: No crash -- strategy manages its own state
    SUCCEED();
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_UpdateSimulationWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EngineSimAPI nullApi{};

    // Act: updateSimulation takes no BufferContext
    strategy->updateSimulation(nullptr, nullApi, 16.667);

    // Assert: No crash
    SUCCEED();
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_PrepareBufferWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: prepareBuffer takes no BufferContext
    strategy->prepareBuffer();

    // Assert: No crash
    SUCCEED();
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_ResetBufferAfterWarmupWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: resetBufferAfterWarmup takes no BufferContext
    strategy->resetBufferAfterWarmup();

    // Assert: No crash
    SUCCEED();
}

// ============================================================================
// GROUP 2: SyncPullStrategy can be created and used WITHOUT BufferContext
//
// TARGET: SyncPullStrategy owns its own state, no BufferContext needed.
// ============================================================================

TEST_F(BufferContextEradicationTest, SyncPullStrategy_InitializesWithoutBufferContext) {
    // Arrange: Create strategy (no BufferContext needed)
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    // Act: Initialize with config only (no BufferContext parameter)
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    bool result = strategy->initialize(config);

    // Assert: Should succeed -- strategy owns its own state
    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_ReportsNotPlayingAfterInit) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Assert: Not playing until startPlayback is called
    EXPECT_FALSE(strategy->isPlaying());
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_RenderWithoutBufferContext) {
    // Arrange: Create and initialize -- no BufferContext
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: render takes no BufferContext
    // Without engine API, this should return false (graceful failure)
    bool result = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Should fail gracefully (no engine connected)
    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_AddFramesWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);

    // Act: AddFrames takes no BufferContext
    bool result = strategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);

    // Assert: SyncPull AddFrames is a no-op, should return true
    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_FillBufferFromEngineWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EngineSimAPI nullApi{};

    // Act: fillBufferFromEngine takes no BufferContext (no-op for SyncPull)
    strategy->fillBufferFromEngine(nullptr, nullApi, 512);

    // Assert: No crash
    SUCCEED();
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_UpdateSimulationWithoutBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EngineSimAPI nullApi{};

    // Act: updateSimulation takes no BufferContext (no-op for SyncPull)
    strategy->updateSimulation(nullptr, nullApi, 16.667);

    // Assert: No crash
    SUCCEED();
}

// ============================================================================
// GROUP 3: AudioStrategyBase provides shared functionality
//
// TARGET: Both strategies inherit from AudioStrategyBase which provides
// isPlaying() and other shared getters.
// These tests verify the interface contract via the strategy objects.
// ============================================================================

TEST_F(BufferContextEradicationTest, ThreadedStrategy_BaseClassGettersWork) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = 44100;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Assert: Interface getters work
    EXPECT_FALSE(strategy->isPlaying());
    EXPECT_STREQ(strategy->getName(), "Threaded");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_BaseClassGettersWork) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = 48000;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Assert: Interface getters work
    EXPECT_FALSE(strategy->isPlaying());
    EXPECT_STREQ(strategy->getName(), "SyncPull");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_IsPlayingChangesOnStartStop) {
    // Arrange: Create and initialize SyncPullStrategy
    // (SyncPullStrategy doesn't require engine handle/API for startPlayback)
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Assert: Not playing initially
    EXPECT_FALSE(strategy->isPlaying());

    // Act: Start playback (SyncPullStrategy doesn't require valid engine handle)
    EngineSimAPI nullApi{};
    strategy->startPlayback(nullptr, &nullApi);

    // Assert: Now playing
    EXPECT_TRUE(strategy->isPlaying());

    // Act: Stop playback
    strategy->stopPlayback(nullptr, &nullApi);

    // Assert: No longer playing
    EXPECT_FALSE(strategy->isPlaying());
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_StartPlaybackRequiresEngineHandle) {
    // Arrange: ThreadedStrategy requires valid engine handle and API for startPlayback
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Assert: Not playing initially
    EXPECT_FALSE(strategy->isPlaying());

    // Act: Start playback with null handle/api -- should fail validation
    EngineSimAPI nullApi{};
    bool result = strategy->startPlayback(nullptr, &nullApi);

    // Assert: startPlayback should return false (requires valid handle)
    EXPECT_FALSE(result);
    EXPECT_FALSE(strategy->isPlaying());

    // Act: Stop playback should still work (just sets isPlaying=false)
    strategy->stopPlayback(nullptr, &nullApi);
    EXPECT_FALSE(strategy->isPlaying());
}

// ============================================================================
// GROUP 4: Full pipeline works without BufferContext
//
// TARGET: End-to-end workflow -- init, add frames, render, verify output.
// No BufferContext involved at any point.
// ============================================================================

TEST_F(BufferContextEradicationTest, ThreadedStrategy_FullPipeline_NoBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: Add frames to strategy's internal buffer
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    ASSERT_TRUE(strategy->AddFrames(input.data(), TEST_FRAME_COUNT));

    // Act: Render from strategy's internal buffer
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    bool renderResult = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render should succeed
    EXPECT_TRUE(renderResult);

    // Assert: Output should match input
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < TEST_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], TEST_SIGNAL_VALUE_1)
            << "Sample mismatch at index " << i;
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_MultipleRenderCycles_NoBufferContext) {
    // Arrange: Create and initialize
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    const int NUM_CYCLES = 3;

    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        // Generate unique signal per cycle
        float signalValue = 0.1f * (cycle + 1);
        std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, signalValue);
        ASSERT_TRUE(strategy->AddFrames(input.data(), TEST_FRAME_COUNT));

        AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
        bool renderResult = strategy->render(&audioBuffer, TEST_FRAME_COUNT);
        ASSERT_TRUE(renderResult) << "Render failed on cycle " << (cycle + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
        for (int i = 0; i < TEST_FRAME_COUNT * STEREO_CHANNELS; ++i) {
            EXPECT_FLOAT_EQ(outputData[i], signalValue)
                << "Cycle " << (cycle + 1) << " sample mismatch at index " << i;
        }

        freeAudioBufferList(audioBuffer);
    }
}

// ============================================================================
// GROUP 5: Factory creates strategies that don't need BufferContext
//
// TARGET: Factory-created strategies can be initialized without BufferContext.
// ============================================================================

TEST_F(BufferContextEradicationTest, Factory_CreatesThreadedStrategy_InitWithoutBufferContext) {
    // Act: Create via factory
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, logger_.get());
    ASSERT_NE(strategy, nullptr);

    // Initialize without BufferContext
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    EXPECT_TRUE(strategy->initialize(config));
}

TEST_F(BufferContextEradicationTest, Factory_CreatesSyncPullStrategy_InitWithoutBufferContext) {
    // Act: Create via factory
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, logger_.get());
    ASSERT_NE(strategy, nullptr);

    // Initialize without BufferContext
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    EXPECT_TRUE(strategy->initialize(config));
}
