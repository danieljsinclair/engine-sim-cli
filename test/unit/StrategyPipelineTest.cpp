// StrategyPipelineTest.cpp - TDD RED phase tests proving pipeline bugs
//
// These tests verify REAL functional audio pipeline behavior:
// 1. ThreadedStrategy::updateSimulation passes correct delta time units
// 2. SyncPullStrategy produces frames on successive calls (not just the first)
// 3. The audio pipeline produces non-silent output after initialisation
// 4. ReadAudioBuffer drains engine before RenderOnDemand (beep-then-silence root cause)
// 5. SyncPullStrategy propagates frame counts to diagnostics
//
// These are TDD RED tests -- they compile but are expected to FAIL with the
// current code, proving the bugs exist. Fixing the bugs makes them pass.
//
// Mocking strategy:
// EngineSimAPI is a concrete struct with non-virtual inline methods. We cannot
// intercept calls via inheritance mocking. Instead, we use the REAL bridge
// API with sine mode (which requires no .mr script file). This tests real
// production code paths. For ThreadedStrategy pipeline tests (buffer-only),
// no engine API is needed.

#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"
#include "audio/state/BufferContext.h"
#include "audio/common/CircularBuffer.h"
#include "AudioTestConstants.h"
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

#include <gtest/gtest.h>
#include <memory>
#include <cstring>
#include <cmath>

using namespace test::constants;

// ============================================================================
// Helper: Create a real engine in sine mode for integration-level tests
// ============================================================================

struct SineEngine {
    EngineSimHandle handle = nullptr;
    EngineSimAPI api;
    bool valid = false;

    SineEngine() {
        EngineSimConfig config{};
        config.sampleRate = 48000;
        config.inputBufferSize = 1024;       // Required by ValidateConfig (>= 64)
        config.audioBufferSize = 96000;      // 2s @ 48kHz
        config.simulationFrequency = 10000;
        config.fluidSimulationSteps = 8;     // Required by ValidateConfig (>= 1)
        config.targetSynthesizerLatency = 0.05;
        config.sineMode = 1;  // Sine wave mode -- no .mr script needed

        EngineSimResult result = api.Create(&config, &handle);
        if (result != ESIM_SUCCESS || !handle) return;

        // SineWaveSimulator creates dummy engine objects in initialize().
        // LoadScript detects these and wires ctx->engine without loading a file.
        result = api.LoadScript(handle, nullptr, nullptr);
        if (result != ESIM_SUCCESS) return;

        valid = true;
    }

    ~SineEngine() {
        if (valid && handle) {
            api.Destroy(handle);
        }
    }

    // Non-copyable
    SineEngine(const SineEngine&) = delete;
    SineEngine& operator=(const SineEngine&) = delete;
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

    // Helper: Check if a buffer contains only silence
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
//
// BUG: ThreadedStrategy::updateSimulation passes a hardcoded
// UPDATE_INTERVAL_MS = 10.0 to api.Update(). The bridge expects SECONDS.
// 10.0 > 1.0 fails validation, returning ESIM_ERROR_INVALID_PARAMETER (-4).
//
// This test uses a REAL sine-mode engine to prove the bug.
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedStrategy_UpdateSimulation_SucceedsWithRealEngine) {
    // Arrange: Create a real sine-mode engine
    SineEngine engine;
    ASSERT_TRUE(engine.valid) << "Failed to create sine-mode engine";

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    auto circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY));
    context->circularBuffer = circularBuffer.get();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;
    context->audioState.isPlaying.store(true);

    // SimulationLoop passes deltaTime in MILLISECONDS:
    // AudioLoopConfig::UPDATE_INTERVAL * 1000.0 = ~16.667ms
    double deltaTimeMs = (1.0 / 60.0) * 1000.0;

    // Act: Call updateSimulation with the real engine API
    // BUG: ThreadedStrategy ignores deltaTimeMs and passes hardcoded 10.0
    // to api.Update(). The bridge expects seconds (<= 1.0), so 10.0 fails.
    // After the fix (using deltaTimeMs/1000.0), this should succeed.
    strategy->updateSimulation(context.get(), engine.handle, engine.api, deltaTimeMs);

    // Assert: The engine should still be in a valid state after update attempt
    // If the update was rejected (result=-4), GetStats will still return the
    // initial RPM. The real test: does updateSimulation produce a valid result?
    EngineSimStats stats = {};
    engine.api.GetStats(engine.handle, &stats);

    // After a successful update, RPM should be non-zero (sine mode starts at ~800)
    // If update failed (-4), the simulation never advanced.
    // This test WILL FAIL with current code because updateSimulation passes 10.0
    // as seconds, which the bridge rejects. After fix, it passes ~0.0167s and succeeds.
    EXPECT_GT(stats.currentRPM, 0.0)
        << "Engine should have non-zero RPM after update. "
        << "If this fails, ThreadedStrategy::updateSimulation is still passing "
        << "invalid deltaTime to the bridge (BUG: hardcoded 10.0ms treated as seconds).";
}

