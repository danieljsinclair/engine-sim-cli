// test_telemetry_isp.cpp - TDD RED-phase tests for ISP-compliant telemetry
//
// Purpose: Verify per-component telemetry write/read interfaces.
// Components push only their own data (ISP), not the whole TelemetryData god struct.
//
// ISP Components:
//   EngineStateTelemetry  -- currentRPM, currentLoad, exhaustFlow, manifoldPressure, activeChannels
//   FramePerformanceTelemetry -- processingTimeMs
//   AudioDiagnosticsTelemetry -- underrunCount, bufferHealthPct
//   VehicleInputsTelemetry    -- throttlePosition, ignitionOn, starterMotorEngaged
//   SimulatorMetricsTelemetry -- timestamp
//
// TDD: These tests assert correct behaviour of the ISP interfaces.
// In RED phase, they compile against forward declarations but fail at link/run
// because the implementations return default/zero values.

#include <gtest/gtest.h>
#include <thread>
#include <vector>
#include <atomic>

#include "telemetry/ITelemetryProvider.h"

using namespace telemetry;

// ============================================================================
// Test Fixture -- ISP component telemetry
// ============================================================================

class TelemetryISPTest : public ::testing::Test {
protected:
    void SetUp() override {
        telemetry = std::make_unique<InMemoryTelemetry>();
    }

    void TearDown() override {
        telemetry.reset();
    }

    std::unique_ptr<InMemoryTelemetry> telemetry;
};

// ============================================================================
// EngineStateTelemetry -- write and read component in isolation
// ============================================================================

TEST_F(TelemetryISPTest, EngineState_WriteAndRead) {
    EngineStateTelemetry state;
    state.currentRPM = 5200.0;
    state.currentLoad = 0.75;
    state.exhaustFlow = 0.035;
    state.manifoldPressure = 101325.0;
    state.activeChannels = 4;

    telemetry->writeEngineState(state);

    auto snapshot = telemetry->getEngineState();
    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 5200.0);
    EXPECT_DOUBLE_EQ(snapshot.currentLoad, 0.75);
    EXPECT_DOUBLE_EQ(snapshot.exhaustFlow, 0.035);
    EXPECT_DOUBLE_EQ(snapshot.manifoldPressure, 101325.0);
    EXPECT_EQ(snapshot.activeChannels, 4);
}

TEST_F(TelemetryISPTest, EngineState_OverwritesOnMultipleWrites) {
    EngineStateTelemetry first;
    first.currentRPM = 1000.0;
    first.currentLoad = 0.1;
    first.exhaustFlow = 0.001;
    first.manifoldPressure = 50000.0;
    first.activeChannels = 1;
    telemetry->writeEngineState(first);

    EngineStateTelemetry second;
    second.currentRPM = 8000.0;
    second.currentLoad = 0.9;
    second.exhaustFlow = 0.050;
    second.manifoldPressure = 200000.0;
    second.activeChannels = 8;
    telemetry->writeEngineState(second);

    auto snapshot = telemetry->getEngineState();
    EXPECT_DOUBLE_EQ(snapshot.currentRPM, 8000.0);
    EXPECT_DOUBLE_EQ(snapshot.currentLoad, 0.9);
    EXPECT_DOUBLE_EQ(snapshot.exhaustFlow, 0.050);
    EXPECT_DOUBLE_EQ(snapshot.manifoldPressure, 200000.0);
    EXPECT_EQ(snapshot.activeChannels, 8);
}

// ============================================================================
// FramePerformanceTelemetry -- write and read component in isolation
// ============================================================================

TEST_F(TelemetryISPTest, FramePerformance_WriteAndRead) {
    FramePerformanceTelemetry perf;
    perf.processingTimeMs = 2.5;

    telemetry->writeFramePerformance(perf);

    auto snapshot = telemetry->getFramePerformance();
    EXPECT_DOUBLE_EQ(snapshot.processingTimeMs, 2.5);
}

TEST_F(TelemetryISPTest, FramePerformance_OverwritesOnMultipleWrites) {
    FramePerformanceTelemetry first;
    first.processingTimeMs = 0.5;
    telemetry->writeFramePerformance(first);

    FramePerformanceTelemetry second;
    second.processingTimeMs = 8.0;
    telemetry->writeFramePerformance(second);

    auto snapshot = telemetry->getFramePerformance();
    EXPECT_DOUBLE_EQ(snapshot.processingTimeMs, 8.0);
}

// ============================================================================
// AudioDiagnosticsTelemetry -- write and read component in isolation
// ============================================================================

TEST_F(TelemetryISPTest, AudioDiagnostics_WriteAndRead) {
    AudioDiagnosticsTelemetry diag;
    diag.underrunCount = 3;
    diag.bufferHealthPct = 75.5;

    telemetry->writeAudioDiagnostics(diag);

    auto snapshot = telemetry->getAudioDiagnostics();
    EXPECT_EQ(snapshot.underrunCount, 3);
    EXPECT_DOUBLE_EQ(snapshot.bufferHealthPct, 75.5);
}

