// ISimulatorTest.cpp - TDD RED-phase tests for Phase E: ISimulator refactor
//
// Purpose: Assert that ISimulator abstracts the C-style EngineSimAPI behind
// a proper C++ interface, decoupling the audio pipeline from the raw bridge.
//
// Phase E establishes the "Holy Trinity":
//   ISimulator (produces frames) -> IAudioStrategy (orchestrates) -> IAudioHardwareProvider (consumes)
//
// TARGET ARCHITECTURE:
//   1. ISimulator is a pure virtual C++ interface wrapping EngineSimAPI
//   2. BridgeSimulator implements ISimulator, forwarding to EngineSim C functions
//   3. MockSimulator implements ISimulator for testing (no engine-sim dependency)
//   4. IAudioStrategy methods take ISimulator* instead of EngineSimHandle/EngineSimAPI&
//   5. SimulationLoop takes ISimulator* instead of EngineSimHandle + EngineSimAPI&
//
// RED PHASE: These tests will NOT compile because ISimulator.h,
// BridgeSimulator.h, and MockSimulator.h do not exist yet.
// The tech-architect creates them to make these GREEN.

#include <gtest/gtest.h>
#include <memory>
#include <string>

// These headers do not exist yet -- RED phase
#include "ISimulator.h"
#include "BridgeSimulator.h"
#include "MockSimulator.h"
#include "IAudioStrategy.h"
#include "ThreadedStrategy.h"
#include "SyncPullStrategy.h"
#include "AudioTestConstants.h"
#include "AudioTestHelpers.h"
#include <thread>
#include <chrono>

using namespace test::constants;

// ============================================================================
// GROUP 1: ISimulator interface contract
//
// TARGET: ISimulator is a pure virtual interface that abstracts the
// EngineSim C API behind clean C++ virtual methods. Every method maps
// directly to an EngineSim C function.
// ============================================================================

class ISimulatorTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}
};

TEST_F(ISimulatorTest, MockSimulator_ImplementsISimulator) {
    // Arrange: Create a MockSimulator (test double)
    auto sim = std::make_unique<MockSimulator>();

    // Assert: Can be used through ISimulator pointer (Liskov substitution)
    ISimulator* iface = sim.get();
    ASSERT_NE(iface, nullptr);
}

TEST_F(ISimulatorTest, MockSimulator_UpdateAdvancesSimulation) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act: Advance simulation by one frame (16.667ms at 60Hz)
    sim->update(0.016667);

    // Assert: Stats should reflect the update
    auto stats = sim->getStats();
    EXPECT_GE(stats.currentRPM, 0.0);
}

TEST_F(ISimulatorTest, MockSimulator_GetStats_ReturnsValidStats) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();
    sim->update(0.016667);

    // Act: Get engine stats
    auto stats = sim->getStats();

    // Assert: Stats struct is populated (RPM >= 0)
    EXPECT_GE(stats.currentRPM, 0.0);
}

TEST_F(ISimulatorTest, MockSimulator_SetThrottle_AcceptsValue) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act: Set throttle to 50%
    sim->setThrottle(0.5);

    // Assert: update and getStats should reflect the throttle
    sim->update(0.016667);
    auto stats = sim->getStats();
    EXPECT_GT(stats.currentRPM, 0.0);
}

TEST_F(ISimulatorTest, MockSimulator_SetIgnition_AcceptsValue) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act: Enable ignition (void return - no crash = success)
    sim->setIgnition(true);

    // Assert: If we get here, the call succeeded
    SUCCEED();
}

TEST_F(ISimulatorTest, MockSimulator_SetStarterMotor_AcceptsValue) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act: Enable starter motor (void return - no crash = success)
    sim->setStarterMotor(true);

    // Assert: If we get here, the call succeeded
    SUCCEED();
}

TEST_F(ISimulatorTest, MockSimulator_ReadAudioBuffer_ReturnsSilence) {
    // Arrange: MockSimulator produces silence (no engine physics)
    auto sim = std::make_unique<MockSimulator>();
    const int frames = 256;
    std::vector<float> buffer(frames * 2, 999.0f); // sentinel value

    // Act: Read audio from simulator
    int32_t samplesRead = 0;
    bool result = sim->readAudioBuffer(buffer.data(), frames, &samplesRead);

    // Assert: Should succeed
    EXPECT_TRUE(result);
    // Assert: Should produce data (silence is valid for a mock)
    EXPECT_GT(samplesRead, 0);
}

TEST_F(ISimulatorTest, MockSimulator_RenderOnDemand_ReturnsSilence) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();
    const int frames = 256;
    std::vector<float> buffer(frames * 2, 999.0f);

    // Act: Render on demand (synchronous pull model)
    int32_t framesWritten = 0;
    bool result = sim->renderOnDemand(buffer.data(), frames, &framesWritten);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_GT(framesWritten, 0);
}

