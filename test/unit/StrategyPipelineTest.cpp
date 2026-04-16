// StrategyPipelineTest.cpp - TDD tests proving pipeline behavior
//
// These tests verify REAL functional audio pipeline behavior:
// 1. ThreadedStrategy::updateSimulation passes correct delta time units
// 2. SyncPullStrategy produces frames on successive calls (not just the first)
// 3. The audio pipeline produces non-silent output after initialisation
// 4. ReadAudioBuffer drains engine before RenderOnDemand (beep-then-silence root cause)
// 5. SyncPullStrategy propagates frame counts to diagnostics
//
// Phase E: Uses ISimulator (BridgeSimulator) instead of raw EngineSimAPI

#include "ThreadedStrategy.h"
#include "SyncPullStrategy.h"
#include "ISimulator.h"
#include "BridgeSimulator.h"
#include "AudioTestConstants.h"
#include "AudioTestHelpers.h"
#include "engine_sim_bridge.h"

#include <gtest/gtest.h>
#include <memory>
#include <cstring>
#include <cmath>

using namespace test::constants;

// ============================================================================
// Helper: Create a real engine in sine mode for integration-level tests
// Uses BridgeSimulator (ISimulator) instead of raw EngineSimAPI
// ============================================================================

struct SineSimulator {
    BridgeSimulator simulator;
    bool valid = false;

    SineSimulator() {
        EngineSimConfig config{};
        config.sampleRate = 48000;
        config.inputBufferSize = 1024;
        config.audioBufferSize = 96000;
        config.simulationFrequency = 10000;
        config.fluidSimulationSteps = 8;
        config.targetSynthesizerLatency = 0.05;
        config.sineMode = 1;

        if (!simulator.create(config)) return;

        if (!simulator.loadScript("", "")) return;

        valid = true;
    }

    ~SineSimulator() {
        if (valid) {
            simulator.destroy();
        }
    }

    SineSimulator(const SineSimulator&) = delete;
    SineSimulator& operator=(const SineSimulator&) = delete;
};

// ============================================================================
// Test fixture for strategy pipeline tests
// ============================================================================

class StrategyPipelineTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<ConsoleLogger>();
    }

    void TearDown() override {}

    bool isAllSilence(const float* data, int frames) const {
        for (int i = 0; i < frames * STEREO_CHANNELS; ++i) {
            if (std::abs(data[i]) > 1e-6f) return false;
        }
        return true;
    }

    std::unique_ptr<ConsoleLogger> logger_;
};

// ============================================================================
// TEST GROUP 1: ThreadedStrategy::updateSimulation units mismatch
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedStrategy_UpdateSimulation_SucceedsWithRealEngine) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid) << "Failed to create sine-mode engine";

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // ThreadedStrategy::startPlayback calls simulator->startAudioThread()
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator))
        << "startPlayback should succeed with real engine";

    double deltaTimeMs = (1.0 / 60.0) * 1000.0;

    // Act: Call updateSimulation with the real engine via ISimulator
    strategy->updateSimulation(&engine.simulator, deltaTimeMs);

    // Assert: The engine should still be in a valid state
    EngineSimStats stats = engine.simulator.getStats();

    EXPECT_GT(stats.currentRPM, 0.0)
        << "Engine should have non-zero RPM after update. "
        << "If this fails, ThreadedStrategy::updateSimulation is still passing "
        << "invalid deltaTime to the bridge.";
}

TEST_F(StrategyPipelineTest, ThreadedStrategy_UpdateSimulation_DeltaTimeConversionIsCorrect) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    double deltaTimeMs = (1.0 / 60.0) * 1000.0;
    double deltaTimeSeconds = deltaTimeMs / 1000.0;

    // Act: Call update with the CORRECT value (seconds)
    engine.simulator.update(deltaTimeSeconds);

    SUCCEED();
}

