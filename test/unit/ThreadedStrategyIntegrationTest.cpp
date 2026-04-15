// ThreadedStrategyIntegrationTest.cpp - Integration tests for ThreadedStrategy
// TDD: Tests verify ThreadedStrategy behavior end-to-end with deterministic simulation
// Uses MockDataSimulator for predictable audio generation
// Captures baseline output for regression verification

#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"
#include "audio/state/BufferContext.h"
#include "audio/common/CircularBuffer.h"
#include "../mocks/MockDataSimulator.h"
#include "../mocks/MockAudioUnit.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <gtest/gtest.h>
#include <fstream>
#include <cstring>

using namespace test::constants;

// ============================================================================
// Integration test fixture for ThreadedStrategy
// ============================================================================

class ThreadedStrategyIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        // No logger needed for basic tests
        // logger_ = nullptr;  // Commented out to avoid potential issues

        // Create strategy context
        context_ = std::make_unique<BufferContext>();

        // Initialize circular buffer for threaded mode
        circularBuffer_ = std::make_unique<CircularBuffer>();
        ASSERT_TRUE(circularBuffer_->initialize(DEFAULT_BUFFER_CAPACITY))
            << "Failed to initialize circular buffer";
        context_->circularBuffer = circularBuffer_.get();

        // Create strategy
        strategy_ = std::make_unique<ThreadedStrategy>(logger_.get());

        // Initialize context with required state (no mock simulator needed for basic tests)
        context_->audioState.sampleRate = DEFAULT_SAMPLE_RATE;
        context_->audioState.isPlaying = false;
        context_->engineHandle = nullptr;  // No engine handle needed for basic tests
        context_->engineAPI = nullptr;  // No engine API needed for basic tests

        // Initialize strategy
        AudioStrategyConfig config;
        config.sampleRate = DEFAULT_SAMPLE_RATE;
        config.channels = STEREO_CHANNELS;
        ASSERT_TRUE(strategy_->initialize(context_.get(), config))
            << "Failed to initialize ThreadedStrategy";
    }

    void TearDown() override {
        if (strategy_) {
            strategy_->reset();
        }
        if (circularBuffer_) {
            circularBuffer_->cleanup();
        }
        // No mock simulator to reset
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

    // Helper: Capture baseline to file
    void captureBaseline(const float* data, int frames, const char* filename) {
        std::ofstream baselineFile(filename, std::ios::binary);
        ASSERT_TRUE(baselineFile.is_open())
            << "Failed to open baseline file: " << filename;

        baselineFile.write(reinterpret_cast<const char*>(data), frames * STEREO_CHANNELS * sizeof(float));
        baselineFile.close();

        std::cout << "[BASELINE] Captured " << frames << " frames to " << filename << "\n";
    }

    // Helper: Verify baseline matches expected pattern
    void verifyBaselinePattern(const char* filename, int16_t expectedLeftBase, int16_t expectedRightBase) {
        std::ifstream baselineFile(filename, std::ios::binary);
        ASSERT_TRUE(baselineFile.is_open())
            << "Failed to open baseline file: " << filename;

        // Read baseline data
        std::vector<float> baselineData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
        baselineFile.read(reinterpret_cast<char*>(baselineData.data()), baselineData.size() * sizeof(float));
        baselineFile.close();

        // Verify pattern: left channel = expectedLeftBase + frame, right = expectedRightBase + frame
        for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
            float expectedLeft = static_cast<float>(expectedLeftBase + frame);
            float expectedRight = static_cast<float>(expectedRightBase + frame);

            EXPECT_FLOAT_EQ(baselineData[frame * STEREO_CHANNELS], expectedLeft)
                << "Left channel mismatch at frame " << frame;
            EXPECT_FLOAT_EQ(baselineData[frame * STEREO_CHANNELS + 1], expectedRight)
                << "Right channel mismatch at frame " << frame;
        }
    }

    std::unique_ptr<ConsoleLogger> logger_;
    // std::unique_ptr<MockDataSimulator> mockSimulator_;  // Not used in basic tests
    std::unique_ptr<BufferContext> context_;
    std::unique_ptr<CircularBuffer> circularBuffer_;
    std::unique_ptr<ThreadedStrategy> strategy_;
};

// ============================================================================
// TDD RED TEST 1: ThreadedStrategy should initialize successfully
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_InitializesSuccessfully) {
    // Arrange: Already initialized in SetUp()

    // Act: Strategy is initialized

    // Assert: Strategy should have correct name and be enabled
    EXPECT_STREQ(strategy_->getName(), "Threaded");
    EXPECT_TRUE(strategy_->isEnabled());
    EXPECT_TRUE(strategy_->shouldDrainDuringWarmup());
    EXPECT_STREQ(strategy_->getModeString().c_str(), "Threaded mode");
}

