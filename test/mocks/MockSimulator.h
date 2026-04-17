// MockSimulator.h - Test double for ISimulator
// Returns predictable values for deterministic testing.
// Does NOT depend on the engine-sim library.
// Uses input provider for shutdown signaling (production-like behavior).

#ifndef MOCK_SIMULATOR_H
#define MOCK_SIMULATOR_H

#include "simulator/ISimulator.h"
#include "simulator/engine_sim_bridge.h"
#include "MockInputProvider.h"
#include <cstring>
#include <vector>
#include <atomic>
#include <string>
#include <memory>

class MockSimulator : public ISimulator {
public:
    MockSimulator() = default;
    ~MockSimulator() override = default;

    // ================================================================
    // Input Provider Integration
    // ================================================================

    /**
     * Set input provider for shutdown signaling.
     * Production-like behavior: shutdown comes from input provider,
     * not from direct stop() calls.
     */
    void setInputProvider(test::MockInputProvider* provider) {
        inputProvider_ = provider;
    }

    /**
     * Signal shutdown via input provider.
     * Production-like: sets shouldContinue=false on input provider.
     * No sleeps or CPU spinning.
     */
    void signalShutdown() {
        if (inputProvider_) {
            inputProvider_->setShouldContinue(false);
        }
    }

    // Lifecycle
    bool create(const EngineSimConfig& config) override {
        config_ = config;
        created_ = true;
        return true;
    }

    bool loadScript(const std::string& path, const std::string& assetBase) override {
        scriptPath_ = path;
        assetBasePath_ = assetBase;
        return true;
    }

    bool setLogging(ILogging* logger) override {
        logger_ = logger;
        return true;
    }

    void setTelemetryWriter(telemetry::ITelemetryWriter*) override {}

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
        // Production-like shutdown: use input provider mechanism
        // No sleeps or CPU spinning - clean, immediate shutdown
        signalShutdown();
        audioThreadRunning_ = false;
    }

private:
    EngineSimConfig config_{};
    EngineSimStats stats_{};
    std::string scriptPath_;
    std::string assetBasePath_;
    std::string lastError_;
    ILogging* logger_ = nullptr;
    double throttle_ = 0.0;
    bool ignition_ = false;
    bool starterMotor_ = false;
    bool created_ = false;
    bool audioThreadRunning_ = false;
    double deltaTime_ = 0.0;

    // ================================================================
    // Input Provider
    // ================================================================
    test::MockInputProvider* inputProvider_ = nullptr;
};

#endif // MOCK_SIMULATOR_H
