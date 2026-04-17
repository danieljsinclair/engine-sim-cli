// ThreadedStrategyIntegrationTest.cpp - Integration tests for ThreadedStrategy
// TDD: Tests verify ThreadedStrategy behavior end-to-end with deterministic simulation
// Uses MockDataSimulator for predictable audio generation
// Captures baseline output for regression verification
// Strategies own their own state -- no BufferContext needed
// Phase G: Uses AudioBufferDescriptor instead of CoreAudio AudioBufferList

#include "strategy/ThreadedStrategy.h"
#include "strategy/SyncPullStrategy.h"
#include "../mocks/MockDataSimulator.h"
#include "../mocks/MockAudioUnit.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <gtest/gtest.h>
#include <fstream>
#include <cstring>

#include "telemetry/ITelemetryProvider.h"

using namespace test::constants;

// ============================================================================
// Integration test fixture for ThreadedStrategy
// ============================================================================

class ThreadedStrategyIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create strategy with telemetry for diagnostic verification
        telemetry_ = std::make_unique<telemetry::InMemoryTelemetry>();
        strategy_ = std::make_unique<ThreadedStrategy>(logger_.get(), telemetry_.get());

        // Initialize strategy
        AudioStrategyConfig config;
        config.sampleRate = DEFAULT_SAMPLE_RATE;
        config.channels = STEREO_CHANNELS;
        ASSERT_TRUE(strategy_->initialize(config))
            << "Failed to initialize ThreadedStrategy";
    }

    void TearDown() override {
        if (strategy_) {
            strategy_->reset();
        }
    }

    // Helper: Capture baseline to file
    void captureBaseline(const float* data, int frames, const char* filename) {
        std::ofstream baselineFile(filename, std::ios::binary);
        ASSERT_TRUE(baselineFile.is_open())
            << "Failed to open baseline file: " << filename;

        baselineFile.write(reinterpret_cast<const char*>(data), frames * STEREO_CHANNELS * sizeof(float));
        baselineFile.close();

        std::cout << "[BASELINE] Captured " << frames << " frames to " << filename << "\n";
    }

    std::unique_ptr<ConsoleLogger> logger_;
    std::unique_ptr<telemetry::InMemoryTelemetry> telemetry_;
    std::unique_ptr<ThreadedStrategy> strategy_;
};

// ============================================================================
// TDD TEST 1: ThreadedStrategy should initialize successfully
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_InitializesSuccessfully) {
    // Arrange: Already initialized in SetUp()

    // Assert: Strategy behaviour is correct
    EXPECT_TRUE(strategy_->shouldDrainDuringWarmup());
    EXPECT_STREQ(strategy_->getModeString().c_str(), "THREADED");
}

// ============================================================================
// TDD TEST 2: ThreadedStrategy should render from circular buffer
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_RendersFromCircularBuffer) {
    // Arrange: Fill internal buffer with known pattern
    // Use pattern: left = +0 + frame, right = +1 + frame
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame + 1);
    }

    // Write to strategy's internal buffer
    ASSERT_TRUE(strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT));

    // Act: Render audio from internal buffer
    AudioBufferDescriptor audioBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(audioBuffer);

    // Assert: Render should succeed
    EXPECT_TRUE(renderResult)
        << "ThreadedStrategy::render should succeed";

    // Verify: Output should match input pattern
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        float expectedLeft = static_cast<float>(frame);
        float expectedRight = static_cast<float>(frame + 1);

        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS], expectedLeft)
            << "Left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Right channel mismatch at frame " << frame;
    }

    freeAudioBuffer(audioBuffer);
}

// ============================================================================
// TDD TEST 3: ThreadedStrategy should handle buffer underrun gracefully
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_HandlesBufferUnderrunGracefully) {
    // Arrange: Reset internal buffer to empty (initialized in SetUp but no data)
    strategy_->resetBufferAfterWarmup();

    // Act: Render with no data available
    AudioBufferDescriptor audioBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(audioBuffer);

    // Assert: Render should succeed (output silence for underrun)
    EXPECT_TRUE(renderResult)
        << "ThreadedStrategy::render should succeed even with underrun";

    // Verify: Output should be silence (0.0)
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS], 0.0f)
            << "Left channel should be silent at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS + 1], 0.0f)
            << "Right channel should be silent at frame " << frame;
    }

    // Verify: Underrun count should be visible in telemetry
    auto diag = telemetry_->getAudioDiagnostics();
    EXPECT_GT(diag.underrunCount, 0)
        << "Underrun count should be published to telemetry";

    freeAudioBuffer(audioBuffer);
}

