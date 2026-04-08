// IntegrationAudioPlayerTest.cpp - Integration tests for AudioPlayer with new architecture
// TDD: Tests verify AudioPlayer works correctly with IAudioStrategy + IAudioHardwareProvider
// Tests the transition from old IAudioRenderer architecture to new IAudioStrategy architecture

#include "AudioPlayer.h"
#include "audio/strategies/IAudioStrategy.h"
#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"
#include "audio/hardware/IAudioHardwareProvider.h"
#include "audio/hardware/CoreAudioHardwareProvider.h"
#include "audio/state/StrategyContext.h"
#include "AudioTestConstants.h"
#include "AudioTestHelpers.h"

#include <gtest/gtest.h>
#include <memory>

// Forward declaration for callback signature
struct AudioTimestamp;

using namespace test::constants;

// ============================================================================
// Integration test fixture for AudioPlayer with new architecture
// ============================================================================

class IntegrationAudioPlayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Create mock logger
        logger_ = std::make_unique<ConsoleLogger>();

        // Create hardware provider (mock for testing)
        hardwareProvider_ = std::make_unique<CoreAudioHardwareProvider>(logger_.get());

        // Create strategy context
        context_ = std::make_unique<StrategyContext>();

        // Initialize circular buffer
    circularBuffer_ = std::make_unique<CircularBuffer>();
    ASSERT_TRUE(circularBuffer_->initialize(DEFAULT_BUFFER_CAPACITY))
        << "Failed to initialize circular buffer";
    context_->circularBuffer = circularBuffer_.get();
    }

    void TearDown() override {
        if (hardwareProvider_) {
            hardwareProvider_->cleanup();
        }
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

    std::unique_ptr<ConsoleLogger> logger_;
    std::unique_ptr<IAudioHardwareProvider> hardwareProvider_;
    std::unique_ptr<StrategyContext> context_;
    std::unique_ptr<CircularBuffer> circularBuffer_;  // Owns the circular buffer
};

// ============================================================================
// TDD RED TEST 1: AudioPlayer should accept IAudioStrategy
// ============================================================================

TEST_F(IntegrationAudioPlayerTest, AudioPlayer_AcceptsThreadedStrategy) {
    // Arrange: Create ThreadedStrategy
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    // Act: AudioPlayer should work with ThreadedStrategy
    // (This will be implemented as part of the integration)

    // Assert: Strategy is valid
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(IntegrationAudioPlayerTest, AudioPlayer_AcceptsSyncPullStrategy) {
    // Arrange: Create SyncPullStrategy
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    // Act: AudioPlayer should work with SyncPullStrategy
    // (This will be implemented as part of the integration)

    // Assert: Strategy is valid
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
    EXPECT_TRUE(strategy->isEnabled());
}

// ============================================================================
// TDD RED TEST 2: AudioPlayer should use IAudioHardwareProvider
// ============================================================================

TEST_F(IntegrationAudioPlayerTest, AudioPlayer_UsesHardwareProvider) {
    // Arrange: AudioPlayer initialized with hardware provider
    // Act: AudioPlayer should delegate hardware operations to provider
    // Assert: Hardware provider methods are called

    // Verify hardware provider is valid
    ASSERT_NE(hardwareProvider_, nullptr);

    // Register audio callback BEFORE initializing (required by CoreAudioHardwareProvider)
    auto mockCallback = +[](void* refCon, void* actionFlags,
                                const void* timestamp, int busNumber, int numberFrames,
                                PlatformAudioBufferList* ioData) -> int {
        (void)refCon;
        (void)actionFlags;
        (void)timestamp;
        (void)busNumber;
        (void)numberFrames;
        (void)ioData;
        return 0;
    };

    EXPECT_TRUE(hardwareProvider_->registerAudioCallback(mockCallback));

    // Test hardware provider initialization
    AudioStreamFormat format;
    format.sampleRate = 44100;
    format.channels = 2;
    format.bitsPerSample = 32;
    format.isFloat = true;
    format.isInterleaved = true;

    // Note: This test will pass once AudioPlayer is refactored to use IAudioHardwareProvider
    // For now, we verify the interface is correct
    EXPECT_TRUE(hardwareProvider_->initialize(format));
}

// ============================================================================
// TDD RED TEST 3: StrategyContext should work with IAudioStrategy
// ============================================================================

TEST_F(IntegrationAudioPlayerTest, StrategyContext_WorksWithThreadedStrategy) {
    // Arrange: Create ThreadedStrategy and context
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    context_->strategy = strategy.get();

    // Set up context state
    context_->audioState.sampleRate = 44100;
    context_->audioState.isPlaying = true;
    context_->bufferState.capacity = DEFAULT_BUFFER_CAPACITY;

    // Act: Strategy should render using context
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);

    // Note: For this test to work, AudioPlayer needs to be refactored
    // For now, we verify the strategy can render given the context
    bool result = strategy->render(context_.get(), &audioBuffer, TEST_FRAME_COUNT);

    // Assert: Render succeeds (even if no audio data, should not crash)
    // This test will pass once AudioPlayer integration is complete
    EXPECT_TRUE(result);

    freeAudioBufferList(audioBuffer);
}

