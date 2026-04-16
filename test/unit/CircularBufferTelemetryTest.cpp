// CircularBufferTelemetryTest.cpp - TDD tests for underrun telemetry push
//
// Purpose: Verify that underruns are published to AudioDiagnosticsTelemetry
// instead of being managed as external state on CircularBuffer.
//
// Business value: Components push diagnostics to telemetry (ISP/SRP).
// ThreadedStrategy detects underruns and pushes to telemetry.
// SimulationLoop reads underruns from telemetry, not from the buffer.

#include <gtest/gtest.h>
#include <memory>

#include "ThreadedStrategy.h"
#include "SyncPullStrategy.h"
#include "ITelemetryProvider.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

using namespace test::constants;

// ============================================================================
// Test Fixture
// ============================================================================

class CircularBufferTelemetryTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create telemetry provider
        telemetry_ = std::make_unique<telemetry::InMemoryTelemetry>();

        // Create strategy with telemetry injected (strategy owns its CircularBuffer)
        strategy_ = std::make_unique<ThreadedStrategy>(nullptr, telemetry_.get());

        // Initialize strategy
        AudioStrategyConfig config;
        config.sampleRate = DEFAULT_SAMPLE_RATE;
        config.channels = STEREO_CHANNELS;
        ASSERT_TRUE(strategy_->initialize(config));
    }

    void TearDown() override {
        strategy_.reset();
    }

    std::unique_ptr<telemetry::InMemoryTelemetry> telemetry_;
    std::unique_ptr<ThreadedStrategy> strategy_;
};

// ============================================================================
// Test 1: Underrun is published to telemetry when buffer is empty
// ============================================================================

TEST_F(CircularBufferTelemetryTest, UnderrunPushedToTelemetry_WhenBufferEmpty) {
    // Act: Render with no data available (buffer is empty after init)
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(&audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Render should succeed (output silence)
    EXPECT_TRUE(renderResult);

    // Assert: Underrun count should be visible in telemetry
    auto diag = telemetry_->getAudioDiagnostics();
    EXPECT_GT(diag.underrunCount, 0)
        << "Underrun should be published to telemetry when buffer is empty";

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// Test 2: No underrun published when buffer has data
// ============================================================================

TEST_F(CircularBufferTelemetryTest, NoUnderrunInTelemetry_WhenBufferHasData) {
    // Arrange: Fill internal buffer with data via AddFrames
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame + 1);
    }
    strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT);

    // Act: Render with data available
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(&audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Render should succeed
    EXPECT_TRUE(renderResult);

    // Assert: No underrun should be recorded in telemetry
    auto diag = telemetry_->getAudioDiagnostics();
    EXPECT_EQ(diag.underrunCount, 0)
        << "No underrun should be published when buffer has sufficient data";

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// Test 3: Multiple underruns accumulate in telemetry
// ============================================================================

TEST_F(CircularBufferTelemetryTest, MultipleUnderruns_AccumulateInTelemetry) {
    // Act: Render multiple times with no data (buffer empty after init)
    for (int i = 0; i < 5; ++i) {
        AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
        strategy_->render(&audioBuffer, DEFAULT_FRAME_COUNT);
        freeAudioBufferList(audioBuffer);
    }

    // Assert: All underruns should be in telemetry
    auto diag = telemetry_->getAudioDiagnostics();
    EXPECT_GE(diag.underrunCount, 5)
        << "Multiple underruns should accumulate in telemetry";
}

// ============================================================================
// Test 4: Buffer health is published to telemetry during normal operation
// ============================================================================

TEST_F(CircularBufferTelemetryTest, BufferHealthPublishedToTelemetry) {
    // Arrange: Fill buffer partially via AddFrames
    int halfCapacity = DEFAULT_BUFFER_CAPACITY / 2;
    std::vector<float> testData(halfCapacity * STEREO_CHANNELS, 0.5f);
    strategy_->AddFrames(testData.data(), halfCapacity);

    // Act: Render a small amount
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    strategy_->render(&audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Buffer health should be published
    auto diag = telemetry_->getAudioDiagnostics();
    EXPECT_GT(diag.bufferHealthPct, 0.0)
        << "Buffer health should be published to telemetry";

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// Test 5: SyncPullStrategy does not push audio diagnostics to telemetry
//         (SyncPull does not use CircularBuffer at all)
// ============================================================================

TEST_F(CircularBufferTelemetryTest, SyncPullStrategy_DoesNotPushAudioDiagnostics) {
    // Arrange: Create SyncPullStrategy
    auto syncStrategy = std::make_unique<SyncPullStrategy>(nullptr);

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    syncStrategy->initialize(config);

    // Act: SyncPullStrategy.render() will fail without engine API,
    //      but it should NOT push audio diagnostics to telemetry
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    syncStrategy->render(&audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Audio diagnostics should remain at zero (SyncPull does not push)
    auto diag = telemetry_->getAudioDiagnostics();
    EXPECT_EQ(diag.underrunCount, 0)
        << "SyncPullStrategy should not push audio diagnostics to telemetry";

    freeAudioBufferList(audioBuffer);
}