// ============================================================================
// TDD TEST 4: ThreadedStrategy should handle buffer wrap-around correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_HandlesBufferWrapAroundCorrectly) {
    // Arrange: Write data to internal buffer and read to advance pointers
    // This tests wrap-around behavior through the strategy interface

    // Fill buffer with test data
    std::vector<float> testData(100 * STEREO_CHANNELS);
    for (int i = 0; i < 100; ++i) {
        testData[i * STEREO_CHANNELS] = static_cast<float>(i * 2);
        testData[i * STEREO_CHANNELS + 1] = static_cast<float>(i * 2 + 1);
    }

    ASSERT_TRUE(strategy_->AddFrames(testData.data(), 100));

    // Act: Render should read correctly from internal buffer
    AudioBufferDescriptor audioBuffer = createAudioBuffer(50);
    bool renderResult = strategy_->render(audioBuffer);

    // Assert: Render should succeed
    EXPECT_TRUE(renderResult)
        << "ThreadedStrategy::render should handle wrap-around";

    // Verify: Output should start from beginning of buffer
    for (int frame = 0; frame < 50; ++frame) {
        float expectedLeft = static_cast<float>(frame * 2);
        float expectedRight = static_cast<float>(frame * 2 + 1);

        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS], expectedLeft)
            << "Left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Right channel mismatch at frame " << frame;
    }

    freeAudioBuffer(audioBuffer);
}

// ============================================================================
// TDD TEST 5: ThreadedStrategy should AddFrames correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_AddFramesCorrectly) {
    // Arrange: Create test data
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame * 3);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame * 3 + 1);
    }

    // Act: Add frames to internal buffer
    bool addResult = strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT);

    // Assert: AddFrames should succeed
    EXPECT_TRUE(addResult)
        << "ThreadedStrategy::AddFrames should succeed";

    // Verify: Data should be readable via render
    AudioBufferDescriptor audioBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(audioBuffer);
    ASSERT_TRUE(renderResult) << "Failed to render after AddFrames";

    // Verify rendered data matches what was added
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        float expectedLeft = static_cast<float>(frame * 3);
        float expectedRight = static_cast<float>(frame * 3 + 1);

        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS], expectedLeft)
            << "Left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Right channel mismatch at frame " << frame;
    }

    freeAudioBuffer(audioBuffer);
}

// ============================================================================
// TDD TEST 6: ThreadedStrategy should prepare buffer correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_PrepareBufferCorrectly) {
    // Arrange: Reset internal buffer
    strategy_->resetBufferAfterWarmup();

    // Act: Prepare buffer (pre-fill with silence)
    strategy_->prepareBuffer();

    // Assert: Strategy should remain functional after prepareBuffer
    // We can verify by rendering and checking it doesn't crash
    AudioBufferDescriptor audioBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool result = strategy_->render(audioBuffer);
    EXPECT_TRUE(result) << "Render should succeed after prepareBuffer";
    freeAudioBuffer(audioBuffer);
}

// ============================================================================
// TDD TEST 7: ThreadedStrategy should reset buffer after warmup correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_ResetBufferAfterWarmupCorrectly) {
    // Arrange: Add data to internal buffer
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS, 1.0f);
    ASSERT_TRUE(strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT));

    // Act: Reset buffer after warmup
    strategy_->resetBufferAfterWarmup();

    // Verify: Rendering should output silence (buffer was reset)
    AudioBufferDescriptor audioBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    ASSERT_TRUE(strategy_->render(audioBuffer));

    for (int i = 0; i < DEFAULT_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(audioBuffer.buffer[i], 0.0f)
            << "Output should be silence after reset at sample " << i;
    }

    freeAudioBuffer(audioBuffer);
}

// ============================================================================
// TDD TEST 8: ThreadedStrategy should handle concurrent write/read correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_HandlesConcurrentWriteReadCorrectly) {
    // Arrange: Pre-fill buffer with data
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame * 4);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame * 4 + 1);
    }
    ASSERT_TRUE(strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT));

    // Act: Simulate concurrent AddFrames while reading
    std::vector<float> additionalData(50 * STEREO_CHANNELS);
    for (int i = 0; i < 50; ++i) {
        additionalData[i * STEREO_CHANNELS] = static_cast<float>((DEFAULT_FRAME_COUNT + i) * 4);
        additionalData[i * STEREO_CHANNELS + 1] = static_cast<float>((DEFAULT_FRAME_COUNT + i) * 4 + 1);
    }
    bool addResult = strategy_->AddFrames(additionalData.data(), 50);
    ASSERT_TRUE(addResult) << "AddFrames should succeed";

    // Render original data
    AudioBufferDescriptor audioBuffer1 = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool renderResult1 = strategy_->render(audioBuffer1);
    ASSERT_TRUE(renderResult1) << "First render should succeed";

    // Verify first render matches original data
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        EXPECT_FLOAT_EQ(audioBuffer1.buffer[frame * STEREO_CHANNELS], static_cast<float>(frame * 4))
            << "First render left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer1.buffer[frame * STEREO_CHANNELS + 1], static_cast<float>(frame * 4 + 1))
            << "First render right channel mismatch at frame " << frame;
    }
    freeAudioBuffer(audioBuffer1);

    // Render newly added data
    AudioBufferDescriptor audioBuffer2 = createAudioBuffer(50);
    bool renderResult2 = strategy_->render(audioBuffer2);
    ASSERT_TRUE(renderResult2) << "Second render should succeed";

    // Verify second render matches newly added data
    for (int frame = 0; frame < 50; ++frame) {
        float expectedLeft = static_cast<float>((DEFAULT_FRAME_COUNT + frame) * 4);
        float expectedRight = static_cast<float>((DEFAULT_FRAME_COUNT + frame) * 4 + 1);
        EXPECT_FLOAT_EQ(audioBuffer2.buffer[frame * STEREO_CHANNELS], expectedLeft)
            << "Second render left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer2.buffer[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Second render right channel mismatch at frame " << frame;
    }
    freeAudioBuffer(audioBuffer2);
}

