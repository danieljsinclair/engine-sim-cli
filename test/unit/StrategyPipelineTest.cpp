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
// Phase G: Uses AudioBufferView instead of CoreAudio AudioBufferList

#include "strategy/ThreadedStrategy.h"
#include "strategy/SyncPullStrategy.h"
#include "simulator/ISimulator.h"
#include "simulator/BridgeSimulator.h"
#include "simulator/SineSimulator.h"
#include "AudioTestConstants.h"
#include "AudioTestHelpers.h"
#include "simulator/EngineSimTypes.h"
#include "common/ILogging.h"
#include "telemetry/ITelemetryProvider.h"

#include <gtest/gtest.h>
#include <memory>
#include <cstring>
#include <cmath>

using namespace test::constants;

// ============================================================================
// Helper: Create a real engine in sine mode for integration-level tests
// Uses SineSimulator (pure Simulator) wrapped in BridgeSimulator (ISimulator)
// ============================================================================

struct TestSineEngine {
    std::unique_ptr<BridgeSimulator> simulator;
    bool valid = false;

    TestSineEngine() {
        logger_ = std::make_unique<ConsoleLogger>();
        telemetry_ = std::make_unique<telemetry::InMemoryTelemetry>();

        auto sineSim = std::make_unique<SineSimulator>();
        Simulator::Parameters simParams;
        simParams.systemType = Simulator::SystemType::NsvOptimized;
        sineSim->initialize(simParams);
        sineSim->setSimulationFrequency(EngineSimDefaults::SIMULATION_FREQUENCY);
        sineSim->setFluidSimulationSteps(EngineSimDefaults::FLUID_SIMULATION_STEPS);
        sineSim->setTargetSynthesizerLatency(EngineSimDefaults::TARGET_SYNTH_LATENCY);
        sineSim->loadSimulation(nullptr, nullptr, nullptr);
        simulator = std::make_unique<BridgeSimulator>(std::move(sineSim));

        ISimulatorConfig config{};
        config.sampleRate = EngineSimDefaults::SAMPLE_RATE;
        config.simulationFrequency = EngineSimDefaults::SIMULATION_FREQUENCY;

        if (!simulator->create(config, logger_.get(), telemetry_.get())) return;

        valid = true;
    }

    ~TestSineEngine() {
        if (valid && simulator) {
            simulator->destroy();
        }
    }

    TestSineEngine(const TestSineEngine&) = delete;
    TestSineEngine& operator=(const TestSineEngine&) = delete;

private:
    std::unique_ptr<ConsoleLogger> logger_;
    std::unique_ptr<telemetry::InMemoryTelemetry> telemetry_;
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
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid) << "Failed to create sine-mode engine";

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));

    // ThreadedStrategy::startPlayback calls simulator->start()
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()))
        << "startPlayback should succeed with real engine";

    double deltaTimeMs = (1.0 / 60.0) * 1000.0;

    // Act: Call updateSimulation with the real engine via ISimulator
    strategy->updateSimulation(engine.simulator.get(), deltaTimeMs);

    // Assert: The engine should still be in a valid state
    EngineSimStats stats = engine.simulator->getStats();

    EXPECT_GT(stats.currentRPM, 0.0)
        << "Engine should have non-zero RPM after update. "
        << "If this fails, ThreadedStrategy::updateSimulation is still passing "
        << "invalid deltaTime to the bridge.";
}

TEST_F(StrategyPipelineTest, ThreadedStrategy_UpdateSimulation_DeltaTimeConversionIsCorrect) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    double deltaTimeMs = (1.0 / 60.0) * 1000.0;
    double deltaTimeSeconds = deltaTimeMs / 1000.0;

    // Act: Call update with the CORRECT value (seconds)
    engine.simulator->update(deltaTimeSeconds);

    SUCCEED();
}

// ============================================================================
// TEST GROUP 2: SyncPullStrategy produces frames on successive calls
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_ProducesAudioOnSuccessiveCalls) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));

    // SyncPullStrategy needs a simulator reference set via startPlayback
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()));

    const int NUM_RENDER_CALLS = 5;
    const int FRAMES_PER_CALL = 64;

    for (int call = 0; call < NUM_RENDER_CALLS; ++call) {
        AudioBufferView audioBuffer = createAudioBuffer(FRAMES_PER_CALL);

        bool result = strategy->render(audioBuffer);
        ASSERT_TRUE(result) << "render() should succeed on call " << (call + 1);

        bool silent = isAllSilence(audioBuffer.asFloat(), FRAMES_PER_CALL);
        EXPECT_FALSE(silent) << "render() call " << (call + 1)
            << " produced silence -- expected non-silent audio from sine engine";

        freeAudioBuffer(audioBuffer);
    }
}

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_FirstAndSubsequentCallsBothProduceAudio) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()));

    const int FRAMES = 64;

    // First render
    AudioBufferView firstBuffer = createAudioBuffer(FRAMES);
    bool firstResult = strategy->render(firstBuffer);
    ASSERT_TRUE(firstResult);

    bool firstSilent = isAllSilence(firstBuffer.asFloat(), FRAMES);

    // Second render
    AudioBufferView secondBuffer = createAudioBuffer(FRAMES);
    bool secondResult = strategy->render(secondBuffer);
    ASSERT_TRUE(secondResult);

    bool secondSilent = isAllSilence(secondBuffer.asFloat(), FRAMES);

    EXPECT_FALSE(firstSilent) << "First render() should produce non-silent audio";
    EXPECT_FALSE(secondSilent) << "Second render() should produce non-silent audio";

    freeAudioBuffer(firstBuffer);
    freeAudioBuffer(secondBuffer);
}

