// IOSSimulator.h - iOS-specific simulator wrapper
// Uses shared SimulationRunner from bridge for audio pipeline

#ifndef IOS_SIMULATOR_H
#define IOS_SIMULATOR_H

#include "simulator/ISimulator.h"
#include "telemetry/ITelemetryProvider.h"
#include "simulator/EngineSimTypes.h"
#include <memory>

class SimulationRunner;

class IOSSimulator {
public:
    IOSSimulator();
    ~IOSSimulator();

    // Non-copyable
    IOSSimulator(const IOSSimulator&) = delete;
    IOSSimulator& operator=(const IOSSimulator&) = delete;

    // Lifecycle
    bool loadScript(const std::string& scriptPath, const std::string& assetBasePath);
    bool start();
    void stop();
    void update(double deltaTime);

    // Controls
    void setThrottle(double position);
    void setIgnition(bool on);
    void setStarterMotor(bool on);

    // Telemetry access (read from telemetry, no local state)
    double getCurrentRPM() const;
    double getCurrentLoad() const;
    double getExhaustFlow() const;
    double getManifoldPressure() const;
    double getThrottlePosition() const;
    bool getIgnition() const;
    bool getStarterMotor() const;
    bool isRunning() const;

private:
    std::unique_ptr<ISimulator> simulator_;
    std::unique_ptr<telemetry::InMemoryTelemetry> telemetry_;
    std::unique_ptr<SimulationRunner> runner_;
    bool running_;

    // Track current control state for telemetry
    double currentThrottle_ = 0.0;
    bool currentIgnition_ = false;
    bool currentStarter_ = false;
};

#endif // IOS_SIMULATOR_H