TEST_F(ISimulatorTest, MockSimulator_StartAudioThread_Succeeds) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act: Start audio thread
    bool result = sim->startAudioThread();

    // Assert: MockSimulator should accept this (no-op or simulated thread)
    EXPECT_TRUE(result);
}

TEST_F(ISimulatorTest, MockSimulator_CreateAndDestroy_Lifecycle) {
    // Arrange/Act: Full lifecycle -- create, use, destroy
    auto sim = std::make_unique<MockSimulator>();
    ASSERT_NE(sim, nullptr);

    sim->update(0.016667);
    sim->setThrottle(0.5);
    sim->setIgnition(true);

    // Act: Destroy (void return - no crash = success)
    sim->destroy();

    // Assert: Clean shutdown
    SUCCEED();
}

TEST_F(ISimulatorTest, MockSimulator_SetLogging_AcceptsLogger) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();
    auto logger = std::make_unique<ConsoleLogger>();

    // Act
    bool result = sim->setLogging(logger.get());

    // Assert
    EXPECT_TRUE(result);
}

TEST_F(ISimulatorTest, MockSimulator_GetLastError_ReturnsEmptyWhenNoError) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act
    std::string error = sim->getLastError();

    // Assert: No error on fresh simulator
    EXPECT_TRUE(error.empty());
}

// ============================================================================
// GROUP 2: BridgeSimulator wraps real EngineSimAPI
//
// TARGET: BridgeSimulator implements ISimulator by forwarding calls
// to the C-style EngineSim functions. This is the production implementation.
// These tests verify that the wrapping layer works correctly.
// ============================================================================

TEST_F(ISimulatorTest, BridgeSimulator_ImplementsISimulator) {
    // Arrange: Create BridgeSimulator (wraps real EngineSim)
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;  // Sine mode for testing (no engine script needed)

    auto sim = std::make_unique<BridgeSimulator>();

    // Act: Initialize
    bool initResult = sim->create(config);

    // Assert: Should succeed with sine mode
    EXPECT_TRUE(initResult);

    // Assert: Can be used through ISimulator pointer
    ISimulator* iface = sim.get();
    ASSERT_NE(iface, nullptr);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_SineMode_UpdatesSuccessfully) {
    // Arrange: Create in sine mode
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();
    ASSERT_TRUE(sim->create(config));
    ASSERT_TRUE(sim->loadScript("", ""));  // Initialize synthesizer

    // Act: Update simulation (void return - no crash = success)
    sim->update(0.016667);

    // Assert
    SUCCEED();

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_SineMode_ReadAudioBufferReturnsData) {
    // Arrange
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();
    ASSERT_TRUE(sim->create(config));
    ASSERT_TRUE(sim->loadScript("", ""));
    ASSERT_TRUE(sim->startAudioThread());

    // Advance simulation so audio thread has data to produce
    sim->update(0.016667);

    // Give audio thread time to produce samples
    std::this_thread::sleep_for(std::chrono::milliseconds(100));

    // Act: Read audio
    const int frames = 256;
    std::vector<float> buffer(frames * 2, 0.0f);
    int32_t samplesRead = 0;
    bool result = sim->readAudioBuffer(buffer.data(), frames, &samplesRead);

    // Assert: Should succeed and produce data
    EXPECT_TRUE(result);
    EXPECT_GT(samplesRead, 0);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_SineMode_RenderOnDemandReturnsData) {
    // Arrange
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();
    ASSERT_TRUE(sim->create(config));
    ASSERT_TRUE(sim->loadScript("", ""));

    // Act: Render on demand (synchronous)
    const int frames = 256;
    std::vector<float> buffer(frames * 2, 0.0f);
    int32_t framesWritten = 0;
    bool result = sim->renderOnDemand(buffer.data(), frames, &framesWritten);

    // Assert
    EXPECT_TRUE(result);
    EXPECT_GT(framesWritten, 0);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_GetStats_ReturnsValidStats) {
    // Arrange
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();
    ASSERT_TRUE(sim->create(config));
    ASSERT_TRUE(sim->loadScript("", ""));
    sim->update(0.016667);

    // Act
    auto stats = sim->getStats();

    // Assert: Stats should be populated (RPM >= 0)
    EXPECT_GE(stats.currentRPM, 0.0);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_SetThrottle_Succeeds) {
    // Arrange
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();
    ASSERT_TRUE(sim->create(config));
    ASSERT_TRUE(sim->loadScript("", ""));

    // Act (void return - no crash = success)
    sim->setThrottle(0.75);

    // Assert
    SUCCEED();

    sim->destroy();
}