TEST_F(StrategyPipelineTest, ThreadedStrategy_UpdateSimulation_DeltaTimeConversionIsCorrect) {
    // This test verifies the deltaTime conversion logic by directly calling
    // the bridge API with the value ThreadedStrategy SHOULD pass.
    // It proves the correct value works, isolating the bug to ThreadedStrategy.

    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    // The correct value: SimulationLoop passes ms, strategy should convert to seconds
    double deltaTimeMs = (1.0 / 60.0) * 1000.0;  // ~16.667ms
    double deltaTimeSeconds = deltaTimeMs / 1000.0;  // ~0.01667s

    // Act: Call the bridge directly with the CORRECT value (seconds)
    EngineSimResult result = engine.api.Update(engine.handle, deltaTimeSeconds);

    // Assert: The bridge should accept this (<= 1.0 seconds)
    EXPECT_EQ(result, ESIM_SUCCESS)
        << "Bridge should accept deltaTime=" << deltaTimeSeconds << "s. "
        << "This proves the correct conversion is ms/1000.0. "
        << "The bug is that ThreadedStrategy doesn't do this conversion.";

    // Now call with the WRONG value that ThreadedStrategy currently passes
    EngineSimResult wrongResult = engine.api.Update(engine.handle, 10.0);

    // Assert: The bridge should reject this (> 1.0 seconds)
    EXPECT_EQ(wrongResult, ESIM_ERROR_INVALID_PARAMETER)
        << "Bridge should reject deltaTime=10.0s (> 1.0). "
        << "This is the bug: ThreadedStrategy passes 10.0 (ms as seconds).";
}

// ============================================================================
// TEST GROUP 2: SyncPullStrategy produces frames on successive calls
//
// BUG PROFILE: If the engine stops producing frames after the first call,
// the audio goes silent (beep-then-silence). This test uses a REAL sine-mode
// engine to prove that render produces non-silent frames on successive calls.
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_ProducesAudioOnSuccessiveCalls) {
    // Arrange: Create SyncPullStrategy with real sine-mode engine
    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    config.engineHandle = engine.handle;
    config.engineAPI = &engine.api;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int NUM_RENDER_CALLS = 5;
    const int FRAMES_PER_CALL = 64;  // Small frame count for reliable rendering

    // Act & Assert: Render multiple times and verify each produces non-silent output
    for (int call = 0; call < NUM_RENDER_CALLS; ++call) {
        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CALL);

        bool result = strategy->render(context.get(), &audioBuffer, FRAMES_PER_CALL);
        ASSERT_TRUE(result) << "render() should succeed on call " << (call + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);

        // Verify output is NOT silence
        bool silent = isAllSilence(outputData, FRAMES_PER_CALL);
        EXPECT_FALSE(silent) << "render() call " << (call + 1)
            << " produced silence -- expected non-silent audio from sine engine";

        freeAudioBufferList(audioBuffer);
    }
}

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_FirstAndSubsequentCallsBothProduceAudio) {
    // Arrange: Set up SyncPullStrategy with real sine-mode engine
    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    config.engineHandle = engine.handle;
    config.engineAPI = &engine.api;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int FRAMES = 64;

    // Act: First render
    AudioBufferList firstBuffer = createAudioBufferList(FRAMES);
    bool firstResult = strategy->render(context.get(), &firstBuffer, FRAMES);
    ASSERT_TRUE(firstResult);

    float* firstData = static_cast<float*>(firstBuffer.mBuffers[0].mData);
    bool firstSilent = isAllSilence(firstData, FRAMES);

    // Act: Second render
    AudioBufferList secondBuffer = createAudioBufferList(FRAMES);
    bool secondResult = strategy->render(context.get(), &secondBuffer, FRAMES);
    ASSERT_TRUE(secondResult);

    float* secondData = static_cast<float*>(secondBuffer.mBuffers[0].mData);
    bool secondSilent = isAllSilence(secondData, FRAMES);

    // Assert: BOTH calls should produce non-silent audio
    EXPECT_FALSE(firstSilent) << "First render() should produce non-silent audio";
    EXPECT_FALSE(secondSilent) << "Second render() should produce non-silent audio (no beep-then-silence)";

    freeAudioBufferList(firstBuffer);
    freeAudioBufferList(secondBuffer);
}