TEST_F(TelemetryISPTest, AudioDiagnostics_OverwritesOnMultipleWrites) {
    AudioDiagnosticsTelemetry first;
    first.underrunCount = 0;
    first.bufferHealthPct = 100.0;
    telemetry->writeAudioDiagnostics(first);

    AudioDiagnosticsTelemetry second;
    second.underrunCount = 42;
    second.bufferHealthPct = 10.0;
    telemetry->writeAudioDiagnostics(second);

    auto snapshot = telemetry->getAudioDiagnostics();
    EXPECT_EQ(snapshot.underrunCount, 42);
    EXPECT_DOUBLE_EQ(snapshot.bufferHealthPct, 10.0);
}

// ============================================================================
// VehicleInputsTelemetry -- write and read component in isolation
// ============================================================================

TEST_F(TelemetryISPTest, VehicleInputs_WriteAndRead) {
    VehicleInputsTelemetry inputs;
    inputs.throttlePosition = 0.65;
    inputs.ignitionOn = true;
    inputs.starterMotorEngaged = false;

    telemetry->writeVehicleInputs(inputs);

    auto snapshot = telemetry->getVehicleInputs();
    EXPECT_DOUBLE_EQ(snapshot.throttlePosition, 0.65);
    EXPECT_TRUE(snapshot.ignitionOn);
    EXPECT_FALSE(snapshot.starterMotorEngaged);
}

TEST_F(TelemetryISPTest, VehicleInputs_OverwritesOnMultipleWrites) {
    VehicleInputsTelemetry first;
    first.throttlePosition = 0.0;
    first.ignitionOn = false;
    first.starterMotorEngaged = false;
    telemetry->writeVehicleInputs(first);

    VehicleInputsTelemetry second;
    second.throttlePosition = 1.0;
    second.ignitionOn = true;
    second.starterMotorEngaged = true;
    telemetry->writeVehicleInputs(second);

    auto snapshot = telemetry->getVehicleInputs();
    EXPECT_DOUBLE_EQ(snapshot.throttlePosition, 1.0);
    EXPECT_TRUE(snapshot.ignitionOn);
    EXPECT_TRUE(snapshot.starterMotorEngaged);
}

// ============================================================================
// SimulatorMetricsTelemetry -- write and read component in isolation
// ============================================================================

TEST_F(TelemetryISPTest, SimulatorMetrics_WriteAndRead) {
    SimulatorMetricsTelemetry metrics;
    metrics.timestamp = 5.678;

    telemetry->writeSimulatorMetrics(metrics);

    auto snapshot = telemetry->getSimulatorMetrics();
    EXPECT_DOUBLE_EQ(snapshot.timestamp, 5.678);
}

TEST_F(TelemetryISPTest, SimulatorMetrics_OverwritesOnMultipleWrites) {
    SimulatorMetricsTelemetry first;
    first.timestamp = 0.0;
    telemetry->writeSimulatorMetrics(first);

    SimulatorMetricsTelemetry second;
    second.timestamp = 99.999;
    telemetry->writeSimulatorMetrics(second);

    auto snapshot = telemetry->getSimulatorMetrics();
    EXPECT_DOUBLE_EQ(snapshot.timestamp, 99.999);
}

// ============================================================================
// ISP Isolation -- writing one component does not affect others
// ============================================================================

TEST_F(TelemetryISPTest, ComponentIsolation_WritingEngineStateDoesNotAffectAudioDiagnostics) {
    // Write engine state
    EngineStateTelemetry engine;
    engine.currentRPM = 5000.0;
    engine.currentLoad = 0.8;
    engine.exhaustFlow = 0.04;
    engine.manifoldPressure = 150000.0;
    engine.activeChannels = 2;
    telemetry->writeEngineState(engine);

    // Write audio diagnostics
    AudioDiagnosticsTelemetry audio;
    audio.underrunCount = 7;
    audio.bufferHealthPct = 88.0;
    telemetry->writeAudioDiagnostics(audio);

    // Re-write engine state with different values
    EngineStateTelemetry engine2;
    engine2.currentRPM = 6000.0;
    engine2.currentLoad = 0.9;
    engine2.exhaustFlow = 0.05;
    engine2.manifoldPressure = 160000.0;
    engine2.activeChannels = 3;
    telemetry->writeEngineState(engine2);

    // Audio diagnostics should be unchanged
    auto audioSnapshot = telemetry->getAudioDiagnostics();
    EXPECT_EQ(audioSnapshot.underrunCount, 7);
    EXPECT_DOUBLE_EQ(audioSnapshot.bufferHealthPct, 88.0);
}