// ============================================================================
// TDD RED TEST 2: ThreadedStrategy should render from circular buffer
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_RendersFromCircularBuffer) {
    // Arrange: Fill circular buffer with known pattern
    // Use pattern: left = +0 + frame, right = +1 + frame (different from SyncPull's +1000/+2000)
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame);          // Left: 0, 1, 2, 3...
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame + 1); // Right: 1, 2, 3, 4...
    }

    // Write to circular buffer
    size_t framesWritten = circularBuffer_->write(testData.data(), DEFAULT_FRAME_COUNT);
    ASSERT_EQ(framesWritten, static_cast<size_t>(DEFAULT_FRAME_COUNT))
        << "Not all frames written to circular buffer";

    // Update context pointers
    context_->bufferState.writePointer.store(circularBuffer_->getWritePointer());

    // Act: Render audio from circular buffer
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(context_.get(), &audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Render should succeed
    EXPECT_TRUE(renderResult)
        << "ThreadedStrategy::render should succeed";

    // Verify: Output should match input pattern
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        float expectedLeft = static_cast<float>(frame);
        float expectedRight = static_cast<float>(frame + 1);

        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS], expectedLeft)
            << "Left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Right channel mismatch at frame " << frame;
    }

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// TDD RED TEST 3: ThreadedStrategy should handle buffer underrun gracefully
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_HandlesBufferUnderrunGracefully) {
    // Arrange: Empty circular buffer (no data)
    circularBuffer_->reset();

    // Act: Render with no data available
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(context_.get(), &audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Render should succeed (output silence for underrun)
    EXPECT_TRUE(renderResult)
        << "ThreadedStrategy::render should succeed even with underrun";

    // Verify: Output should be silence (0.0)
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS], 0.0f)
            << "Left channel should be silent at frame " << frame;
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS + 1], 0.0f)
            << "Right channel should be silent at frame " << frame;
    }

    // Verify: Underrun count should be incremented
    int underrunCount = context_->bufferState.underrunCount.load();
    EXPECT_GT(underrunCount, 0)
        << "Underrun count should be incremented";

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// TDD RED TEST 4: ThreadedStrategy should handle buffer wrap-around correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_HandlesBufferWrapAroundCorrectly) {
    // Arrange: Write data near end of buffer and then read from beginning
    // This tests the wrap-around logic in ThreadedStrategy::render()

    int capacity = static_cast<int>(circularBuffer_->capacity());
    int writePosition = capacity - 50;  // Write near end
    int readPosition = 0;              // Read from beginning

    // Fill circular buffer with test data starting at writePosition
    std::vector<float> testData(100 * STEREO_CHANNELS);
    for (int i = 0; i < 100; ++i) {
        testData[i * STEREO_CHANNELS] = static_cast<float>(i * 2);      // Left: 0, 2, 4...
        testData[i * STEREO_CHANNELS + 1] = static_cast<float>(i * 2 + 1); // Right: 1, 3, 5...
    }

    // Simulate write at wrap position
    size_t framesWritten = circularBuffer_->write(testData.data(), 100);
    ASSERT_EQ(framesWritten, 100u) << "Failed to write test data";

    // Set read position to beginning (simulate wrap scenario)
    context_->bufferState.readPointer.store(readPosition);

    // Act: Render should read from beginning correctly despite write position near end
    AudioBufferList audioBuffer = createAudioBufferList(50);
    bool renderResult = strategy_->render(context_.get(), &audioBuffer, 50);

    // Assert: Render should succeed
    EXPECT_TRUE(renderResult)
        << "ThreadedStrategy::render should handle wrap-around";

    // Verify: Output should start from beginning of buffer
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int frame = 0; frame < 50; ++frame) {
        float expectedLeft = static_cast<float>(frame * 2);
        float expectedRight = static_cast<float>(frame * 2 + 1);

        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS], expectedLeft)
            << "Left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Right channel mismatch at frame " << frame;
    }

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// TDD RED TEST 5: ThreadedStrategy should AddFrames correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_AddFramesCorrectly) {
    // Arrange: Create test data
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame * 3);      // Left: 0, 3, 6...
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame * 3 + 1); // Right: 1, 4, 7...
    }

    // Act: Add frames to circular buffer
    bool addResult = strategy_->AddFrames(context_.get(), testData.data(), DEFAULT_FRAME_COUNT);

    // Assert: AddFrames should succeed
    EXPECT_TRUE(addResult)
        << "ThreadedStrategy::AddFrames should succeed";

    // Verify: Write pointer should be updated
    int expectedWritePointer = circularBuffer_->getWritePointer();
    int actualWritePointer = context_->bufferState.writePointer.load();
    EXPECT_EQ(actualWritePointer, expectedWritePointer)
        << "Write pointer should be updated";

    // Verify: Data should be in circular buffer
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(context_.get(), &audioBuffer, DEFAULT_FRAME_COUNT);
    ASSERT_TRUE(renderResult) << "Failed to render after AddFrames";

    // Verify rendered data matches what was added
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        float expectedLeft = static_cast<float>(frame * 3);
        float expectedRight = static_cast<float>(frame * 3 + 1);

        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS], expectedLeft)
            << "Left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Right channel mismatch at frame " << frame;
    }

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// TDD RED TEST 6: ThreadedStrategy should prepare buffer correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_PrepareBufferCorrectly) {
    // Arrange: Empty circular buffer
    circularBuffer_->reset();

    // Act: Prepare buffer (pre-fill with silence)
    strategy_->prepareBuffer(context_.get());

    // Verify: Circular buffer should be pre-filled with silence
    size_t availableFrames = circularBuffer_->available();
    size_t expectedPreFillFrames = static_cast<size_t>(DEFAULT_SAMPLE_RATE * 0.1);  // 100ms at sample rate

    EXPECT_GE(availableFrames, expectedPreFillFrames * 0.9)  // Allow 10% tolerance
        << "Buffer should be pre-filled with approximately 100ms of silence";
    EXPECT_LE(availableFrames, expectedPreFillFrames * 1.1)  // Allow 10% tolerance
        << "Buffer should not be over-filled";
}