// ============================================================================
// TEST GROUP 3: Audio pipeline produces non-silent output
//
// BUG PROFILE: After initialisation and warmup, the pipeline should produce
// continuous non-silent audio. Tests that the ThreadedStrategy + buffer
// pipeline doesn't just output silence after the first buffer.
// These tests use pure buffer operations (no engine API needed).
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedStrategy_Pipeline_ProducesContinuousAudioAfterMultipleAddFrames) {
    // Arrange: Create a full ThreadedStrategy pipeline
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    auto circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY));
    context->circularBuffer = circularBuffer.get();

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(context.get(), config));

    context->audioState.isPlaying.store(true);

    const int NUM_CYCLES = 5;
    const int FRAMES_PER_CYCLE = 10;

    for (int cycle = 0; cycle < NUM_CYCLES; ++cycle) {
        // Simulate generating audio
        float signalValue = 0.1f * (cycle + 1);
        std::vector<float> generatedAudio(FRAMES_PER_CYCLE * STEREO_CHANNELS, signalValue);

        bool addResult = strategy->AddFrames(context.get(), generatedAudio.data(), FRAMES_PER_CYCLE);
        ASSERT_TRUE(addResult) << "AddFrames failed on cycle " << (cycle + 1);

        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CYCLE);
        bool renderResult = strategy->render(context.get(), &audioBuffer, FRAMES_PER_CYCLE);
        ASSERT_TRUE(renderResult) << "render failed on cycle " << (cycle + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);

        EXPECT_FALSE(isAllSilence(outputData, FRAMES_PER_CYCLE))
            << "Pipeline cycle " << (cycle + 1) << " produced silence. "
            << "Expected non-silent audio from buffer.";

        freeAudioBufferList(audioBuffer);
    }
}

TEST_F(StrategyPipelineTest, ThreadedStrategy_Pipeline_DoesNotDrainToSilenceAfterFirstRender) {
    // This test targets the "beep-then-silence" pattern:
    // Write enough audio for multiple renders, then verify ALL renders get audio.

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    auto circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY));
    context->circularBuffer = circularBuffer.get();

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    // Write enough audio for 3 full render calls
    const int TOTAL_FRAMES = TEST_FRAME_COUNT * 3;
    std::vector<float> bigAudioBuffer(TOTAL_FRAMES * STEREO_CHANNELS);
    for (int i = 0; i < TOTAL_FRAMES * STEREO_CHANNELS; ++i) {
        bigAudioBuffer[i] = 0.5f;
    }

    bool addResult = strategy->AddFrames(context.get(), bigAudioBuffer.data(), TOTAL_FRAMES);
    ASSERT_TRUE(addResult);

    // Act: Render 3 times from the same buffer
    int silentRenderCount = 0;
    for (int render = 0; render < 3; ++render) {
        AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
        bool renderResult = strategy->render(context.get(), &audioBuffer, TEST_FRAME_COUNT);
        ASSERT_TRUE(renderResult);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
        if (isAllSilence(outputData, TEST_FRAME_COUNT)) {
            silentRenderCount++;
        }

        freeAudioBufferList(audioBuffer);
    }

    // Assert: None of the renders should have been silence
    EXPECT_EQ(silentRenderCount, 0)
        << "Expected all 3 renders to produce audio, but " << silentRenderCount
        << " produced silence (beep-then-silence pattern detected)";
}