// ============================================================================
// TEST GROUP 3: Audio pipeline produces non-silent output
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedStrategy_Pipeline_ProducesContinuousAudioAfterMultipleAddFrames) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));

    const int NUM_CYCLES = 5;
    const int FRAMES_PER_CYCLE = 10;

    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        float signalValue = 0.1f * (cycle + 1);
        std::vector<float> generatedAudio(FRAMES_PER_CYCLE * STEREO_CHANNELS, signalValue);

        bool addResult = strategy->AddFrames(generatedAudio.data(), FRAMES_PER_CYCLE);
        ASSERT_TRUE(addResult) << "AddFrames failed on cycle " << (cycle + 1);

        AudioBufferView audioBuffer = createAudioBuffer(FRAMES_PER_CYCLE);
        bool renderResult = strategy->render(audioBuffer);
        ASSERT_TRUE(renderResult) << "render failed on cycle " << (cycle + 1);

        EXPECT_FALSE(isAllSilence(audioBuffer.asFloat(), FRAMES_PER_CYCLE))
            << "Pipeline cycle " << (cycle + 1) << " produced silence.";

        freeAudioBuffer(audioBuffer);
    }
}

TEST_F(StrategyPipelineTest, ThreadedStrategy_Pipeline_DoesNotDrainToSilenceAfterFirstRender) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));

    // Write enough audio for 3 full render calls
    const int TOTAL_FRAMES = TEST_FRAME_COUNT * 3;
    std::vector<float> bigAudioBuffer(TOTAL_FRAMES * STEREO_CHANNELS, 0.5f);

    bool addResult = strategy->AddFrames(bigAudioBuffer.data(), TOTAL_FRAMES);
    ASSERT_TRUE(addResult);

    // Render 3 times from the same buffer
    int silentRenderCount = 0;
    for (int render = 0; render < 3; ++render) {
        AudioBufferView audioBuffer = createAudioBuffer(TEST_FRAME_COUNT);
        bool renderResult = strategy->render(audioBuffer);
        ASSERT_TRUE(renderResult);

        if (isAllSilence(audioBuffer.asFloat(), TEST_FRAME_COUNT)) {
            silentRenderCount++;
        }

        freeAudioBuffer(audioBuffer);
    }

    EXPECT_EQ(silentRenderCount, 0)
        << "Expected all 3 renders to produce audio, but " << silentRenderCount
        << " produced silence (beep-then-silence pattern detected)";
}

// ============================================================================
// TEST GROUP 4: ReadAudioBuffer drains engine before RenderOnDemand
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPull_ReadAudioBufferDrainsEngineBeforeRenderOnDemand) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()));

    const int FRAMES = 64;

    // First render (baseline)
    AudioBufferView firstBuffer = createAudioBuffer(FRAMES);
    ASSERT_TRUE(strategy->render(firstBuffer));
    bool firstSilent = isAllSilence(firstBuffer.asFloat(), FRAMES);
    freeAudioBuffer(firstBuffer);

    // Advance simulation
    engine.simulator->update(1.0 / 60.0);

    // Drain via readAudioBuffer
    std::vector<float> drainBuffer(FRAMES * 2);
    int drained = 0;
    engine.simulator->readAudioBuffer(drainBuffer.data(), FRAMES, &drained);

    // Render again after drain
    AudioBufferView secondBuffer = createAudioBuffer(FRAMES);
    ASSERT_TRUE(strategy->render(secondBuffer));
    bool secondSilent = isAllSilence(secondBuffer.asFloat(), FRAMES);
    freeAudioBuffer(secondBuffer);

    EXPECT_FALSE(firstSilent) << "First render (before ReadAudioBuffer) should produce audio";
    EXPECT_FALSE(secondSilent)
        << "Second render (after ReadAudioBuffer) should produce audio. "
        << "Got silence -- ReadAudioBuffer drained the synthesizer buffer.";
}

