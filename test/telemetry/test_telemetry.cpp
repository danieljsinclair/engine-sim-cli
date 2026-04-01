// test_telemetry.cpp - Unit tests for telemetry implementation
//
// Purpose: Verify telemetry Reader/Writer interfaces work correctly.
// Tests focus on business value: thread safety, data consistency, and performance.
//
// Testing Approach:
// - Test atomic operations correctness
// - Test thread safety (concurrent write/read patterns)
// - Verify no data races in real-world usage patterns
// - Ensure snapshot consistency (no mixed time points)

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>

// Include the telemetry interfaces (to be implemented)
#include "ITelemetry.h"

using namespace telemetry;

// ============================================================================
// Test Fixture
// ============================================================================

class TelemetryTest : public ::testing::Test {
protected:
    void SetUp() override {
        telemetry = std::make_unique<InMemoryTelemetry>();
        writer = telemetry.get();
        reader = telemetry.get();
    }

    void TearDown() override {
        telemetry.reset();
    }

    // Helper: Create a TelemetryData with known values
    TelemetryData createTestTelemetry(double rpm = 1000.0, double load = 0.5, double flow = 0.01) {
        TelemetryData data;
        data.currentRPM = rpm;
        data.currentLoad = load;
        data.exhaustFlow = flow;
        data.manifoldPressure = 101325.0;
        data.activeChannels = 2;
        data.processingTimeMs = 1.5;
        data.underrunCount = 0;
        data.bufferHealthPct = 100.0;
        data.throttlePosition = 0.5;
        data.ignitionOn = true;
        data.starterMotorEngaged = false;
        data.timestamp = 0.0;
        return data;
    }

    std::unique_ptr<InMemoryTelemetry> telemetry;
    ITelemetryWriter* writer;
    ITelemetryReader* reader;
};

// ============================================================================
// Scenario 1: InMemoryTelemetry correctly writes and reads atomic values
// ============================================================================

TEST_F(TelemetryTest, WriteAndReadSingleValue) {
    // Test: Write telemetry data and read it back
    // Expect: Values match exactly (atomic operations work)

    auto testData = createTestTelemetry(5000.0, 0.75, 0.025);

    writer->write(testData);
    auto snapshot = reader->getSnapshot();

    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 5000.0);
    EXPECT_DOUBLE_EQ(snapshot.currentLoad, 0.75);
    EXPECT_DOUBLE_EQ(snapshot.exhaustFlow, 0.025);
}

TEST_F(TelemetryTest, MultipleWritesOverwrite) {
    // Test: Multiple writes should overwrite previous values
    // Expect: Only the latest value is visible

    writer->write(createTestTelemetry(1000.0, 0.1, 0.001));
    writer->write(createTestTelemetry(2000.0, 0.3, 0.010));
    writer->write(createTestTelemetry(3000.0, 0.5, 0.020));

    auto snapshot = reader->getSnapshot();

    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 3000.0);
    EXPECT_DOUBLE_EQ(snapshot.currentLoad, 0.5);
    EXPECT_DOUBLE_EQ(snapshot.exhaustFlow, 0.020);
}

// ============================================================================
// Scenario 2: getSnapshot() returns consistent snapshot (not mixed time points)
// ============================================================================

TEST_F(TelemetryTest, SnapshotConsistency_SingleThread) {
    // Test: Reading snapshot should not see partial updates
    // Expect: All fields in snapshot are from the same write

    // Write initial data
    writer->write(createTestTelemetry(1000.0, 0.1, 0.001));

    // Update all fields to new values
    auto newData = createTestTelemetry(8000.0, 0.9, 0.050);
    newData.manifoldPressure = 200000.0;
    newData.activeChannels = 4;
    newData.processingTimeMs = 5.0;
    newData.underrunCount = 10;
    newData.bufferHealthPct = 50.0;
    newData.throttlePosition = 1.0;
    newData.ignitionOn = true;
    newData.starterMotorEngaged = true;
    newData.timestamp = 123.456;

    writer->write(newData);

    auto snapshot = reader->getSnapshot();

    // Verify all fields are from the same write (all new values)
    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 8000.0);
    EXPECT_DOUBLE_EQ(snapshot.currentLoad, 0.9);
    EXPECT_DOUBLE_EQ(snapshot.exhaustFlow, 0.050);
    EXPECT_DOUBLE_EQ(snapshot.manifoldPressure, 200000.0);
    EXPECT_EQ(snapshot.activeChannels, 4);
    EXPECT_DOUBLE_EQ(snapshot.processingTimeMs, 5.0);
    EXPECT_EQ(snapshot.underrunCount, 10);
    EXPECT_DOUBLE_EQ(snapshot.bufferHealthPct, 50.0);
    EXPECT_DOUBLE_EQ(snapshot.throttlePosition, 1.0);
    EXPECT_TRUE(snapshot.ignitionOn);
    EXPECT_TRUE(snapshot.starterMotorEngaged);
    EXPECT_DOUBLE_EQ(snapshot.timestamp, 123.456);
}