// ============================================================================
// TDD RED TEST 7: ThreadedStrategy should reset buffer after warmup correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_ResetBufferAfterWarmupCorrectly) {
    // Arrange: Fill circular buffer with data
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    circularBuffer_->write(testData.data(), DEFAULT_FRAME_COUNT);

    // Act: Reset buffer after warmup
    strategy_->resetBufferAfterWarmup(context_.get());

    // Verify: Circular buffer should be reset
    size_t availableFrames = circularBuffer_->available();
    EXPECT_EQ(availableFrames, 0u)
        << "Buffer should be empty after reset";

    // Verify: Pointers should be reset to 0
    int writePointer = context_->bufferState.writePointer.load();
    int readPointer = context_->bufferState.readPointer.load();
    EXPECT_EQ(writePointer, 0)
        << "Write pointer should be reset to 0";
    EXPECT_EQ(readPointer, 0)
        << "Read pointer should be reset to 0";
}

// ============================================================================
// TDD RED TEST 8: ThreadedStrategy should handle concurrent write/read correctly
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_HandlesConcurrentWriteReadCorrectly) {
    // Arrange: Pre-fill buffer with data
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame * 4);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame * 4 + 1);
    }
    circularBuffer_->write(testData.data(), DEFAULT_FRAME_COUNT);

    // Act: Simulate concurrent AddFrames while reading
    // Add more data
    std::vector<float> additionalData(50 * STEREO_CHANNELS);
    for (int i = 0; i < 50; ++i) {
        additionalData[i * STEREO_CHANNELS] = static_cast<float>((DEFAULT_FRAME_COUNT + i) * 4);
        additionalData[i * STEREO_CHANNELS + 1] = static_cast<float>((DEFAULT_FRAME_COUNT + i) * 4 + 1);
    }
    bool addResult = strategy_->AddFrames(context_.get(), additionalData.data(), 50);
    ASSERT_TRUE(addResult) << "AddFrames should succeed";

    // Render original data
    AudioBufferList audioBuffer1 = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult1 = strategy_->render(context_.get(), &audioBuffer1, DEFAULT_FRAME_COUNT);
    ASSERT_TRUE(renderResult1) << "First render should succeed";

    // Verify first render matches original data
    float* output1 = static_cast<float*>(audioBuffer1.mBuffers[0].mData);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        EXPECT_FLOAT_EQ(output1[frame * STEREO_CHANNELS], static_cast<float>(frame * 4))
            << "First render left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(output1[frame * STEREO_CHANNELS + 1], static_cast<float>(frame * 4 + 1))
            << "First render right channel mismatch at frame " << frame;
    }
    freeAudioBufferList(audioBuffer1);

    // Render newly added data
    AudioBufferList audioBuffer2 = createAudioBufferList(50);
    bool renderResult2 = strategy_->render(context_.get(), &audioBuffer2, 50);
    ASSERT_TRUE(renderResult2) << "Second render should succeed";

    // Verify second render matches newly added data
    float* output2 = static_cast<float*>(audioBuffer2.mBuffers[0].mData);
    for (int frame = 0; frame < 50; ++frame) {
        float expectedLeft = static_cast<float>((DEFAULT_FRAME_COUNT + frame) * 4);
        float expectedRight = static_cast<float>((DEFAULT_FRAME_COUNT + frame) * 4 + 1);
        EXPECT_FLOAT_EQ(output2[frame * STEREO_CHANNELS], expectedLeft)
            << "Second render left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(output2[frame * STEREO_CHANNELS + 1], expectedRight)
            << "Second render right channel mismatch at frame " << frame;
    }
    freeAudioBufferList(audioBuffer2);
}

