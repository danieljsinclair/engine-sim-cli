// BridgeSimulator.h - Production implementation of ISimulator
// Wraps the C-style EngineSimAPI behind the ISimulator interface.
// DIP: Consumers depend on ISimulator, not on raw bridge types.

#ifndef BRIDGE_SIMULATOR_H
#define BRIDGE_SIMULATOR_H

#include "simulation/ISimulator.h"
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"
#include <string>

class BridgeSimulator : public ISimulator {
public:
    BridgeSimulator();
    ~BridgeSimulator() override;

    // ISimulator lifecycle
    bool create(const EngineSimConfig& config) override;
    bool loadScript(const std::string& path, const std::string& assetBase) override;
    bool setLogging(ILogging* logger) override;
    bool destroy() override;
    std::string getLastError() const override;

    // ISimulator simulation
    bool update(double deltaTime) override;
    EngineSimStats getStats() const override;

    // ISimulator control inputs
    bool setThrottle(double position) override;
    bool setIgnition(bool on) override;
    bool setStarterMotor(bool on) override;

    // ISimulator audio production
    bool renderOnDemand(float* buffer, int32_t frames, int32_t* written) override;
    bool readAudioBuffer(float* buffer, int32_t frames, int32_t* read) override;
    bool startAudioThread() override;

private:
    EngineSimAPI api_;
    EngineSimHandle handle_ = nullptr;
    ILogging* pendingLogger_ = nullptr;
    bool created_ = false;
};

#endif // BRIDGE_SIMULATOR_H
