// ISimulator.h - Pure virtual interface for engine simulation
// Abstracts the C-style EngineSimAPI behind a clean C++ interface.
// Phase E: Establishes the "Holy Trinity":
//   ISimulator (produces frames) -> IAudioStrategy (orchestrates) -> IAudioHardwareProvider (consumes)

#ifndef ISIMULATOR_H
#define ISIMULATOR_H

#include <string>
#include <cstdint>
#include "engine_sim_bridge.h"

class ILogging;

class ISimulator {
public:
    virtual ~ISimulator() = default;

    // Lifecycle
    virtual bool create(const EngineSimConfig& config) = 0;
    virtual bool loadScript(const std::string& path, const std::string& assetBase) = 0;
    virtual bool setLogging(ILogging* logger) = 0;
    virtual bool destroy() = 0;
    virtual std::string getLastError() const = 0;

    // Simulation
    virtual bool update(double deltaTime) = 0;
    virtual EngineSimStats getStats() const = 0;

    // Control inputs
    virtual bool setThrottle(double position) = 0;
    virtual bool setIgnition(bool on) = 0;
    virtual bool setStarterMotor(bool on) = 0;

    // Audio frame production
    virtual bool renderOnDemand(float* buffer, int32_t frames, int32_t* written) = 0;
    virtual bool readAudioBuffer(float* buffer, int32_t frames, int32_t* read) = 0;
    virtual bool startAudioThread() = 0;

    // Version (static -- not instance-dependent)
    static const char* getVersion();
};

#endif // ISIMULATOR_H