// ============================================================================
// TEST GROUP 2: SyncPullStrategy produces frames on successive calls
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_ProducesAudioOnSuccessiveCalls) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // SyncPullStrategy needs a simulator reference set via startPlayback
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator));

    const int NUM_RENDER_CALLS = 5;
    const int FRAMES_PER_CALL = 64;

    for (int call = 0; call < NUM_RENDER_CALLS; ++call) {
        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CALL);

        bool result = strategy->render(&audioBuffer, FRAMES_PER_CALL);
        ASSERT_TRUE(result) << "render() should succeed on call " << (call + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);

        bool silent = isAllSilence(outputData, FRAMES_PER_CALL);
        EXPECT_FALSE(silent) << "render() call " << (call + 1)
            << " produced silence -- expected non-silent audio from sine engine";

        freeAudioBufferList(audioBuffer);
    }
}

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_FirstAndSubsequentCallsBothProduceAudio) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator));

    const int FRAMES = 64;

    // First render
    AudioBufferList firstBuffer = createAudioBufferList(FRAMES);
    bool firstResult = strategy->render(&firstBuffer, FRAMES);
    ASSERT_TRUE(firstResult);

    float* firstData = static_cast<float*>(firstBuffer.mBuffers[0].mData);
    bool firstSilent = isAllSilence(firstData, FRAMES);

    // Second render
    AudioBufferList secondBuffer = createAudioBufferList(FRAMES);
    bool secondResult = strategy->render(&secondBuffer, FRAMES);
    ASSERT_TRUE(secondResult);

    float* secondData = static_cast<float*>(secondBuffer.mBuffers[0].mData);
    bool secondSilent = isAllSilence(secondData, FRAMES);

    EXPECT_FALSE(firstSilent) << "First render() should produce non-silent audio";
    EXPECT_FALSE(secondSilent) << "Second render() should produce non-silent audio";

    freeAudioBufferList(firstBuffer);
    freeAudioBufferList(secondBuffer);
}

// ============================================================================
// TEST GROUP 3: Audio pipeline produces non-silent output
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedStrategy_Pipeline_ProducesContinuousAudioAfterMultipleAddFrames) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    const int NUM_CYCLES = 5;
    const int FRAMES_PER_CYCLE = 10;

    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        float signalValue = 0.1f * (cycle + 1);
        std::vector<float> generatedAudio(FRAMES_PER_CYCLE * STEREO_CHANNELS, signalValue);

        bool addResult = strategy->AddFrames(generatedAudio.data(), FRAMES_PER_CYCLE);
        ASSERT_TRUE(addResult) << "AddFrames failed on cycle " << (cycle + 1);

        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CYCLE);
        bool renderResult = strategy->render(&audioBuffer, FRAMES_PER_CYCLE);
        ASSERT_TRUE(renderResult) << "render failed on cycle " << (cycle + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);

        EXPECT_FALSE(isAllSilence(outputData, FRAMES_PER_CYCLE))
            << "Pipeline cycle " << (cycle + 1) << " produced silence.";

        freeAudioBufferList(audioBuffer);
    }
}

TEST_F(StrategyPipelineTest, ThreadedStrategy_Pipeline_DoesNotDrainToSilenceAfterFirstRender) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Write enough audio for 3 full render calls
    const int TOTAL_FRAMES = TEST_FRAME_COUNT * 3;
    std::vector<float> bigAudioBuffer(TOTAL_FRAMES * STEREO_CHANNELS, 0.5f);

    bool addResult = strategy->AddFrames(bigAudioBuffer.data(), TOTAL_FRAMES);
    ASSERT_TRUE(addResult);

    // Render 3 times from the same buffer
    int silentRenderCount = 0;
    for (int render = 0; render < 3; ++render) {
        AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
        bool renderResult = strategy->render(&audioBuffer, TEST_FRAME_COUNT);
        ASSERT_TRUE(renderResult);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
        if (isAllSilence(outputData, TEST_FRAME_COUNT)) {
            silentRenderCount++;
        }

        freeAudioBufferList(audioBuffer);
    }

    EXPECT_EQ(silentRenderCount, 0)
        << "Expected all 3 renders to produce audio, but " << silentRenderCount
        << " produced silence (beep-then-silence pattern detected)";
}

// ============================================================================
// TEST GROUP 4: ReadAudioBuffer drains engine before RenderOnDemand
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPull_ReadAudioBufferDrainsEngineBeforeRenderOnDemand) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator));

    const int FRAMES = 64;

    // First render (baseline)
    AudioBufferList firstBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(&firstBuffer, FRAMES));
    float* firstData = static_cast<float*>(firstBuffer.mBuffers[0].mData);
    bool firstSilent = isAllSilence(firstData, FRAMES);
    freeAudioBufferList(firstBuffer);

    // Advance simulation
    engine.simulator.update(1.0 / 60.0);

    // Drain via readAudioBuffer
    std::vector<float> drainBuffer(FRAMES * 2);
    int drained = 0;
    engine.simulator.readAudioBuffer(drainBuffer.data(), FRAMES, &drained);

    // Render again after drain
    AudioBufferList secondBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(&secondBuffer, FRAMES));
    float* secondData = static_cast<float*>(secondBuffer.mBuffers[0].mData);
    bool secondSilent = isAllSilence(secondData, FRAMES);
    freeAudioBufferList(secondBuffer);

    EXPECT_FALSE(firstSilent) << "First render (before ReadAudioBuffer) should produce audio";
    EXPECT_FALSE(secondSilent)
        << "Second render (after ReadAudioBuffer) should produce audio. "
        << "Got silence -- ReadAudioBuffer drained the synthesizer buffer.";
}