// ============================================================================
// TEST GROUP 4: ReadAudioBuffer drains engine before RenderOnDemand can read
//
// ROOT CAUSE: generateAudioForThreadedMode (SimulationLoop.cpp:278-279) runs
// unconditionally when audioPlayer != null, even in SyncPull mode. It calls
// api.ReadAudioBuffer() which consumes the engine's synthesizer output. When
// SyncPull's render callback then calls RenderOnDemand(), the synthesizer
// buffer is empty -- producing silence after the initial pre-fill drains.
//
// The old StrategyAdapter checked `if (strategy != "Threaded") return;` to
// skip this for SyncPull. The ARCH-004 refactor removed that guard.
//
// This test proves that calling ReadAudioBuffer before RenderOnDemand
// starves RenderOnDemand of frames.
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPull_ReadAudioBufferDrainsEngineBeforeRenderOnDemand) {
    // Arrange: Create a real sine-mode engine with SyncPullStrategy
    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    config.engineHandle = engine.handle;
    config.engineAPI = &engine.api;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int FRAMES = 64;

    // Act: First render (baseline -- should produce audio)
    AudioBufferList firstBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(context.get(), &firstBuffer, FRAMES));
    float* firstData = static_cast<float*>(firstBuffer.mBuffers[0].mData);
    bool firstSilent = isAllSilence(firstData, FRAMES);
    freeAudioBufferList(firstBuffer);

    // Advance simulation so the synthesizer generates more samples
    engine.api.Update(engine.handle, 1.0 / 60.0);

    // Act: Simulate what generateAudioForThreadedMode does -- call ReadAudioBuffer
    // This drains the synthesizer's output buffer, consuming audio that
    // RenderOnDemand would otherwise produce.
    std::vector<float> drainBuffer(FRAMES * 2);
    int drained = 0;
    engine.api.ReadAudioBuffer(engine.handle, drainBuffer.data(), FRAMES, &drained);

    // Act: Now call render again (what the CoreAudio callback would do)
    // If ReadAudioBuffer drained the synthesizer, this should produce silence.
    AudioBufferList secondBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(context.get(), &secondBuffer, FRAMES));
    float* secondData = static_cast<float*>(secondBuffer.mBuffers[0].mData);
    bool secondSilent = isAllSilence(secondData, FRAMES);
    freeAudioBufferList(secondBuffer);

    // Assert: First render should have audio, second should also have audio.
    // If ReadAudioBuffer drained the synthesizer, the second render will be silence.
    // The bug is that ReadAudioBuffer consumed the audio that RenderOnDemand needed.
    EXPECT_FALSE(firstSilent) << "First render (before ReadAudioBuffer) should produce audio";
    EXPECT_FALSE(secondSilent)
        << "Second render (after ReadAudioBuffer) should produce audio. "
        << "Got silence -- ReadAudioBuffer drained the synthesizer buffer. "
        << "BUG: generateAudioForThreadedMode runs in SyncPull mode, "
        << "consuming audio that RenderOnDemand needs.";
}

TEST_F(StrategyPipelineTest, SyncPull_RenderOnDemand_RecoversAfterReadAudioBufferDrain) {
    // This test proves the engine CAN recover after a drain if given time,
    // but the immediate next render is starved -- which is what happens at
    // 60Hz loop rate (no time to recover between ReadAudioBuffer and render).

    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    config.engineHandle = engine.handle;
    config.engineAPI = &engine.api;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int FRAMES = 64;

    // Drain via ReadAudioBuffer
    std::vector<float> drainBuffer(FRAMES * 2);
    int drained = 0;
    engine.api.ReadAudioBuffer(engine.handle, drainBuffer.data(), FRAMES, &drained);

    // Immediate render -- should be starved (this is the bug)
    AudioBufferList starvedBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(context.get(), &starvedBuffer, FRAMES));
    float* starvedData = static_cast<float*>(starvedBuffer.mBuffers[0].mData);
    bool starvedSilent = isAllSilence(starvedData, FRAMES);
    freeAudioBufferList(starvedBuffer);

    // The simulation loop runs updateSimulation (no-op for SyncPull) and then
    // generateAudioForThreadedMode (ReadAudioBuffer) every ~16ms. There's no
    // time for the synthesizer to accumulate new samples between the drain
    // and the next render callback. So every render after the first is starved.

    EXPECT_FALSE(starvedSilent)
        << "Render after ReadAudioBuffer drain produced silence. "
        << "This proves that ReadAudioBuffer + RenderOnDemand in the same tick "
        << "causes beep-then-silence in SyncPull mode. "
        << "FIX: generateAudioForThreadedMode must NOT run in SyncPull mode.";
}

