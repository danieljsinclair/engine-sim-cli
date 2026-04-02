// AudioRegressionTest.cpp - Baseline capture tests for ThreadedRenderer
// PURPOSE: Capture baseline output BEFORE refactoring to ensure output remains IDENTICAL
// PRINCIPLE: Before we change anything, we must know what "correct" output looks like

#include "audio/renderers/ThreadedRenderer.h"
#include "audio/common/CircularBuffer.h"
#include "AudioPlayer.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <vector>
#include <fstream>
#include <gtest/gtest.h>
#include <cstring>

using namespace test::constants;

// ============================================================================
// BASELINE CAPTURE TESTS
// These tests capture the exact output of the OLD ThreadedRenderer implementation
// ============================================================================

namespace {
    // Baseline file path (relative to build directory, but we check multiple locations)
    // Files are stored in test/unit/baselines/ in source tree
    // Tests check both current directory and ../test/unit/baselines/
    const char* BASELINE_FILE = "threaded_renderer_baseline.dat";
    const char* BASELINE_DIR = "test/unit/baselines/";

    // Helper to find baseline file (checks current dir and source tree)
    std::string findBaselineFile(const char* basename) {
        // First check current directory
        std::ifstream test(basename);
        if (test.good()) {
            return basename;
        }

        // Then check source tree location
        std::string sourcePath = std::string(BASELINE_DIR) + basename;
        test.open(sourcePath);
        if (test.good()) {
            return sourcePath;
        }

        // Return current directory for write operations
        return basename;
    }

    // Baseline data structure
    struct BaselineData {
        int frameCount;
        int bufferSize;
        int writePointer;
        int readPointer;
        int underrunCount;
        int bufferStatus;
        int64_t totalFramesRead;
        std::vector<float> audioSamples;
    };

    // Store baseline to file
    void storeBaseline(const char* filename, const BaselineData& data) {
        std::ofstream outFile(filename, std::ios::binary);
        if (!outFile) {
            FAIL() << "Failed to open baseline file for writing: " << filename;
        }

        // Write metadata
        outFile.write(reinterpret_cast<const char*>(&data.frameCount), sizeof(data.frameCount));
        outFile.write(reinterpret_cast<const char*>(&data.bufferSize), sizeof(data.bufferSize));
        outFile.write(reinterpret_cast<const char*>(&data.writePointer), sizeof(data.writePointer));
        outFile.write(reinterpret_cast<const char*>(&data.readPointer), sizeof(data.readPointer));
        outFile.write(reinterpret_cast<const char*>(&data.underrunCount), sizeof(data.underrunCount));
        outFile.write(reinterpret_cast<const char*>(&data.bufferStatus), sizeof(data.bufferStatus));
        outFile.write(reinterpret_cast<const char*>(&data.totalFramesRead), sizeof(data.totalFramesRead));

        // Write audio samples
        size_t sampleCount = data.audioSamples.size();
        outFile.write(reinterpret_cast<const char*>(&sampleCount), sizeof(sampleCount));
        outFile.write(reinterpret_cast<const char*>(data.audioSamples.data()),
                      sampleCount * sizeof(float));

        outFile.close();
        std::cout << "[BASELINE] Stored baseline to " << filename << "\n";
    }

