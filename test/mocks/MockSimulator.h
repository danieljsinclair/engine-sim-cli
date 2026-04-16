// MockSimulator.h - Test double for ISimulator
// Returns predictable values for deterministic testing.
// Does NOT depend on the engine-sim library.

#ifndef MOCK_SIMULATOR_H
#define MOCK_SIMULATOR_H

#include "simulation/ISimulator.h"
#include "engine_sim_bridge.h"
#include <cstring>
#include <vector>
#include <atomic>
#include <string>

class MockSimulator : public ISimulator {
public:
    MockSimulator() = default;
    ~MockSimulator() override = default;

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

    bool destroy() override {
        created_ = false;
        return true;
    }

    std::string getLastError() const override {
        return lastError_;
    }

    // Simulation
    bool update(double deltaTime) override {
        deltaTime_ = deltaTime;
        stats_.currentRPM = 800.0 + throttle_ * 5200.0;
        stats_.currentLoad = throttle_;
        return true;
    }

    EngineSimStats getStats() const override {
        return stats_;
    }

    // Control inputs
    bool setThrottle(double position) override {
        throttle_ = position;
        return true;
    }

    bool setIgnition(bool on) override {
        ignition_ = on;
        return true;
    }

    bool setStarterMotor(bool on) override {
        starterMotor_ = on;
        return true;
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

    bool startAudioThread() override {
        audioThreadRunning_ = true;
        return true;
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
};

#endif // MOCK_SIMULATOR_H