// ============================================================================
// GROUP 3: IAudioStrategy uses ISimulator* instead of EngineSimHandle/API
//
// TARGET: Strategy methods take ISimulator* instead of raw engine types.
// This decouples the audio pipeline from the bridge implementation.
// ============================================================================

class ISimulatorStrategyTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<ConsoleLogger>();
        mockSim_ = std::make_unique<MockSimulator>();
    }

    void TearDown() override {}

    std::unique_ptr<ConsoleLogger> logger_;
    std::unique_ptr<MockSimulator> mockSim_;
};

TEST_F(ISimulatorStrategyTest, ThreadedStrategy_FillBufferFromEngine_UsesISimulator) {
    // Arrange: Create strategy
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: fillBufferFromEngine takes ISimulator* instead of EngineSimHandle/EngineSimAPI&
    strategy->fillBufferFromEngine(mockSim_.get(), 512);

    // Assert: No crash -- strategy uses ISimulator interface
    SUCCEED();
}

TEST_F(ISimulatorStrategyTest, ThreadedStrategy_UpdateSimulation_UsesISimulator) {
    // Arrange
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: updateSimulation takes ISimulator* instead of EngineSimHandle/EngineSimAPI&
    strategy->updateSimulation(mockSim_.get(), 16.667);

    // Assert: No crash
    SUCCEED();
}

TEST_F(ISimulatorStrategyTest, ThreadedStrategy_StartPlayback_UsesISimulator) {
    // Arrange
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: startPlayback takes ISimulator* instead of EngineSimHandle/EngineSimAPI*
    bool result = strategy->startPlayback(mockSim_.get());

    // Assert: Should succeed with MockSimulator
    EXPECT_TRUE(result);
    EXPECT_TRUE(strategy->isPlaying());

    // Cleanup
    strategy->stopPlayback(mockSim_.get());
}

TEST_F(ISimulatorStrategyTest, SyncPullStrategy_StartPlayback_UsesISimulator) {
    // Arrange
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: startPlayback takes ISimulator*
    bool result = strategy->startPlayback(mockSim_.get());

    // Assert
    EXPECT_TRUE(result);
    EXPECT_TRUE(strategy->isPlaying());

    strategy->stopPlayback(mockSim_.get());
}

TEST_F(ISimulatorStrategyTest, ThreadedStrategy_FullPipeline_WithMockSimulator) {
    // Arrange: Create strategy with ISimulator
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: Fill buffer from ISimulator
    strategy->fillBufferFromEngine(mockSim_.get(), 512);

    // Act: Render what we got
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    bool renderResult = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Should succeed (may be silence from mock, but no crash)
    EXPECT_TRUE(renderResult);

    freeAudioBufferList(audioBuffer);
}

TEST_F(ISimulatorStrategyTest, SyncPullStrategy_Render_UsesISimulator) {
    // Arrange: SyncPullStrategy with ISimulator
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    // Act: Start playback with ISimulator
    ASSERT_TRUE(strategy->startPlayback(mockSim_.get()));

    // Act: Render audio (SyncPull renders on-demand from ISimulator)
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    bool renderResult = strategy->render(&audioBuffer, TEST_FRAME_COUNT);

    // Assert: Should succeed -- SyncPull calls renderOnDemand() on ISimulator
    EXPECT_TRUE(renderResult);

    freeAudioBufferList(audioBuffer);
    strategy->stopPlayback(mockSim_.get());
}

// ============================================================================
// GROUP 4: SimulationLoop uses ISimulator* instead of EngineSimHandle/API
//
// TARGET: runUnifiedAudioLoop and runSimulation take ISimulator*
// instead of EngineSimHandle + EngineSimAPI&. This is the final decoupling.
// ============================================================================

TEST_F(ISimulatorTest, MockSimulator_CanBeUsedBySimulationLoop) {
    // Arrange: Create ISimulator (MockSimulator for testing)
    auto sim = std::make_unique<MockSimulator>();

    // Act: Verify the simulator has all methods SimulationLoop needs
    ISimulator* iface = sim.get();
    ASSERT_NE(iface, nullptr);

    // SimulationLoop needs these operations:
    iface->update(0.016667);
    iface->setThrottle(0.5);
    iface->setIgnition(true);
    iface->setStarterMotor(true);

    auto stats = iface->getStats();
    EXPECT_GE(stats.currentRPM, 0.0);

    std::string error = iface->getLastError();
    EXPECT_TRUE(error.empty());
}

TEST_F(ISimulatorTest, ISimulator_HasVirtualDestructor) {
    // Arrange: Verify ISimulator has virtual destructor for proper cleanup
    ISimulator* sim = new MockSimulator();

    // Act: Delete through base pointer -- must not crash or leak
    delete sim;

    // Assert: If we get here, virtual destructor works
    SUCCEED();
}