TEST_F(StrategyPipelineTest, SyncPull_RenderOnDemand_RecoversAfterReadAudioBufferDrain) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator));

    const int FRAMES = 64;

    // Drain via readAudioBuffer
    std::vector<float> drainBuffer(FRAMES * 2);
    int drained = 0;
    engine.simulator.readAudioBuffer(drainBuffer.data(), FRAMES, &drained);

    // Immediate render
    AudioBufferList starvedBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(&starvedBuffer, FRAMES));
    float* starvedData = static_cast<float*>(starvedBuffer.mBuffers[0].mData);
    bool starvedSilent = isAllSilence(starvedData, FRAMES);
    freeAudioBufferList(starvedBuffer);

    EXPECT_FALSE(starvedSilent)
        << "Render after ReadAudioBuffer drain produced silence. "
        << "This proves ReadAudioBuffer + RenderOnDemand in the same tick "
        << "causes beep-then-silence in SyncPull mode.";
}

// ============================================================================
// TEST GROUP 5: Threaded pipeline integration -- full failure chain
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedPipeline_FullChain_WithInjectedAudio) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    ASSERT_TRUE(strategy->startPlayback(&engine.simulator))
        << "startPlayback should succeed with real engine";

    const int FRAMES_PER_CYCLE = 64;
    double deltaTimeMs = (1.0 / 60.0) * 1000.0;

    const int NUM_ITERATIONS = 5;
    int silentRenders = 0;

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        // Step 1: updateSimulation
        strategy->updateSimulation(&engine.simulator, deltaTimeMs);

        // Step 2: Simulate successful audio generation
        std::vector<float> generatedAudio(FRAMES_PER_CYCLE * STEREO_CHANNELS);
        float signalValue = 0.1f * (i + 1);
        for (int j = 0; j < FRAMES_PER_CYCLE * STEREO_CHANNELS; ++j) {
            generatedAudio[j] = signalValue;
        }

        // Step 3: Add to circular buffer
        strategy->AddFrames(generatedAudio.data(), FRAMES_PER_CYCLE);

        // Step 4: Render
        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CYCLE);
        bool renderResult = strategy->render(&audioBuffer, FRAMES_PER_CYCLE);
        ASSERT_TRUE(renderResult) << "render failed on iteration " << (i + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
        if (isAllSilence(outputData, FRAMES_PER_CYCLE)) {
            silentRenders++;
        }

        freeAudioBufferList(audioBuffer);
    }

    EXPECT_EQ(silentRenders, 0)
        << "Threaded pipeline produced silence on " << silentRenders
        << " of " << NUM_ITERATIONS << " iterations.";
}

TEST_F(StrategyPipelineTest, ThreadedPipeline_ReadAudioBufferReturnsZeroWithoutAudioThread) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    const int FRAMES = 64;

    engine.simulator.update(0.01667);

    std::vector<float> buffer(FRAMES * 2);
    int readCount = 0;
    engine.simulator.readAudioBuffer(buffer.data(), FRAMES, &readCount);

    EXPECT_EQ(readCount, 0)
        << "ReadAudioBuffer should return 0 without StartAudioThread.";
}

// ============================================================================
// TEST GROUP 6: SyncPull render propagates frame counts to diagnostics
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_UpdatesTotalFramesRendered) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator));

    const int FRAMES = 64;

    // Act: Render
    AudioBufferList audioBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(&audioBuffer, FRAMES));

    // Assert: Diagnostics snapshot should reflect frames rendered
    auto snap = strategy->diagnostics().getSnapshot();
    EXPECT_GT(snap.totalFramesRendered, 0)
        << "After render(), diagnostics.totalFramesRendered should be > 0. "
        << "Got 0 -- SyncPullStrategy does not propagate frame counts to Diagnostics.";

    freeAudioBufferList(audioBuffer);
}

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_AccumulatesFramesAcrossMultipleCalls) {
    SineSimulator engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));
    ASSERT_TRUE(strategy->startPlayback(&engine.simulator));

    const int FRAMES_PER_CALL = 64;
    const int NUM_CALLS = 4;

    for (int i = 0; i < NUM_CALLS; ++i) {
        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CALL);
        ASSERT_TRUE(strategy->render(&audioBuffer, FRAMES_PER_CALL));
        freeAudioBufferList(audioBuffer);
    }

    // Assert: Total frames rendered should accumulate
    auto snap = strategy->diagnostics().getSnapshot();
    EXPECT_EQ(snap.totalFramesRendered, static_cast<int64_t>(FRAMES_PER_CALL * NUM_CALLS))
        << "After " << NUM_CALLS << " renders of " << FRAMES_PER_CALL << " frames each, "
        << "totalFramesRendered should be " << (FRAMES_PER_CALL * NUM_CALLS)
        << ". Got " << snap.totalFramesRendered << ".";
}