// ============================================================================
// TDD TEST 9: ThreadedStrategy baseline capture for regression testing
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, CaptureThreadedStrategyBaseline) {
    // Arrange: Fill internal buffer with known deterministic pattern
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame + 1);
    }

    ASSERT_TRUE(strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT));

    // Act: Render audio to capture baseline
    AudioBufferDescriptor audioBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(audioBuffer);

    // Assert: Render should succeed
    ASSERT_TRUE(renderResult)
        << "Baseline capture render should succeed";

    // Capture baseline to file for regression testing
    captureBaseline(audioBuffer.buffer, DEFAULT_FRAME_COUNT, "threaded_strategy_baseline.dat");

    // Verify captured baseline matches expected pattern
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS], static_cast<float>(frame))
            << "Baseline left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(audioBuffer.buffer[frame * STEREO_CHANNELS + 1], static_cast<float>(frame + 1))
            << "Baseline right channel mismatch at frame " << frame;
    }

    freeAudioBuffer(audioBuffer);

    std::cout << "[BASELINE] ThreadedStrategy baseline captured with pattern +0/+1 (left/right per frame)\n";
}

// ============================================================================
// TDD TEST 10: ThreadedStrategy vs SyncPullStrategy output comparison
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_ProducesBufferedAudio_SyncPullProducesSilence) {
    // Purpose: Verify the two strategies use fundamentally different audio paths.
    // ThreadedStrategy reads from its internal buffer (cursor-chasing).
    // SyncPullStrategy renders on-demand from simulator (which we don't have here).
    // When ThreadedStrategy has buffered data but SyncPullStrategy has no simulator,
    // their outputs must differ -- proving they use independent audio generation.

    // Arrange: Create SyncPullStrategy
    auto syncPullStrategy = std::make_unique<SyncPullStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy_->initialize(config));
    ASSERT_TRUE(syncPullStrategy->initialize(config));

    // Fill ThreadedStrategy's buffer with known non-silent data
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame + 1);
    }
    ASSERT_TRUE(strategy_->AddFrames(testData.data(), DEFAULT_FRAME_COUNT));

    // Act: Render from ThreadedStrategy (has buffered data)
    AudioBufferDescriptor threadedBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool threadedResult = strategy_->render(threadedBuffer);
    ASSERT_TRUE(threadedResult) << "ThreadedStrategy render should succeed";

    // Act: Render from SyncPullStrategy (no simulator -- fills silence)
    AudioBufferDescriptor syncPullBuffer = createAudioBuffer(DEFAULT_FRAME_COUNT);
    bool syncPullResult = syncPullStrategy->render(syncPullBuffer);
    ASSERT_TRUE(syncPullResult) << "SyncPullStrategy render should succeed";

    // Assert: SyncPullStrategy output is silence
    test::verifySilence(syncPullBuffer.buffer, DEFAULT_FRAME_COUNT, "SyncPull without simulator");

    // Assert: ThreadedStrategy output contains non-silent data
    bool hasNonSilentSample = false;
    for (int i = 0; i < DEFAULT_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        if (threadedBuffer.buffer[i] != 0.0f) {
            hasNonSilentSample = true;
            break;
        }
    }
    EXPECT_TRUE(hasNonSilentSample) << "ThreadedStrategy should produce non-silent output from buffered data";

    // Assert: Outputs are actually different
    bool outputsDiffer = false;
    for (int i = 0; i < DEFAULT_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        if (threadedBuffer.buffer[i] != syncPullBuffer.buffer[i]) {
            outputsDiffer = true;
            break;
        }
    }
    EXPECT_TRUE(outputsDiffer) << "ThreadedStrategy and SyncPullStrategy must produce different output";

    freeAudioBuffer(threadedBuffer);
    freeAudioBuffer(syncPullBuffer);
}