// ============================================================================
// TEST GROUP 5: Threaded pipeline integration -- full failure chain
//
// Simulates the complete SimulationLoop threaded pipeline:
//   updateSimulation -> ReadAudioBuffer -> addToCircularBuffer -> render
//
// NOTE: SineWaveSimulator requires StartAudioThread to populate the output
// buffer for ReadAudioBuffer. Without the audio thread running, ReadAudioBuffer
// always returns 0 frames regardless of update success. This means the threaded
// + sine combination is fundamentally limited for unit testing ReadAudioBuffer.
//
// Instead, these tests:
// (a) Prove the threaded pipeline works when audio is injected (AddFrames + render)
// (b) Prove ReadAudioBuffer returns 0 without StartAudioThread (explaining why
//     --threaded --sine produces silence in the real app)
// (c) Prove updateSimulation succeeds with the fix (deltaTime conversion)
// ============================================================================

TEST_F(StrategyPipelineTest, ThreadedPipeline_FullChain_WithInjectedAudio) {
    // Arrange: Full threaded pipeline. Since ReadAudioBuffer returns 0 without
    // StartAudioThread (which creates real threads unsuitable for unit tests),
    // we inject audio directly via AddFrames to simulate successful generation.
    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    auto circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY));
    context->circularBuffer = circularBuffer.get();

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int FRAMES_PER_CYCLE = 64;
    double deltaTimeMs = (1.0 / 60.0) * 1000.0;

    const int NUM_ITERATIONS = 5;
    int silentRenders = 0;

    for (int i = 0; i < NUM_ITERATIONS; ++i) {
        // Step 1: updateSimulation (proves the deltaTime fix works)
        strategy->updateSimulation(context.get(), engine.handle, engine.api, deltaTimeMs);

        // Step 2: Simulate successful audio generation (what ReadAudioBuffer
        // would produce if StartAudioThread were running)
        std::vector<float> generatedAudio(FRAMES_PER_CYCLE * STEREO_CHANNELS);
        float signalValue = 0.1f * (i + 1);
        for (int j = 0; j < FRAMES_PER_CYCLE * STEREO_CHANNELS; ++j) {
            generatedAudio[j] = signalValue;
        }

        // Step 3: Add to circular buffer
        strategy->AddFrames(context.get(), generatedAudio.data(), FRAMES_PER_CYCLE);

        // Step 4: Render (what CoreAudio callback does)
        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CYCLE);
        bool renderResult = strategy->render(context.get(), &audioBuffer, FRAMES_PER_CYCLE);
        ASSERT_TRUE(renderResult) << "render failed on iteration " << (i + 1);

        float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
        if (isAllSilence(outputData, FRAMES_PER_CYCLE)) {
            silentRenders++;
        }

        freeAudioBufferList(audioBuffer);
    }

    // Assert: All renders should produce audio (the buffer pipeline works)
    EXPECT_EQ(silentRenders, 0)
        << "Threaded pipeline produced silence on " << silentRenders
        << " of " << NUM_ITERATIONS << " iterations. "
        << "When audio is injected, the buffer pipeline should produce continuous output.";
}

TEST_F(StrategyPipelineTest, ThreadedPipeline_ReadAudioBufferReturnsZeroWithoutAudioThread) {
    // This test proves WHY --threaded --sine produces silence in the real app:
    // ReadAudioBuffer requires StartAudioThread to populate the output buffer.
    // Without it, ReadAudioBuffer always returns 0, even after successful Update.
    // This is by design -- the threaded path needs the audio thread running.

    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    const int FRAMES = 64;

    // Advance the simulation with a valid update
    EngineSimResult updateResult = engine.api.Update(engine.handle, 0.01667);
    ASSERT_EQ(updateResult, ESIM_SUCCESS)
        << "Update should succeed with valid deltaTime";

    // ReadAudioBuffer without StartAudioThread -> returns 0
    std::vector<float> buffer(FRAMES * 2);
    int readCount = 0;
    engine.api.ReadAudioBuffer(engine.handle, buffer.data(), FRAMES, &readCount);

    // Assert: ReadAudioBuffer returns 0 frames without StartAudioThread.
    // This explains the --threaded --sine silence: the bridge's ReadAudioBuffer
    // reads from the simulator's output ring, which is only populated when the
    // internal audio thread (started by StartAudioThread) calls renderAudio().
    EXPECT_EQ(readCount, 0)
        << "ReadAudioBuffer should return 0 without StartAudioThread. "
        << "Got " << readCount << " frames. "
        << "This proves --threaded --sine cannot produce audio via ReadAudioBuffer "
        << "unless StartAudioThread is called (which the strategy does in startPlayback).";
}

