// DebugTest.cpp - Simple debug test to isolate the hanging issue

#include "audio/strategies/ThreadedStrategy.h"
#include "../mocks/MockDataSimulator.h"
#include "AudioTestConstants.h"

#include <gtest/gtest.h>
#include <memory>

using namespace test::constants;

TEST(DebugTest, SimpleInitialization) {
    std::cout << "Test starting..." << std::endl;

    auto strategy = std::make_unique<ThreadedStrategy>(nullptr);
    std::cout << "ThreadedStrategy created..." << std::endl;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    std::cout << "About to call strategy->initialize..." << std::endl;

    bool initResult = strategy->initialize(config);
    std::cout << "Strategy initialize returned: " << (initResult ? "true" : "false") << std::endl;

    ASSERT_TRUE(initResult) << "Failed to initialize ThreadedStrategy";
    std::cout << "Test completed successfully!" << std::endl;
}

TEST(DebugTest, InitializationWithMockSimulator) {
    std::cout << "Test with MockSimulator starting..." << std::endl;

    auto strategy = std::make_unique<ThreadedStrategy>(nullptr);
    std::cout << "ThreadedStrategy created..." << std::endl;

    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    std::cout << "About to call strategy->initialize..." << std::endl;

    bool initResult = strategy->initialize(config);
    std::cout << "Strategy initialize returned: " << (initResult ? "true" : "false") << std::endl;

    ASSERT_TRUE(initResult) << "Failed to initialize ThreadedStrategy";
    std::cout << "Test completed successfully!" << std::endl;
}
