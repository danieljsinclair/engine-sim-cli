// BufferContextEradicationTest.cpp - TDD tests for Phase B (BufferContext removal)
// Updated for Phase E: Uses ISimulator* instead of EngineSimHandle/EngineSimAPI&
//
// Purpose: Assert that strategies work WITHOUT BufferContext.
// Phase B: Each strategy owns its own AudioState and Diagnostics.
// Phase E: Strategies take ISimulator* instead of raw engine types.

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
// ============================================================================

TEST_F(BufferContextEradicationTest, ThreadedStrategy_InitializesWithoutBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    bool result = strategy->initialize(config);

    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_ReportsNotPlayingAfterInit) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EXPECT_FALSE(strategy->isPlaying());
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_RenderWithoutBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    bool result = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    EXPECT_TRUE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_AddFramesWithoutBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    bool result = strategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);

    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_FillBufferFromEngineWithNullSimulator) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: fillBufferFromEngine with null simulator is a safe no-op
    strategy->fillBufferFromEngine(nullptr, 512);

    SUCCEED();
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_UpdateSimulationWithNullSimulator) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: updateSimulation with null simulator is a safe no-op
    strategy->updateSimulation(nullptr, 16.667);

    SUCCEED();
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_PrepareBufferWithoutBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    strategy->prepareBuffer();

    SUCCEED();
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_ResetBufferAfterWarmupWithoutBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    strategy->resetBufferAfterWarmup();

    SUCCEED();
}

// ============================================================================
// GROUP 2: SyncPullStrategy can be created and used WITHOUT BufferContext
// ============================================================================

TEST_F(BufferContextEradicationTest, SyncPullStrategy_InitializesWithoutBufferContext) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    bool result = strategy->initialize(config);

    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_ReportsNotPlayingAfterInit) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EXPECT_FALSE(strategy->isPlaying());
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_RenderWithoutSimulator) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Act: render without simulator set should return false (graceful failure)
    bool result = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    EXPECT_FALSE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_AddFramesWithoutBufferContext) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);

    bool result = strategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);

    EXPECT_TRUE(result);
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_FillBufferFromEngineWithNullSimulator) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: fillBufferFromEngine is a no-op for SyncPull
    strategy->fillBufferFromEngine(nullptr, 512);

    SUCCEED();
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_UpdateSimulationWithNullSimulator) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: updateSimulation is a no-op for SyncPull
    strategy->updateSimulation(nullptr, 16.667);

    SUCCEED();
}

// ============================================================================
// GROUP 3: Interface contract tests
// ============================================================================

TEST_F(BufferContextEradicationTest, ThreadedStrategy_BaseClassGettersWork) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = 44100;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EXPECT_FALSE(strategy->isPlaying());
    EXPECT_STREQ(strategy->getName(), "Threaded");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_BaseClassGettersWork) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = 48000;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EXPECT_FALSE(strategy->isPlaying());
    EXPECT_STREQ(strategy->getName(), "SyncPull");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(BufferContextEradicationTest, SyncPullStrategy_IsPlayingChangesOnStartStop) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EXPECT_FALSE(strategy->isPlaying());

    // Act: Start playback (SyncPullStrategy doesn't require valid simulator)
    strategy->startPlayback(nullptr);

    EXPECT_TRUE(strategy->isPlaying());

    // Act: Stop playback
    strategy->stopPlayback(nullptr);

    EXPECT_FALSE(strategy->isPlaying());
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_StartPlaybackRequiresSimulator) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    EXPECT_FALSE(strategy->isPlaying());

    // Act: Start playback with null simulator -- should fail validation
    bool result = strategy->startPlayback(nullptr);

    EXPECT_FALSE(result);
    EXPECT_FALSE(strategy->isPlaying());

    // Act: Stop playback should still work
    strategy->stopPlayback(nullptr);
    EXPECT_FALSE(strategy->isPlaying());
}

// ============================================================================
// GROUP 4: Full pipeline works without BufferContext
// ============================================================================

TEST_F(BufferContextEradicationTest, ThreadedStrategy_FullPipeline_NoBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    ASSERT_TRUE(strategy->AddFrames(input.data(), TEST_FRAME_COUNT));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    bool renderResult = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    EXPECT_TRUE(renderResult);

    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < TEST_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(outputData[i], TEST_SIGNAL_VALUE_1)
            << "Sample mismatch at index " << i;
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(BufferContextEradicationTest, ThreadedStrategy_MultipleRenderCycles_NoBufferContext) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    const int NUM_CYCLES = 3;

    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
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
// ============================================================================

TEST_F(BufferContextEradicationTest, Factory_CreatesThreadedStrategy_InitWithoutBufferContext) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, logger_.get());
    ASSERT_NE(strategy, nullptr);

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    EXPECT_TRUE(strategy->initialize(config));
}

TEST_F(BufferContextEradicationTest, Factory_CreatesSyncPullStrategy_InitWithoutBufferContext) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, logger_.get());
    ASSERT_NE(strategy, nullptr);

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    EXPECT_TRUE(strategy->initialize(config));
}
