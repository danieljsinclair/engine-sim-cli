// CircularBufferTest.cpp - Unit tests for CircularBuffer
// Tests wrap-around math, pointer arithmetic, capacity management
// TDD approach: RED -> GREEN -> REFACTOR

#include "CircularBuffer.h"
#include "AudioTestHelpers.h"
#include "AudioTestConstants.h"

#include <vector>
#include <gtest/gtest.h>

using namespace test::constants;

// Test fixture for CircularBuffer tests
class CircularBufferTest : public ::testing::Test {
protected:
    void SetUp() override {}
    void TearDown() override {}

    // Helper: Initialize buffer (fails test if init fails)
    void initBuffer(CircularBuffer& buffer, int capacity) {
        ASSERT_TRUE(buffer.initialize(capacity)) << "Failed to initialize buffer with capacity " << capacity;
    }
};

// ============================================================================
// HAPPY PATH TESTS - Core functionality
// ============================================================================

TEST_F(CircularBufferTest, WriteAndRead_SimpleCase_NoWrap) {
    // Arrange: Create buffer with DEFAULT_BUFFER_CAPACITY
    CircularBuffer buffer;
    initBuffer(buffer, DEFAULT_BUFFER_CAPACITY);

    // Act: Write TEST_FRAME_COUNT frames, read TEST_FRAME_COUNT frames
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    size_t written = buffer.write(input.data(), TEST_FRAME_COUNT);

    std::vector<float> output(TEST_FRAME_COUNT * STEREO_CHANNELS);
    size_t read = buffer.read(output.data(), TEST_FRAME_COUNT);

    // Assert: Exact byte match, buffer empty
    EXPECT_EQ(written, TEST_FRAME_COUNT);
    EXPECT_EQ(read, TEST_FRAME_COUNT);
    EXPECT_EQ(buffer.available(), 0);
    EXPECT_EQ(buffer.freeSpace(), DEFAULT_BUFFER_CAPACITY);
    test::validateExactMatch(output.data(), input.data(), TEST_FRAME_COUNT);
}

TEST_F(CircularBufferTest, Available_CalculatesCorrectly) {
    // Arrange: Create buffer
    CircularBuffer buffer;
    initBuffer(buffer, DEFAULT_BUFFER_CAPACITY);

    // Assert: Empty buffer state
    EXPECT_EQ(buffer.available(), 0);
    EXPECT_EQ(buffer.freeSpace(), DEFAULT_BUFFER_CAPACITY);

    // Act: Write partial buffer
    std::vector<float> input(TEST_FRAME_COUNT * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    buffer.write(input.data(), TEST_FRAME_COUNT);

    // Assert: Partial fill state
    EXPECT_EQ(buffer.available(), TEST_FRAME_COUNT);
    EXPECT_EQ(buffer.freeSpace(), DEFAULT_BUFFER_CAPACITY - TEST_FRAME_COUNT);
}

TEST_F(CircularBufferTest, WriteRespectsCapacity) {
    // Arrange: Create small buffer
    CircularBuffer buffer;
    initBuffer(buffer, TINY_BUFFER_CAPACITY);

    // Act: Try to write more than capacity
    std::vector<float> input(DEFAULT_BUFFER_CAPACITY * STEREO_CHANNELS, TEST_SIGNAL_VALUE_1);
    size_t written = buffer.write(input.data(), DEFAULT_BUFFER_CAPACITY);

    // Assert: Only wrote up to capacity (respects buffer size)
    EXPECT_EQ(written, TINY_BUFFER_CAPACITY);

    // NOTE: CircularBuffer has a known limitation where it can't distinguish
    // between "empty" and "full" when write == read. This test documents
    // that the write() method correctly respects capacity, but the state
    // tracking has this edge case ambiguity.
}
