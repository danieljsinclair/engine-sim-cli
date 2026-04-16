// AudioStrategyIntegrationTest.cpp - Integration tests for audio strategy + hardware
// TDD: Tests verify strategy/hardware integration works correctly
// Strategies own their own state -- no BufferContext needed

#include "IAudioStrategy.h"
#include "ThreadedStrategy.h"
#include "SyncPullStrategy.h"
#include "IAudioHardwareProvider.h"
#include "CoreAudioHardwareProvider.h"
#include "AudioTestConstants.h"
#include "AudioTestHelpers.h"

#include <gtest/gtest.h>
#include <memory>

using namespace test::constants;

// ============================================================================
// Integration test fixture
// ============================================================================

class AudioStrategyIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger_ = std::make_unique<ConsoleLogger>();
        hardwareProvider_ = std::make_unique<CoreAudioHardwareProvider>(logger_.get());
    }

    void TearDown() override {
        if (hardwareProvider_) {
            hardwareProvider_->cleanup();
        }
    }

    std::unique_ptr<ConsoleLogger> logger_;
    std::unique_ptr<IAudioHardwareProvider> hardwareProvider_;
};

// ============================================================================
// Tests
// ============================================================================

TEST_F(AudioStrategyIntegrationTest, HardwareProvider_InitAndCallback) {
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

TEST_F(AudioStrategyIntegrationTest, ThreadedStrategy_RendersWithInitializedBuffer) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    // Initialize strategy -- creates internal circular buffer
    AudioStrategyConfig config;
    config.sampleRate = 44100;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    bool result = strategy->render(&audioBuffer, TEST_FRAME_COUNT);
    // With empty buffer, should still succeed (output silence)
    EXPECT_TRUE(result);
    freeAudioBufferList(audioBuffer);
}

TEST_F(AudioStrategyIntegrationTest, ThreadedStrategy_WrapsAroundCorrectly) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = 44100;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    int wrapFrames = 20;
    std::vector<float> wrapSignal(wrapFrames * STEREO_CHANNELS, TEST_SIGNAL_VALUE_3);
    ASSERT_TRUE(strategy->AddFrames(wrapSignal.data(), wrapFrames));

    AudioBufferList audioBuffer = createAudioBufferList(wrapFrames);
    bool result = strategy->render(&audioBuffer, wrapFrames);
    EXPECT_TRUE(result);

    // Verify output matches input
    float* output = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < wrapFrames * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(output[i], TEST_SIGNAL_VALUE_3);
    }

    freeAudioBufferList(audioBuffer);
}

TEST_F(AudioStrategyIntegrationTest, Factory_CreatesThreadedStrategy) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::Threaded, logger_.get());
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "Threaded");
}

TEST_F(AudioStrategyIntegrationTest, Factory_CreatesSyncPullStrategy) {
    auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, logger_.get());
    ASSERT_NE(strategy, nullptr);
    EXPECT_STREQ(strategy->getName(), "SyncPull");
}

TEST_F(AudioStrategyIntegrationTest, Factory_ReturnsNullForUnknownMode) {
    auto strategy = IAudioStrategyFactory::createStrategy(static_cast<AudioMode>(999), logger_.get());
    EXPECT_EQ(strategy, nullptr);
}

TEST_F(AudioStrategyIntegrationTest, ThreadedStrategy_AddFrames) {
    auto strategy = std::make_unique<ThreadedStrategy>(logger_.get());

    AudioStrategyConfig config;
    config.sampleRate = 44100;
    config.channels = STEREO_CHANNELS;
    ASSERT_TRUE(strategy->initialize(config));

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    bool result = strategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);
    EXPECT_TRUE(result);

    // Verify data can be rendered back
    AudioBufferList audioBuffer = createAudioBufferList(TEST_FRAME_COUNT);
    ASSERT_TRUE(strategy->render(&audioBuffer, TEST_FRAME_COUNT));
    float* output = static_cast<float*>(audioBuffer.mBuffers[0].mData);
    for (int i = 0; i < TEST_FRAME_COUNT * STEREO_CHANNELS; ++i) {
        EXPECT_FLOAT_EQ(output[i], TEST_SIGNAL_VALUE_1);
    }
    freeAudioBufferList(audioBuffer);
}

TEST_F(AudioStrategyIntegrationTest, SyncPullStrategy_AddFrames) {
    auto strategy = std::make_unique<SyncPullStrategy>(logger_.get());

    std::vector<float> buffer(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_2);
    bool result = strategy->AddFrames(buffer.data(), TEST_FRAME_COUNT);
    EXPECT_TRUE(result);
}
