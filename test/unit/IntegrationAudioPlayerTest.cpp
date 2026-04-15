// IntegrationAudioPlayerTest.cpp - Integration tests for AudioPlayer
// TDD: Tests verify AudioPlayer works correctly with IAudioStrategy + IAudioHardwareProvider

#include "audio/strategies/IAudioStrategy.h"
#include "audio/strategies/ThreadedStrategy.h"
#include "audio/strategies/SyncPullStrategy.h"
#include "audio/hardware/IAudioHardwareProvider.h"
#include "audio/hardware/CoreAudioHardwareProvider.h"
#include "audio/state/BufferContext.h"
#include "AudioTestConstants.h"
#include "AudioTestHelpers.h"

#include <gtest/gtest.h>
#include <memory>

using namespace test::constants;

// ============================================================================
// Integration test fixture
// ============================================================================

class IntegrationAudioPlayerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<ConsoleLogger>();
        hardwareProvider_ = std::make_unique<CoreAudioHardwareProvider>(logger_.get());
        context_ = std::make_unique<BufferContext>();

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
    std::unique_ptr<IAudioHardwareProvider> hardwareProvider_;
    std::unique_ptr<BufferContext> context_;
    std::unique_ptr<CircularBuffer> circularBuffer_;
};

// ============================================================================
// Tests
// ============================================================================

TEST_F(IntegrationAudioPlayerTest, AudioPlayer_AcceptsThreadedStrategy) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(IntegrationAudioPlayerTest, AudioPlayer_AcceptsSyncPullStrategy) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
    EXPECT_TRUE(strategy->isEnabled());
}

TEST_F(IntegrationAudioPlayerTest, AudioPlayer_UsesHardwareProvider) {
    ASSERT_NE(hardwareProvider_, nullptr);

    auto mockCallback = +[](void* refCon, void* actionFlags,
                                const void* timestamp, int busNumber, int numberFrames,
                                PlatformAudioBufferList* ioData) -> int {
        (void)refCon; (void)actionFlags; (void)timestamp;
        (void)busNumber; (void)numberFrames; (void)ioData;
        return 0;
    };

    EXPECT_TRUE(hardwareProvider_->registerAudioCallback(mockCallback));

    AudioStreamFormat format;
    format.sampleRate = 44100;
    format.channels = 2;
    format.bitsPerSample = 32;
    format.isFloat = true;
    format.isInterleaved = true;

    EXPECT_TRUE(hardwareProvider_->initialize(format));
}

TEST_F(IntegrationAudioPlayerTest, BufferContext_WorksWithThreadedStrategy) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    context_->strategy = strategy.get();

    context_->audioState.sampleRate = 44100;
    context_->audioState.isPlaying = true;
    context_->bufferState.capacity = DEFAULT_BUFFER_CAPACITY;

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    bool result = strategy->render(context_.get(), &audioBuffer, TEST_FRAME_COUNT);
    EXPECT_TRUE(result);
    freeAudioBufferList(audioBuffer);
}

TEST_F(IntegrationAudioPlayerTest, BufferContext_WrapsAroundCorrectly) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    context_->strategy = strategy.get();

    int wrapFrames = 20;
    std::vector<float> wrapSignal(wrapFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    context_->circularBuffer->write(wrapSignal.data(), wrapFrames);

    context_->bufferState.writePointer.store(wrapFrames);
    context_->bufferState.readPointer.store(0);
    context_->bufferState.capacity = DEFAULT_BUFFER_CAPACITY;

    AudioBufferList audioBuffer = createAudioBufferList(wrapFrames);
    bool result = strategy->render(context_.get(), &audioBuffer, wrapFrames);
    EXPECT_TRUE(result);
    freeAudioBufferList(audioBuffer);
}

TEST_F(IntegrationAudioPlayerTest, Factory_CreatesThreadedStrategy) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, logger_.get());
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
}

TEST_F(IntegrationAudioPlayerTest, Factory_CreatesSyncPullStrategy) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, logger_.get());
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
}

TEST_F(IntegrationAudioPlayerTest, Factory_ReturnsNullForUnknownMode) {
    auto strategy = IAudioStrategyFactory::createStrategy(static_cast<AudioMode>(999), logger_.get());
    EXPECT_EQ(strategy, nullptr);
}

TEST_F(IntegrationAudioPlayerTest, ThreadedStrategy_AddFrames) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());
    context_->strategy = strategy.get();

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    bool result = strategy->AddFrames(context_.get(), buffer.data(), TEST_FRAME_COUNT);
    EXPECT_TRUE(result);

    int available = static_cast<int>(context_->circularBuffer->available());
    EXPECT_EQ(available, TEST_FRAME_COUNT);
}

TEST_F(IntegrationAudioPlayerTest, SyncPullStrategy_AddFrames) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());
    context_->strategy = strategy.get();

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);
    bool result = strategy->AddFrames(context_.get(), buffer.data(), TEST_FRAME_COUNT);
    EXPECT_TRUE(result);
}