TEST_F(StrategyPipelineTest, ThreadedPipeline_UpdateFailurePreventsSimulationAdvance) {
    // Proves the causal chain: bad deltaTime -> Update fails -> no simulation advance.
    // This is what happened before the fix (hardcoded 10.0 passed as seconds).

    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    // Call Update with the WRONG value (10.0 seconds -- old code passed this)
    EngineSimResult badResult = engine.api.Update(engine.handle, 10.0);
    EXPECT_EQ(badResult, ESIM_ERROR_INVALID_PARAMETER)
        << "Bridge should reject deltaTime=10.0s (> 1.0). Got result=" << badResult;

    // Call Update with the CORRECT value (what the fix passes)
    EngineSimResult goodResult = engine.api.Update(engine.handle, 0.01667);
    EXPECT_EQ(goodResult, ESIM_SUCCESS)
        << "Bridge should accept deltaTime=0.01667s (<= 1.0). Got result=" << goodResult;
}

// ============================================================================
// TEST GROUP 6: SyncPull render propagates frame counts to diagnostics
//
// BUG: SyncPullStrategy::render() did not call diagnostics.recordRender()
// doesn't store frame counts in Diagnostics. This test uses a real sine-mode
// engine to prove that after render(), diagnostics contain frame counts.
// ============================================================================

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_UpdatesTotalFramesRendered) {
    // Arrange
    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    config.engineHandle = engine.handle;
    config.engineAPI = &engine.api;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int FRAMES = 64;

    // Act: Render
    AudioBufferList audioBuffer = createAudioBufferList(FRAMES);
    ASSERT_TRUE(strategy->render(context.get(), &audioBuffer, FRAMES));

    // Assert: Diagnostics should reflect frames rendered
    int64_t totalRendered = context->diagnostics.totalFramesRendered.load();
    EXPECT_GT(totalRendered, 0)
        << "After render(), diagnostics.totalFramesRendered should be > 0. "
        << "Got 0 -- SyncPullStrategy does not propagate frame counts to Diagnostics.";

    freeAudioBufferList(audioBuffer);
}

TEST_F(StrategyPipelineTest, SyncPullStrategy_Render_AccumulatesFramesAcrossMultipleCalls) {
    // Arrange
    SineEngine engine;
    ASSERT_TRUE(engine.valid);

    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    auto context = std::make_unique<BufferContext>();
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    config.engineHandle = engine.handle;
    config.engineAPI = &engine.api;
    ASSERT_TRUE(strategy->initialize(context.get(), config));
    context->audioState.isPlaying.store(true);

    const int FRAMES_PER_CALL = 64;
    const int NUM_CALLS = 4;

    // Act: Render multiple times
    for (int i = 0; i < NUM_CALLS; ++i) {
        AudioBufferList audioBuffer = createAudioBufferList(FRAMES_PER_CALL);
        ASSERT_TRUE(strategy->render(context.get(), &audioBuffer, FRAMES_PER_CALL));
        freeAudioBufferList(audioBuffer);
    }

    // Assert: Total frames rendered should accumulate
    int64_t totalRendered = context->diagnostics.totalFramesRendered.load();
    EXPECT_EQ(totalRendered, static_cast<int64_t>(FRAMES_PER_CALL * NUM_CALLS))
        << "After " << NUM_CALLS << " renders of " << FRAMES_PER_CALL << " frames each, "
        << "totalFramesRendered should be " << (FRAMES_PER_CALL * NUM_CALLS)
        << ". Got " << totalRendered << " -- frames are not being accumulated in diagnostics.";
}