TEST_F(TelemetryISPTest, ComponentIsolation_WritingVehicleInputsDoesNotAffectEngineState) {
    // Write engine state first
    EngineStateTelemetry engine;
    engine.currentRPM = 3000.0;
    engine.currentLoad = 0.5;
    engine.exhaustFlow = 0.02;
    engine.manifoldPressure = 100000.0;
    engine.activeChannels = 2;
    telemetry->writeEngineState(engine);

    // Write vehicle inputs
    VehicleInputsTelemetry inputs;
    inputs.throttlePosition = 0.3;
    inputs.ignitionOn = true;
    inputs.starterMotorEngaged = true;
    telemetry->writeVehicleInputs(inputs);

    // Engine state should be unchanged
    auto engineSnapshot = telemetry->getEngineState();
    EXPECT_DOUBLE_EQ(engineSnapshot.currentRPM, 3000.0);
    EXPECT_DOUBLE_EQ(engineSnapshot.currentLoad, 0.5);
}

// ============================================================================
// reset() clears all ISP components
// ============================================================================

TEST_F(TelemetryISPTest, ResetClearsAllComponents) {
    // Write to every component
    EngineStateTelemetry engine;
    engine.currentRPM = 5000.0;
    engine.currentLoad = 0.8;
    engine.exhaustFlow = 0.04;
    engine.manifoldPressure = 150000.0;
    engine.activeChannels = 4;
    telemetry->writeEngineState(engine);

    FramePerformanceTelemetry perf;
    perf.processingTimeMs = 3.0;
    telemetry->writeFramePerformance(perf);

    AudioDiagnosticsTelemetry diag;
    diag.underrunCount = 10;
    diag.bufferHealthPct = 50.0;
    telemetry->writeAudioDiagnostics(diag);

    VehicleInputsTelemetry inputs;
    inputs.throttlePosition = 0.7;
    inputs.ignitionOn = true;
    inputs.starterMotorEngaged = true;
    telemetry->writeVehicleInputs(inputs);

    SimulatorMetricsTelemetry metrics;
    metrics.timestamp = 10.0;
    telemetry->writeSimulatorMetrics(metrics);

    // Reset
    telemetry->reset();

    // All components should be zeroed
    auto engineSnap = telemetry->getEngineState();
    EXPECT_DOUBLE_EQ(engineSnap.currentRPM, 0.0);
    EXPECT_DOUBLE_EQ(engineSnap.currentLoad, 0.0);

    auto perfSnap = telemetry->getFramePerformance();
    EXPECT_DOUBLE_EQ(perfSnap.processingTimeMs, 0.0);

    auto diagSnap = telemetry->getAudioDiagnostics();
    EXPECT_EQ(diagSnap.underrunCount, 0);
    EXPECT_DOUBLE_EQ(diagSnap.bufferHealthPct, 0.0);

    auto inputsSnap = telemetry->getVehicleInputs();
    EXPECT_DOUBLE_EQ(inputsSnap.throttlePosition, 0.0);
    EXPECT_FALSE(inputsSnap.ignitionOn);
    EXPECT_FALSE(inputsSnap.starterMotorEngaged);

    auto metricsSnap = telemetry->getSimulatorMetrics();
    EXPECT_DOUBLE_EQ(metricsSnap.timestamp, 0.0);
}

// ============================================================================
// Thread safety -- concurrent per-component writes
// ============================================================================

TEST_F(TelemetryISPTest, ConcurrentComponentWrites_NoCorruption) {
    const int iterations = 1000;
    std::atomic<bool> done{false};

    // Thread 1: writes engine state continuously
    std::thread engineWriter([this, iterations]() {
        for (int i = 0; i < iterations; ++i) {
            EngineStateTelemetry state;
            state.currentRPM = 1000.0 + i;
            state.currentLoad = 0.5;
            state.exhaustFlow = 0.01;
            state.manifoldPressure = 101325.0;
            state.activeChannels = 2;
            telemetry->writeEngineState(state);
        }
    });

    // Thread 2: writes audio diagnostics continuously
    std::thread audioWriter([this, iterations, &done]() {
        for (int i = 0; i < iterations; ++i) {
            AudioDiagnosticsTelemetry diag;
            diag.underrunCount = i;
            diag.bufferHealthPct = 50.0;
            telemetry->writeAudioDiagnostics(diag);
        }
        done = true;
    });

    engineWriter.join();
    audioWriter.join();

    EXPECT_TRUE(done);

    // Both components should have valid (non-corrupt) final values
    auto engine = telemetry->getEngineState();
    EXPECT_GE(engine.currentRPM, 1000.0);
    EXPECT_LE(engine.currentRPM, 1000.0 + iterations);

    auto diag = telemetry->getAudioDiagnostics();
    EXPECT_GE(diag.underrunCount, 0);
    EXPECT_LE(diag.underrunCount, iterations);
}