// ============================================================================
// GROUP 5: MockSimulator provides deterministic test behavior
//
// TARGET: MockSimulator returns predictable values for testing.
// It does NOT depend on the engine-sim library.
// ============================================================================

TEST_F(ISimulatorTest, MockSimulator_ProducesSilenceForAudio) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();
    const int frames = 256;
    std::vector<float> buffer(frames * 2, 1.0f); // Initialize to non-zero

    // Act
    int32_t samplesRead = 0;
    sim->readAudioBuffer(buffer.data(), frames, &samplesRead);

    // Assert: MockSimulator produces silence (all zeros)
    EXPECT_GT(samplesRead, 0);
    for (int i = 0; i < samplesRead; ++i) {
        EXPECT_FLOAT_EQ(buffer[i], 0.0f)
            << "MockSimulator should produce silence, non-zero at sample " << i;
    }
}

TEST_F(ISimulatorTest, MockSimulator_RenderOnDemand_ProducesSilence) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();
    const int frames = 256;
    std::vector<float> buffer(frames * 2, 1.0f);

    // Act
    int32_t framesWritten = 0;
    sim->renderOnDemand(buffer.data(), frames, &framesWritten);

    // Assert
    EXPECT_GT(framesWritten, 0);
    for (int i = 0; i < framesWritten * 2; ++i) {
        EXPECT_FLOAT_EQ(buffer[i], 0.0f)
            << "MockSimulator renderOnDemand should produce silence at sample " << i;
    }
}

TEST_F(ISimulatorTest, MockSimulator_Stats_HaveDefaults) {
    // Arrange
    auto sim = std::make_unique<MockSimulator>();

    // Act: Get stats without any update
    auto stats = sim->getStats();

    // Assert: Default stats are zero-initialized
    EXPECT_DOUBLE_EQ(stats.currentRPM, 0.0);
    EXPECT_DOUBLE_EQ(stats.currentLoad, 0.0);
}

TEST_F(ISimulatorTest, MockSimulator_NoEngineSimDependency) {
    // Arrange: MockSimulator does NOT require EngineSimConfig or EngineSimCreate.
    // It can be created without any engine-sim library calls.
    auto sim = std::make_unique<MockSimulator>();

    // Act: Use it without calling create() (it doesn't need EngineSimConfig)
    sim->update(0.016667);
    sim->setThrottle(0.5);

    // Assert: Works without any engine-sim initialization
    SUCCEED();
}

// ============================================================================
// GROUP 6: BridgeSimulator lifecycle -- create with config
//
// TARGET: BridgeSimulator::create() takes EngineSimConfig and returns bool.
// BridgeSimulator::loadScript() loads a .mr file.
// These wrap EngineSimCreate and EngineSimLoadScript.
// ============================================================================

TEST_F(ISimulatorTest, BridgeSimulator_CreateWithSineMode_Succeeds) {
    // Arrange
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();

    // Act
    bool result = sim->create(config);

    // Assert
    EXPECT_TRUE(result);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_CreateWithoutSineMode_NeedsScript) {
    // Arrange: Normal physics mode requires a script to be loaded
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 0;

    auto sim = std::make_unique<BridgeSimulator>();

    // Act: Create should succeed, but update/render will fail without script
    bool createResult = sim->create(config);

    // Assert: Create itself succeeds (just allocates)
    EXPECT_TRUE(createResult);

    // Act: Update without script is a void call -- verify it doesn't crash
    sim->update(0.016667);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_SetLogging_BeforeCreate_Succeeds) {
    // Arrange
    auto sim = std::make_unique<BridgeSimulator>();
    auto logger = std::make_unique<ConsoleLogger>();

    // Act: Set logging before create
    bool result = sim->setLogging(logger.get());

    // Assert
    EXPECT_TRUE(result);

    sim->destroy();
}

TEST_F(ISimulatorTest, BridgeSimulator_FullLifecycle_SineMode) {
    // Arrange: Full lifecycle in sine mode
    EngineSimConfig config{};
    config.sampleRate = 48000;
    config.sineMode = 1;

    auto sim = std::make_unique<BridgeSimulator>();

    // Act: Full lifecycle
    ASSERT_TRUE(sim->create(config));
    ASSERT_TRUE(sim->loadScript("", ""));
    sim->update(0.016667);
    sim->setThrottle(0.5);
    sim->setIgnition(true);

    auto stats = sim->getStats();
    EXPECT_GE(stats.currentRPM, 0.0);

    ASSERT_TRUE(sim->startAudioThread());
    sim->destroy();

    SUCCEED();
}