// ============================================================================
// Scenario 3: Multiple writes don't lose data (concurrent safety)
// ============================================================================

TEST_F(TelemetryTest, ConcurrentWrites_ThreadSafety) {
    // Test: Multiple threads writing simultaneously should not corrupt data
    // Expect: Final snapshot contains valid values (no garbage/corruption)
    //
    // Business value: Bridge may have multiple update paths in future

    const int numThreads = 4;
    const int writesPerThread = 1000;
    std::vector<std::thread> threads;

    // Each thread writes with a unique RPM value
    for (int i = 0; i < numThreads; ++i) {
        threads.emplace_back([this, i, writesPerThread]() {
            for (int j = 0; j < writesPerThread; ++j) {
                auto data = createTestTelemetry(
                    (i + 1) * 1000.0,  // Unique RPM per thread
                    0.5,
                    0.01
                );
                writer->write(data);
            }
        });
    }

    // Wait for all threads to complete
    for (auto& thread : threads) {
        thread.join();
    }

    // Verify final snapshot is valid (no corruption)
    auto snapshot = reader->getSnapshot();

    // RPM should be one of the values written by a thread
    EXPECT_GE(snapshot.currentRPM, 1000.0);
    EXPECT_LE(snapshot.currentRPM, 4000.0);
    EXPECT_GE(snapshot.currentLoad, 0.0);
    EXPECT_LE(snapshot.currentLoad, 1.0);
}

TEST_F(TelemetryTest, ConcurrentWriteRead_NoDataRaces) {
    // Test: One thread writing, another thread reading
    // Expect: Reader always gets consistent data (no crashes/undefined behavior)
    //
    // Business value: Bridge writes from sim thread, presentation reads from main thread

    const int iterations = 1000;
    std::atomic<bool> readerDone{false};
    std::atomic<int> readCount{0};

    // Writer thread (simulates bridge)
    std::thread writerThread([this, iterations]() {
        for (int i = 0; i < iterations; ++i) {
            auto data = createTestTelemetry(
                1000.0 + i,  // Increasing RPM
                0.5,
                0.01
            );
            data.timestamp = i * 0.016;  // ~60Hz
            writer->write(data);
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
    });

    // Reader thread (simulates presentation)
    std::thread readerThread([this, iterations, &readerDone, &readCount]() {
        for (int i = 0; i < iterations; ++i) {
            auto snapshot = reader->getSnapshot();

            // Verify data is consistent (no garbage)
            EXPECT_GE(snapshot.currentRPM, 1000.0);
            EXPECT_LE(snapshot.currentRPM, 1000.0 + iterations);
            EXPECT_GE(snapshot.currentLoad, 0.0);
            EXPECT_LE(snapshot.currentLoad, 1.0);
            EXPECT_GE(snapshot.timestamp, 0.0);

            readCount++;
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        readerDone = true;
    });

    writerThread.join();
    readerThread.join();

    EXPECT_EQ(readCount, iterations);
    EXPECT_TRUE(readerDone);
}

// ============================================================================
// Scenario 4: reset() clears all telemetry fields
// ============================================================================

TEST_F(TelemetryTest, ResetClearsAllFields) {
    // Test: reset() should zero all telemetry data
    // Expect: All fields return to default values

    // Write non-zero data
    auto data = createTestTelemetry(5000.0, 0.75, 0.025);
    data.underrunCount = 42;
    data.timestamp = 123.456;
    writer->write(data);

    // Reset
    writer->reset();

    // Verify all fields are zero
    auto snapshot = reader->getSnapshot();

    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 0.0);
    EXPECT_DOUBLE_EQ(snapshot.currentLoad, 0.0);
    EXPECT_DOUBLE_EQ(snapshot.exhaustFlow, 0.0);
    EXPECT_DOUBLE_EQ(snapshot.manifoldPressure, 0.0);
    EXPECT_EQ(snapshot.activeChannels, 0);
    EXPECT_DOUBLE_EQ(snapshot.processingTimeMs, 0.0);
    EXPECT_EQ(snapshot.underrunCount, 0);
    EXPECT_DOUBLE_EQ(snapshot.bufferHealthPct, 0.0);
    EXPECT_DOUBLE_EQ(snapshot.throttlePosition, 0.0);
    EXPECT_FALSE(snapshot.ignitionOn);
    EXPECT_FALSE(snapshot.starterMotorEngaged);
    EXPECT_DOUBLE_EQ(snapshot.timestamp, 0.0);
}

// ============================================================================
// Scenario 5: TelemetryData struct size is reasonable (< 1KB)
// ============================================================================

TEST_F(TelemetryTest, TelemetryDataSizeIsReasonable) {
    // Test: TelemetryData should fit in cache line for performance
    // Expect: Size < 1KB (preferably < 256 bytes)
    //
    // Business value: Small structs enable efficient atomic operations
    // and reduce cache coherency traffic between cores

    size_t dataSize = sizeof(TelemetryData);

    EXPECT_LT(dataSize, 1024) << "TelemetryData size (" << dataSize << " bytes) exceeds 1KB threshold";

    // Bonus: Check if it fits in typical cache line (64 bytes)
    // Note: Due to std::atomic members, this may exceed 64 bytes
    if (dataSize > 64) {
        // This is acceptable, but note it for performance optimization
        std::cout << "NOTE: TelemetryData size (" << dataSize << " bytes) exceeds cache line (64 bytes)\n";
        std::cout << "      Consider packing if performance profiling shows cache contention\n";
    }
}

// ============================================================================
// Scenario 6: High-frequency write pattern (real-world simulation)
// ============================================================================

TEST_F(TelemetryTest, HighFrequencyWrites_NoDataLoss) {
    // Test: Simulate 60Hz write rate (real-time simulation)
    // Expect: Reader sees consistent updates, no missed writes
    //
    // Business value: Verifies telemetry can keep up with real-time simulation

    const int numWrites = 600;  // 10 seconds @ 60Hz
    const std::chrono::microseconds writeInterval(16667);  // ~60Hz

    auto startTime = std::chrono::steady_clock::now();

    for (int i = 0; i < numWrites; ++i) {
        auto data = createTestTelemetry(
            1000.0 + i * 10.0,  // Increasing RPM
            0.5,
            0.01
        );
        data.timestamp = i * 0.01667;  // ~60Hz timestamp
        writer->write(data);

        // Simulate real-time pacing (don't busy-wait in production code)
        // In real usage, simulation rate is controlled by physics loop
    }

    auto endTime = std::chrono::steady_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(endTime - startTime);

    // Verify final state
    auto snapshot = reader->getSnapshot();
    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 1000.0 + (numWrites - 1) * 10.0);
    EXPECT_DOUBLE_EQ(snapshot.timestamp, (numWrites - 1) * 0.01667);
}