    // Load baseline from file
    bool loadBaseline(const char* filename, BaselineData& data) {
        std::string actualPath = findBaselineFile(filename);
        std::ifstream inFile(actualPath, std::ios::binary);
        if (!inFile) {
            return false;
        }

        // Read metadata
        inFile.read(reinterpret_cast<char*>(&data.frameCount), sizeof(data.frameCount));
        inFile.read(reinterpret_cast<char*>(&data.bufferSize), sizeof(data.bufferSize));
        inFile.read(reinterpret_cast<char*>(&data.writePointer), sizeof(data.writePointer));
        inFile.read(reinterpret_cast<char*>(&data.readPointer), sizeof(data.readPointer));
        inFile.read(reinterpret_cast<char*>(&data.underrunCount), sizeof(data.underrunCount));
        inFile.read(reinterpret_cast<char*>(&data.bufferStatus), sizeof(data.bufferStatus));
        inFile.read(reinterpret_cast<char*>(&data.totalFramesRead), sizeof(data.totalFramesRead));

        // Read audio samples
        size_t sampleCount;
        inFile.read(reinterpret_cast<char*>(&sampleCount), sizeof(sampleCount));
        data.audioSamples.resize(sampleCount);
        inFile.read(reinterpret_cast<char*>(data.audioSamples.data()),
                    sampleCount * sizeof(float));

        inFile.close();
        return true;
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
} // anonymous namespace

// ============================================================================
// BASELINE CAPTURE TEST - Captures OLD ThreadedRenderer output
// ============================================================================

TEST(AudioRegressionTest, CaptureThreadedRendererBaseline) {
    // Arrange: Setup OLD ThreadedRenderer with controlled test data
    ThreadedRenderer renderer;
    AudioUnitContext context;

    // Initialize circular buffer with known capacity
    constexpr int bufferCapacity = DEFAULT_BUFFER_CAPACITY;
    context.circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(context.circularBuffer->initialize(bufferCapacity))
        << "Failed to initialize circular buffer with capacity " << bufferCapacity;

    // Create predictable test signal pattern
    // This pattern is designed to test:
    // 1. Normal reading (no wrap)
    // 2. Partial read (underrun)
    // 3. Wrap-around reading
    constexpr int testFrames = TEST_FRAME_COUNT;
    std::vector<float> testSignal(testFrames * STEREO_CHANNELS);

    // Fill with predictable pattern: left channel = frame index, right channel = frame index * 2
    for (int i = 0; i < testFrames; ++i) {
        testSignal[i * STEREO_CHANNELS] = static_cast<float>(i);           // Left
        testSignal[i * STEREO_CHANNELS + 1] = static_cast<float>(i * 2);   // Right
    }

    // Write test signal to buffer
    size_t written = context.circularBuffer->write(testSignal.data(), testFrames);
    ASSERT_EQ(written, testFrames) << "Failed to write all test frames";

    // Set up pointers for normal read scenario
    context.writePointer.store(testFrames);
    context.readPointer.store(0);

    // Create output buffer
    AudioBufferList audioBuffer = createAudioBufferList(testFrames);

    // Act: Render with OLD ThreadedRenderer implementation
    bool renderSuccess = renderer.render(&context, &audioBuffer, testFrames);

    // Assert: Verify render succeeded
    ASSERT_TRUE(renderSuccess) << "Render should succeed";

    // Capture baseline data
    BaselineData baseline;
    baseline.frameCount = testFrames;
    baseline.bufferSize = bufferCapacity;
    baseline.writePointer = context.writePointer.load();
    baseline.readPointer = context.readPointer.load();
    baseline.underrunCount = context.underrunCount.load();
    baseline.bufferStatus = context.bufferStatus;
    baseline.totalFramesRead = context.totalFramesRead.load();

    // Copy audio samples to baseline
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    baseline.audioSamples.assign(data, data + testFrames * STEREO_CHANNELS);

    // Store baseline for future comparison
    storeBaseline(BASELINE_FILE, baseline);

    // Verify baseline captured expected values
    EXPECT_EQ(baseline.frameCount, testFrames);
    EXPECT_EQ(baseline.writePointer, testFrames);
    EXPECT_EQ(baseline.readPointer, testFrames);
    EXPECT_EQ(baseline.underrunCount, 0);
    EXPECT_EQ(baseline.bufferStatus, 0);
    EXPECT_EQ(baseline.totalFramesRead, testFrames);
    EXPECT_EQ(baseline.audioSamples.size(), testFrames * STEREO_CHANNELS);

    // Verify a few sample values to ensure they're correct
    EXPECT_FLOAT_EQ(baseline.audioSamples[0], 0.0f);          // Left channel, frame 0
    EXPECT_FLOAT_EQ(baseline.audioSamples[1], 0.0f);          // Right channel, frame 0
    EXPECT_FLOAT_EQ(baseline.audioSamples[2], 1.0f);          // Left channel, frame 1
    EXPECT_FLOAT_EQ(baseline.audioSamples[3], 2.0f);          // Right channel, frame 1
    EXPECT_FLOAT_EQ(baseline.audioSamples[4], 2.0f);          // Left channel, frame 2
    EXPECT_FLOAT_EQ(baseline.audioSamples[5], 4.0f);          // Right channel, frame 2

    freeAudioBufferList(audioBuffer);

    std::cout << "[BASELINE] Successfully captured ThreadedRenderer baseline\n";
}

// ============================================================================
// BASELINE CAPTURE TEST - Wrap-around scenario
// ============================================================================

TEST(AudioRegressionTest, CaptureThreadedRendererBaseline_WrapAround) {
    // Arrange: Setup scenario where read wraps around buffer boundary
    ThreadedRenderer renderer;
    AudioUnitContext context;

    // Initialize circular buffer
    constexpr int bufferCapacity = DEFAULT_BUFFER_CAPACITY;
    context.circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(context.circularBuffer->initialize(bufferCapacity));

    // Create test signal for wrap-around scenario
    // We'll write frames that span the buffer boundary
    constexpr int wrapPosition = 90;  // Near end of buffer
    constexpr int wrapFrames = 20;    // Frames that will wrap

    // First, fill up to wrapPosition
    std::vector<float> initialData(wrapPosition * STEREO_CHANNELS, 0.0f);
    context.circularBuffer->write(initialData.data(), wrapPosition);

    // Read through those frames to advance internal read pointer
    std::vector<float> consume(wrapPosition * STEREO_CHANNELS);
    context.circularBuffer->read(consume.data(), wrapPosition);

    // Now write frames that will wrap around
    std::vector<float> wrapSignal(wrapFrames * STEREO_CHANNELS);
    for (int i = 0; i < wrapFrames; ++i) {
        wrapSignal[i * STEREO_CHANNELS] = 100.0f + static_cast<float>(i);     // Left
        wrapSignal[i * STEREO_CHANNELS + 1] = 200.0f + static_cast<float>(i); // Right
    }

    context.circularBuffer->write(wrapSignal.data(), wrapFrames);

    // Set up context pointers for wrap-around read
    // Read pointer at wrapPosition, write pointer wrapped to (90 + 20) % 100 = 10
    context.readPointer.store(wrapPosition);
    context.writePointer.store((wrapPosition + wrapFrames) % bufferCapacity);

    // Create output buffer
    AudioBufferList audioBuffer = createAudioBufferList(wrapFrames);

    // Act: Render with OLD ThreadedRenderer implementation
    bool renderSuccess = renderer.render(&context, &audioBuffer, wrapFrames);

    // Assert: Verify render succeeded
    ASSERT_TRUE(renderSuccess);

    // Capture baseline data
    BaselineData baseline;
    baseline.frameCount = wrapFrames;
    baseline.bufferSize = bufferCapacity;
    baseline.writePointer = context.writePointer.load();
    baseline.readPointer = context.readPointer.load();
    baseline.underrunCount = context.underrunCount.load();
    baseline.bufferStatus = context.bufferStatus;
    baseline.totalFramesRead = context.totalFramesRead.load();

    // Copy audio samples to baseline
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    baseline.audioSamples.assign(data, data + wrapFrames * STEREO_CHANNELS);

    // Store baseline for future comparison
    storeBaseline("threaded_renderer_baseline_wrap.dat", baseline);

    // Verify baseline captured expected values
    EXPECT_EQ(baseline.underrunCount, 0);  // Should not underrun
    EXPECT_EQ(baseline.readPointer, (wrapPosition + wrapFrames) % bufferCapacity);
    EXPECT_EQ(baseline.totalFramesRead, wrapFrames);

    // Verify wrap-around data is correct
    EXPECT_FLOAT_EQ(baseline.audioSamples[0], 100.0f);  // Left channel, frame 0
    EXPECT_FLOAT_EQ(baseline.audioSamples[1], 200.0f);  // Right channel, frame 0

    freeAudioBufferList(audioBuffer);

    std::cout << "[BASELINE] Successfully captured ThreadedRenderer wrap-around baseline\n";
}

// ============================================================================
// BASELINE CAPTURE TEST - Underrun scenario
// ============================================================================

TEST(AudioRegressionTest, CaptureThreadedRendererBaseline_Underrun) {
    // Arrange: Setup scenario where buffer underruns
    ThreadedRenderer renderer;
    AudioUnitContext context;

    // Initialize circular buffer
    constexpr int bufferCapacity = DEFAULT_BUFFER_CAPACITY;
    context.circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(context.circularBuffer->initialize(bufferCapacity));

    // Write fewer frames than we'll request
    constexpr int availableFrames = 10;
    constexpr int requestedFrames = TEST_FRAME_COUNT;

    std::vector<float> partialSignal(availableFrames * STEREO_CHANNELS);
    for (int i = 0; i < availableFrames; ++i) {
        partialSignal[i * STEREO_CHANNELS] = 50.0f + static_cast<float>(i);     // Left
        partialSignal[i * STEREO_CHANNELS + 1] = 150.0f + static_cast<float>(i); // Right
    }

    context.circularBuffer->write(partialSignal.data(), availableFrames);
    context.writePointer.store(availableFrames);
    context.readPointer.store(0);

    // Create output buffer
    AudioBufferList audioBuffer = createAudioBufferList(requestedFrames);

    // Act: Render with OLD ThreadedRenderer implementation (will underrun)
    bool renderSuccess = renderer.render(&context, &audioBuffer, requestedFrames);

    // Assert: Verify render succeeded but with underrun
    ASSERT_TRUE(renderSuccess);
    EXPECT_GT(context.underrunCount.load(), 0);

    // Capture baseline data
    BaselineData baseline;
    baseline.frameCount = requestedFrames;
    baseline.bufferSize = bufferCapacity;
    baseline.writePointer = context.writePointer.load();
    baseline.readPointer = context.readPointer.load();
    baseline.underrunCount = context.underrunCount.load();
    baseline.bufferStatus = context.bufferStatus;
    baseline.totalFramesRead = context.totalFramesRead.load();

    // Copy audio samples to baseline
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    baseline.audioSamples.assign(data, data + requestedFrames * STEREO_CHANNELS);

    // Store baseline for future comparison
    storeBaseline("threaded_renderer_baseline_underrun.dat", baseline);

    // Verify baseline captured expected values
    EXPECT_EQ(baseline.readPointer, availableFrames);
    EXPECT_GT(baseline.underrunCount, 0);
    EXPECT_EQ(baseline.bufferStatus, 2);  // Critical state due to low buffer

    // Verify first frames have audio, rest are silence
    EXPECT_FLOAT_EQ(baseline.audioSamples[0], 50.0f);   // Left channel, frame 0
    EXPECT_FLOAT_EQ(baseline.audioSamples[1], 150.0f);  // Right channel, frame 0

    // Check that remaining frames are silence
    for (int i = availableFrames * STEREO_CHANNELS; i < requestedFrames * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(baseline.audioSamples[i], 0.0f) << "Frame " << i << " should be silence";
    }

    freeAudioBufferList(audioBuffer);

    std::cout << "[BASELINE] Successfully captured ThreadedRenderer underrun baseline\n";
}

// ============================================================================
// COMPARISON TEST - Will FAIL after refactor until output is IDENTICAL
// This test ensures that after refactoring, the output matches the baseline
// ============================================================================

TEST(AudioRegressionTest, CompareOutputToBaseline) {
    // Load the baseline we captured earlier
    BaselineData baseline;
    ASSERT_TRUE(loadBaseline(BASELINE_FILE, baseline))
        << "Baseline file not found. Run CaptureThreadedRendererBaseline first.";

    // Run the same test scenario with current implementation
    ThreadedRenderer renderer;
    AudioUnitContext context;

    // Recreate the exact same scenario
    context.circularBuffer = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(context.circularBuffer->initialize(baseline.bufferSize));

    // Recreate test signal
    std::vector<float> testSignal(baseline.frameCount * STEREO_CHANNELS);
    for (int i = 0; i < baseline.frameCount; ++i) {
        testSignal[i * STEREO_CHANNELS] = static_cast<float>(i);           // Left
        testSignal[i * STEREO_CHANNELS + 1] = static_cast<float>(i * 2);   // Right
    }

    context.circularBuffer->write(testSignal.data(), baseline.frameCount);
    context.writePointer.store(baseline.frameCount);
    context.readPointer.store(0);

    // Render
    AudioBufferList audioBuffer = createAudioBufferList(baseline.frameCount);
    bool renderSuccess = renderer.render(&context, &audioBuffer, baseline.frameCount);
    ASSERT_TRUE(renderSuccess);

    // Compare output to baseline
    float* data = static_cast<float*>(audioBuffer.mBuffers[0].mData);

    // Compare every sample - MUST match exactly
    for (size_t i = 0; i < baseline.audioSamples.size(); ++i) {
        EXPECT_FLOAT_EQ(data[i], baseline.audioSamples[i])
            << "Sample " << i << " does not match baseline. "
            << "Expected: " << baseline.audioSamples[i] << ", Got: " << data[i];
    }

    // Compare state
    EXPECT_EQ(context.writePointer.load(), baseline.writePointer)
        << "Write pointer does not match baseline";
    EXPECT_EQ(context.readPointer.load(), baseline.readPointer)
        << "Read pointer does not match baseline";
    EXPECT_EQ(context.underrunCount.load(), baseline.underrunCount)
        << "Underrun count does not match baseline";
    EXPECT_EQ(context.bufferStatus, baseline.bufferStatus)
        << "Buffer status does not match baseline";
    EXPECT_EQ(context.totalFramesRead.load(), baseline.totalFramesRead)
        << "Total frames read does not match baseline";

    freeAudioBufferList(audioBuffer);

    std::cout << "[REGRESSION] Output matches baseline - refactoring preserved behavior\n";
}