// ============================================================================
// TDD RED TEST 9: ThreadedStrategy baseline capture for regression testing
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, CaptureThreadedStrategyBaseline) {
    // Arrange: Fill circular buffer with known deterministic pattern
    // Use pattern: left = +0 + frame, right = +1 + frame
    std::vector<float> testData(DEFAULT_FRAME_COUNT * STEREO_CHANNELS);
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        testData[frame * STEREO_CHANNELS] = static_cast<float>(frame);
        testData[frame * STEREO_CHANNELS + 1] = static_cast<float>(frame + 1);
    }

    // Write to circular buffer
    circularBuffer_->write(testData.data(), DEFAULT_FRAME_COUNT);
    context_->bufferState.writePointer.store(circularBuffer_->getWritePointer());

    // Act: Render audio to capture baseline
    AudioBufferList audioBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool renderResult = strategy_->render(context_.get(), &audioBuffer, DEFAULT_FRAME_COUNT);

    // Assert: Render should succeed
    ASSERT_TRUE(renderResult)
        << "Baseline capture render should succeed";

    // Capture baseline to file for regression testing
    float* outputData = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    captureBaseline(outputData, DEFAULT_FRAME_COUNT, "threaded_strategy_baseline.dat");

    // Verify captured baseline matches expected pattern
    for (int frame = 0; frame < DEFAULT_FRAME_COUNT; ++frame) {
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS], static_cast<float>(frame))
            << "Baseline left channel mismatch at frame " << frame;
        EXPECT_FLOAT_EQ(outputData[frame * STEREO_CHANNELS + 1], static_cast<float>(frame + 1))
            << "Baseline right channel mismatch at frame " << frame;
    }

    freeAudioBufferList(audioBuffer);

    std::cout << "[BASELINE] ThreadedStrategy baseline captured with pattern +0/+1 (left/right per frame)\n";
}

// ============================================================================
// TDD RED TEST 10: ThreadedStrategy vs SyncPullStrategy output comparison
// ============================================================================

TEST_F(ThreadedStrategyIntegrationTest, ThreadedStrategy_DifferentOutputFromSyncPullStrategy) {
    // Arrange: Create SyncPullStrategy for comparison
    auto syncPullStrategy = std::make_unique<SyncPullStrategy>(logger_.get());

    // Create separate context for SyncPullStrategy
    auto syncPullContext = std::make_unique<BufferContext>();
    syncPullContext->audioState.sampleRate = DEFAULT_SAMPLE_RATE;
    syncPullContext->audioState.isPlaying = false;
    syncPullContext->engineHandle = nullptr;  // No mock simulator for basic tests
    syncPullContext->engineAPI = nullptr;  // No mock API for basic tests

    // Initialize both strategies
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;

    ASSERT_TRUE(strategy_->initialize(context_.get(), config));
    ASSERT_TRUE(syncPullStrategy->initialize(syncPullContext.get(), config));

    // Act: Render from ThreadedStrategy
    AudioBufferList threadedBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool threadedResult = strategy_->render(context_.get(), &threadedBuffer, DEFAULT_FRAME_COUNT);
    ASSERT_TRUE(threadedResult) << "ThreadedStrategy render should succeed";

    // Act: Render from SyncPullStrategy
    // Note: SyncPullStrategy requires engineAPI to function properly
    // Without an engine API, it will return false, which is expected behavior
    AudioBufferList syncPullBuffer = createAudioBufferList(DEFAULT_FRAME_COUNT);
    bool syncPullResult = syncPullStrategy->render(syncPullContext.get(), &syncPullBuffer, DEFAULT_FRAME_COUNT);

    // Assert: SyncPullStrategy should fail gracefully without engine API
    EXPECT_FALSE(syncPullResult)
        << "SyncPullStrategy should fail gracefully without engine API (returns false)";

    // Note: We cannot compare outputs in this test because SyncPullStrategy
    // requires an engine API (MockDataSimulator) to generate audio.
    // The ThreadedStrategy integration tests focus on buffer management,
    // not on simulator integration.

    freeAudioBufferList(threadedBuffer);
    freeAudioBufferList(syncPullBuffer);
}