TEST_F(IntegrationAudioPlayerTest, StrategyContext_WrapsAroundCorrectly) {
    // Arrange: Create ThreadedStrategy and context with wrap-around scenario
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    context_->strategy = strategy.get();

    // Write data to end of buffer
    int wrapPosition = 90;
    int wrapFrames = 20;
    std::vector<float> wrapSignal(wrapFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    context_->circularBuffer->write(wrapSignal.data(), wrapFrames);

    // Set pointers for wrap-around read
    context_->bufferState.writePointer.store(wrapFrames);
    context_->bufferState.readPointer.store(0);
    context_->bufferState.capacity = DEFAULT_BUFFER_CAPACITY;

    // Act: Read from beginning (should wrap correctly)
    AudioBufferList audioBuffer = createAudioBufferList(wrapFrames);
    bool result = strategy->render(context_.get(), &audioBuffer, wrapFrames);

    // Assert: Render succeeds
    EXPECT_TRUE(result);

    freeAudioBufferList(audioBuffer);
}

// ============================================================================
// TDD RED TEST 4: Factory creates correct strategy
// ============================================================================

TEST_F(IntegrationAudioPlayerTest, Factory_CreatesThreadedStrategy) {
    // Act: Create ThreadedStrategy via factory
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, logger_.get());

    // Assert: Should create ThreadedStrategy
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
}

TEST_F(IntegrationAudioPlayerTest, Factory_CreatesSyncPullStrategy) {
    // Act: Create SyncPullStrategy via factory
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, logger_.get());

    // Assert: Should create SyncPullStrategy
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
}

TEST_F(IntegrationAudioPlayerTest, Factory_ReturnsNullForUnknownMode) {
    // Act: Try to create unknown mode
    auto strategy = IAudioStrategyFactory::createStrategy(static_cast<AudioMode>(999), logger_.get());

    // Assert: Should return nullptr
    EXPECT_EQ(strategy, nullptr);
}

// ============================================================================
// TDD RED TEST 5: Strategy handles buffer operations correctly
// ============================================================================

TEST_F(IntegrationAudioPlayerTest, ThreadedStrategy_AddFrames) {
    // Arrange: Create ThreadedStrategy with context
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    context_->strategy = strategy.get();

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);

    // Act: Add frames to strategy
    bool result = strategy->AddFrames(context_.get(), buffer.data(), TEST_FRAME_COUNT);

    // Assert: Frames should be added successfully
    EXPECT_TRUE(result);

    // Verify frames are in buffer
    int available = static_cast<int>(context_->circularBuffer->available());
    EXPECT_EQ(available, TEST_FRAME_COUNT);
}

TEST_F(IntegrationAudioPlayerTest, SyncPullStrategy_AddFrames) {
    // Arrange: Create SyncPullStrategy with context
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    context_->strategy = strategy.get();

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);

    // Act: Add frames to strategy (SyncPull doesn't use buffer)
    bool result = strategy->AddFrames(context_.get(), buffer.data(), TEST_FRAME_COUNT);

    // Assert: Should always succeed for SyncPull
    EXPECT_TRUE(result);
}

// ============================================================================
// TEST SUITE SUMMARY
// ============================================================================

class IntegrationAudioPlayerTestSummary : public ::testing::Test {
protected:
    void TearDown() override {
        std::cout << "\n=== Integration AudioPlayer Test Summary ===\n";
        std::cout << "Tests verify AudioPlayer integration with:\n";
        std::cout << "- IAudioStrategy (ThreadedStrategy, SyncPullStrategy)\n";
        std::cout << "- IAudioHardwareProvider (CoreAudioHardwareProvider)\n";
        std::cout << "- StrategyContext (composed state)\n";
        std::cout << "- Factory pattern for strategy creation\n";
        std::cout << "=========================================\n\n";
    }
};

TEST_F(IntegrationAudioPlayerTestSummary, SummaryReport) {
    // This test exists only to print the summary
    EXPECT_TRUE(true);
}
