// MockSimulator.h - Test double for ISimulator
// Returns predictable values for deterministic testing.
// Does NOT depend on the engine-sim library.

#ifndef MOCK_SIMULATOR_H
#define MOCK_SIMULATOR_H

#include "simulator/ISimulator.h"
#include "simulator/ICombustionEngine.h"
#include "simulator/EngineSimTypes.h"
#include "simulation/EnginePhase.h"
#include <cstring>
#include <vector>
#include <atomic>
#include <string>
#include <memory>

class MockSimulator : public ICombustionEngine {
public:
    MockSimulator() = default;
    ~MockSimulator() override = default;

    // Lifecycle
    bool create(const ISimulatorConfig& config, ILogging* logger, telemetry::ITelemetryWriter* telemetryWriter) override {
        config_ = config;
        logger_ = logger;
        telemetryWriter_ = telemetryWriter;
        created_ = true;
        return true;
    }

    const char* getName() const override {
        return "MockSimulator";
    }

    void destroy() override {
        created_ = false;
    }

    std::string getLastError() const override {
        return lastError_;
    }

    // Simulation
    void update(double deltaTime) override {
        deltaTime_ = deltaTime;
        stats_.currentRPM = 800.0 + throttle_ * 5200.0;
        stats_.currentLoad = throttle_;
    }

    EngineSimStats getStats() const override {
        return stats_;
    }

    // Control inputs
    void setThrottle(double position) override {
        throttle_ = position;
    }

    void setIgnition(bool on) override {
        ignition_ = on;
    }

    void setStarterMotor(bool on) override {
        starterMotor_ = on;
    }

    // Audio frame production -- produces silence
    bool renderOnDemand(float* buffer, int32_t frames, int32_t* written) override {
        if (buffer && frames > 0) {
            std::memset(buffer, 0, frames * 2 * sizeof(float));
            if (written) *written = frames;
        }
        return true;
    }

    bool readAudioBuffer(float* buffer, int32_t frames, int32_t* read) override {
        if (buffer && frames > 0) {
            std::memset(buffer, 0, frames * 2 * sizeof(float));
            if (read) *read = frames;
        }
        return true;
    }

    bool start() override {
        audioThreadRunning_ = true;
        return true;
    }

    void stop() override {
        audioThreadRunning_ = false;
    }

    int getSimulationFrequency() const override { return config_.simulationFrequency; }

    EnginePhase getEnginePhase() const override { return enginePhase_; }
    void applyTransition(const TransitionDecision& decision) override {
        enginePhase_ = decision.targetPhase;
        starterMotor_ = decision.starterMotor;
    }

private:
    ISimulatorConfig config_{};
    EngineSimStats stats_{};
    std::string lastError_;
    ILogging* logger_ = nullptr;
    telemetry::ITelemetryWriter* telemetryWriter_ = nullptr;
    double throttle_ = 0.0;
    bool ignition_ = false;
    bool starterMotor_ = false;
    bool created_ = false;
    bool audioThreadRunning_ = false;
    double deltaTime_ = 0.0;
    EnginePhase enginePhase_ = EnginePhase::Stopped;
};

#endif // MOCK_SIMULATOR_H