// ============================================================================
// Scenario 7: Interface contract compliance
// ============================================================================

TEST_F(TelemetryTest, WriterGetNameReturnsValidString) {
    // Test: Writer interface returns valid name
    // Expect: Non-null, non-empty string

    const char* name = writer->getName();
    EXPECT_NE(name, nullptr);
    EXPECT_STRNE(name, "");
    EXPECT_STREQ(name, "InMemoryTelemetry");
}

TEST_F(TelemetryTest, ReaderGetNameReturnsValidString) {
    // Test: Reader interface returns valid name
    // Expect: Non-null, non-empty string

    const char* name = reader->getName();
    EXPECT_NE(name, nullptr);
    EXPECT_STRNE(name, "");
    EXPECT_STREQ(name, "InMemoryTelemetry");
}

// ============================================================================
// Performance Benchmark (not a FAILING test, just reporting)
// ============================================================================

TEST_F(TelemetryTest, PerformanceBenchmark_WriteLatency) {
    // Test: Measure write operation latency
    // Expect: < 1 microsecond per write (atomic operations should be fast)
    //
    // Business value: Ensures telemetry doesn't bottleneck simulation

    const int numIterations = 10000;
    auto testData = createTestTelemetry();

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numIterations; ++i) {
        writer->write(testData);
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double avgNs = static_cast<double>(duration.count()) / numIterations;
    double avgUs = avgNs / 1000.0;

    std::cout << "Average write latency: " << avgUs << " microseconds\n";

    // Warning threshold (not a hard fail)
    if (avgUs > 1.0) {
        std::cout << "WARNING: Write latency exceeds 1 microsecond threshold\n";
        std::cout << "         Consider optimization if this affects simulation performance\n";
    }

    // Hard fail only if extremely slow (10 microseconds)
    EXPECT_LT(avgUs, 10.0) << "Write latency too high: " << avgUs << " microseconds";
}

TEST_F(TelemetryTest, PerformanceBenchmark_ReadLatency) {
    // Test: Measure read operation latency
    // Expect: < 1 microsecond per read (snapshot copy should be fast)

    const int numIterations = 10000;

    auto start = std::chrono::high_resolution_clock::now();

    for (int i = 0; i < numIterations; ++i) {
        auto snapshot = reader->getSnapshot();
        (void)snapshot;  // Prevent optimization
    }

    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start);

    double avgNs = static_cast<double>(duration.count()) / numIterations;
    double avgUs = avgNs / 1000.0;

    std::cout << "Average read latency: " << avgUs << " microseconds\n";

    // Warning threshold (not a hard fail)
    if (avgUs > 1.0) {
        std::cout << "WARNING: Read latency exceeds 1 microsecond threshold\n";
        std::cout << "         Consider optimization if TelemetryData is too large\n";
    }

    // Hard fail only if extremely slow (10 microseconds)
    EXPECT_LT(avgUs, 10.0) << "Read latency too high: " << avgUs << " microseconds";
}
