// DebugTest.cpp - Simple debug test to isolate the hanging issue

#include "audio/strategies/ThreadedStrategy.h"
#include "audio/state/BufferContext.h"
#include "audio/common/CircularBuffer.h"
#include "../mocks/MockDataSimulator.h"
#include "AudioTestConstants.h"

#include <gtest/gtest.h>
#include <memory>

using namespace test::constants;

TEST(DebugTest, SimpleInitialization) {
    std::cout << "Test starting..." << std::endl;

    // Create circular buffer
    auto circularBuffer = std::make_unique<CircularBuffer>();
    std::cout << "CircularBuffer created..." << std::endl;

    ASSERT_TRUE(circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY))
        << "Failed to initialize circular buffer";
    std::cout << "CircularBuffer initialized..." << std::endl;

    // Create strategy context
    auto context = std::make_unique<BufferContext>();
    std::cout << "BufferContext created..." << std::endl;

    context->circularBuffer = circularBuffer.get();
    std::cout << "CircularBuffer assigned to context..." << std::endl;

    // Create strategy (nullptr logger)
    auto strategy = std::make_unique<ThreadedStrategy>(nullptr);
    std::cout << "ThreadedStrategy created..." << std::endl;

    // Initialize context with required state
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;
    context->audioState.isPlaying = false;
    std::cout << "Context state initialized..." << std::endl;

    // Initialize strategy
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    std::cout << "About to call strategy->initialize..." << std::endl;

    bool initResult = strategy->initialize(context.get(), config);
    std::cout << "Strategy initialize returned: " << (initResult ? "true" : "false") << std::endl;

    ASSERT_TRUE(initResult) << "Failed to initialize ThreadedStrategy";
    std::cout << "Test completed successfully!" << std::endl;
}

TEST(DebugTest, InitializationWithMockSimulator) {
    std::cout << "Test with MockSimulator starting..." << std::endl;

    // Create mock simulator - COMMENTED OUT TO TEST IF THIS IS THE ISSUE
    // auto mockSimulator = std::make_unique<MockDataSimulator>();
    std::cout << "MockSimulator SKIPPED..." << std::endl;

    // Create circular buffer
    auto circularBuffer = std::make_unique<CircularBuffer>();
    std::cout << "CircularBuffer created..." << std::endl;

    ASSERT_TRUE(circularBuffer->initialize(DEFAULT_BUFFER_CAPACITY))
        << "Failed to initialize circular buffer";
    std::cout << "CircularBuffer initialized..." << std::endl;

    // Create strategy context
    auto context = std::make_unique<BufferContext>();
    std::cout << "BufferContext created..." << std::endl;

    context->circularBuffer = circularBuffer.get();
    std::cout << "CircularBuffer assigned to context..." << std::endl;

    // Create strategy (nullptr logger)
    auto strategy = std::make_unique<ThreadedStrategy>(nullptr);
    std::cout << "ThreadedStrategy created..." << std::endl;

    // Initialize context with required state (without mock simulator)
    context->audioState.sampleRate = DEFAULT_SAMPLE_RATE;
    context->audioState.isPlaying = false;
    std::cout << "Context state initialized..." << std::endl;

    // Initialize strategy
    AudioStrategyConfig config;
    config.sampleRate = DEFAULT_SAMPLE_RATE;
    config.channels = STEREO_CHANNELS;
    std::cout << "About to call strategy->initialize..." << std::endl;

    bool initResult = strategy->initialize(context.get(), config);
    std::cout << "Strategy initialize returned: " << (initResult ? "true" : "false") << std::endl;

    ASSERT_TRUE(initResult) << "Failed to initialize ThreadedStrategy";
    std::cout << "Test completed successfully!" << std::endl;
}