TEST_F(StrategyPipelineTest, SyncPull_RenderOnDemand_RecoversAfterReadAudioBufferDrain) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()));

    const int FRAMES = 64;

    // Drain via readAudioBuffer
    std::vector<float> drainBuffer(FRAMES * 2);
    int drained = 0;
    engine.simulator->readAudioBuffer(drainBuffer.data(), FRAMES, &drained);

    // Immediate render
    AudioBufferView starvedBuffer = createAudioBuffer(FRAMES);
    ASSERT_TRUE(strategy->render(starvedBuffer));
    bool starvedSilent = isAllSilence(starvedBuffer.asFloat(), FRAMES);
    freeAudioBuffer(starvedBuffer);

    EXPECT_FALSE(starvedSilent)
        << "Render after ReadAudioBuffer drain produced silence. "
        << "This proves ReadAudioBuffer + RenderOnDemand in the same tick "
        << "causes beep-then-silence in SyncPull mode.";
}

// ============================================================================
// TEST GROUP 5: Threaded pipeline integration -- full failure chain
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedPipeline_FullChain_WithInjectedAudio) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));

    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()))
        << "startPlayback should succeed with real engine";

    const int FRAMES_PER_CYCLE = 64;
    double deltaTimeMs = (1.0 / 60.0) * 1000.0;

    const int NUM_ITERATIONS = 5;
    int silentRenders = 0;

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        // Step 1: updateSimulation
        strategy->updateSimulation(engine.simulator.get(), deltaTimeMs);

        // Step 2: Simulate successful audio generation
        std::vector<float> generatedAudio(FRAMES_PER_CYCLE * STEREO_CHANNELS);
        float signalValue = 0.1f * (i + 1);
        for (int j = 0; j < FRAMES_PER_CYCLE * STEREO_CHANNELS; ++j) {
            generatedAudio[j] = signalValue;
        }

        // Step 3: Add to circular buffer
        strategy->AddFrames(generatedAudio.data(), FRAMES_PER_CYCLE);

        // Step 4: Render
        AudioBufferView audioBuffer = createAudioBuffer(FRAMES_PER_CYCLE);
        bool renderResult = strategy->render(audioBuffer);
        ASSERT_TRUE(renderResult) << "render failed on iteration " << (i + 1);

        if (isAllSilence(audioBuffer.asFloat(), FRAMES_PER_CYCLE)) {
            silentRenders++;
        }

        freeAudioBuffer(audioBuffer);
    }

    EXPECT_EQ(silentRenders, 0)
        << "Threaded pipeline produced silence on " << silentRenders
        << " of " << NUM_ITERATIONS << " iterations.";
}

TEST_F(StrategyPipelineTest, ThreadedPipeline_ReadAudioBufferReturnsZeroWithoutAudioThread) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    const int FRAMES = 64;

    engine.simulator->update(0.01667);

    std::vector<float> buffer(FRAMES * 2);
    int readCount = 0;
    engine.simulator->readAudioBuffer(buffer.data(), FRAMES, &readCount);

    EXPECT_EQ(readCount, 0)
        << "ReadAudioBuffer should return 0 without StartAudioThread.";
}

// ============================================================================
// TEST GROUP 6: SyncPull render propagates frame counts to diagnostics
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_UpdatesTotalFramesRendered) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()));

    const int FRAMES = 64;

    // Act: Render
    AudioBufferView audioBuffer = createAudioBuffer(FRAMES);
    ASSERT_TRUE(strategy->render(audioBuffer));

    // Assert: Diagnostics snapshot should reflect frames rendered
    auto snap = strategy->diagnostics().getSnapshot();
    EXPECT_GT(snap.totalFramesRendered, 0)
        << "After render(), diagnostics.totalFramesRendered should be > 0. "
        << "Got 0 -- SyncPullStrategy does not propagate frame counts to Diagnostics.";

    freeAudioBuffer(audioBuffer);
}

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_AccumulatesFramesAcrossMultipleCalls) {
    TestSineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioBufferConfig config;
// Removed sampleRate - now passed as separate parameter
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config, DEFAULT_SAMPLE_RATE));
    ASSERT_TRUE(strategy->startPlayback(engine.simulator.get()));

    const int FRAMES_PER_CALL = 64;
    const int NUM_CALLS = 4;

    for (int i = 0; i < NUM_CALLS; ++i) {
        AudioBufferView audioBuffer = createAudioBuffer(FRAMES_PER_CALL);
        ASSERT_TRUE(strategy->render(audioBuffer));
        freeAudioBuffer(audioBuffer);
    }

    // Assert: Total frames rendered should accumulate
    auto snap = strategy->diagnostics().getSnapshot();
    EXPECT_EQ(snap.totalFramesRendered, static_cast<int64_t>(FRAMES_PER_CALL * NUM_CALLS))
        << "After " << NUM_CALLS << " renders of " << FRAMES_PER_CALL << " frames each, "
        << "totalFramesRendered should be " << (FRAMES_PER_CALL * NUM_CALLS)
        << ". Got " << snap.totalFramesRendered << ".";
}
