#ifndef ENGINE_SIM_LOADER_H
#define ENGINE_SIM_LOADER_H

// Direct-linked bridge API — no dlopen, no function pointer table.
// The bridge dylib is loaded by the OS at startup via @rpath linking.
// Each method forwards directly to a bridge C function, giving compile-time
// signature checking: if the bridge header changes, this file fails to compile.
//
// Call sites (api.Create(...) etc.) are unchanged from the old dlopen version.
// LoadEngineSimLibrary / UnloadEngineSimLibrary are kept as no-ops so existing
// call sites in CLIMain.cpp continue to compile without modification.

#include "engine_sim_bridge.h"
#include "ILogging.h"

struct EngineSimAPI {
    EngineSimResult Create(const EngineSimConfig* cfg, EngineSimHandle* h) const {
        return EngineSimCreate(cfg, h);
    }
    EngineSimResult LoadScript(EngineSimHandle h, const char* cfg, const char* asset) const {
        return EngineSimLoadScript(h, cfg, asset);
    }
    EngineSimResult SetLogging(EngineSimHandle h, ILogging* log) const {
        return EngineSimSetLogging(h, log);
    }
    EngineSimResult StartAudioThread(EngineSimHandle h) const {
        return EngineSimStartAudioThread(h);
    }
    EngineSimResult Destroy(EngineSimHandle h) const {
        return EngineSimDestroy(h);
    }
    EngineSimResult SetThrottle(EngineSimHandle h, double v) const {
        return EngineSimSetThrottle(h, v);
    }
    EngineSimResult SetSpeedControl(EngineSimHandle h, double v) const {
        return EngineSimSetSpeedControl(h, v);
    }
    EngineSimResult SetStarterMotor(EngineSimHandle h, int on) const {
        return EngineSimSetStarterMotor(h, on);
    }
    EngineSimResult SetIgnition(EngineSimHandle h, int on) const {
        return EngineSimSetIgnition(h, on);
    }
    EngineSimResult ShiftGear(EngineSimHandle h, int gear) const {
        return EngineSimShiftGear(h, gear);
    }
    EngineSimResult SetClutch(EngineSimHandle h, double v) const {
        return EngineSimSetClutch(h, v);
    }
    EngineSimResult SetDyno(EngineSimHandle h, int on) const {
        return EngineSimSetDyno(h, on);
    }
    EngineSimResult SetDynoHold(EngineSimHandle h, int on, double torque) const {
        return EngineSimSetDynoHold(h, on, torque);
    }
    EngineSimResult Update(EngineSimHandle h, double dt) const {
        return EngineSimUpdate(h, dt);
    }
    EngineSimResult Render(EngineSimHandle h, float* buf, int32_t frames, int32_t* written) const {
        return EngineSimRender(h, buf, frames, written);
    }
    EngineSimResult ReadAudioBuffer(EngineSimHandle h, float* buf, int32_t frames, int32_t* read) const {
        return EngineSimReadAudioBuffer(h, buf, frames, read);
    }
    EngineSimResult GetStats(EngineSimHandle h, EngineSimStats* s) const {
        return EngineSimGetStats(h, s);
    }
    const char* GetLastError(EngineSimHandle h) const {
        return EngineSimGetLastError(h);
    }
    const char* GetVersion() const {
        return EngineSimGetVersion();
    }
    EngineSimResult ValidateConfig(const EngineSimConfig* cfg) const {
        return EngineSimValidateConfig(cfg);
    }
    EngineSimResult LoadImpulseResponse(EngineSimHandle h, int idx, const int16_t* ir, int len, float vol) const {
        return EngineSimLoadImpulseResponse(h, idx, ir, len, vol);
    }
    EngineSimResult RenderOnDemand(EngineSimHandle h, float* buf, int32_t frames, int32_t* written) const {
        return EngineSimRenderOnDemand(h, buf, frames, written);
    }
};

#endif // ENGINE_SIM_LOADER_H
