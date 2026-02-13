#ifndef ENGINE_SIM_LOADER_H
#define ENGINE_SIM_LOADER_H

#include "engine_sim_bridge.h"
#include <dlfcn.h>
#include <iostream>
#include <cstring>

// Function pointer types for all engine-sim API functions
typedef EngineSimResult (*PFN_EngineSimCreate)(const EngineSimConfig*, EngineSimHandle*);
typedef EngineSimResult (*PFN_EngineSimLoadScript)(EngineSimHandle, const char*, const char*);
typedef EngineSimResult (*PFN_EngineSimStartAudioThread)(EngineSimHandle);
typedef EngineSimResult (*PFN_EngineSimDestroy)(EngineSimHandle);
typedef EngineSimResult (*PFN_EngineSimSetThrottle)(EngineSimHandle, double);
typedef EngineSimResult (*PFN_EngineSimSetSpeedControl)(EngineSimHandle, double);
typedef EngineSimResult (*PFN_EngineSimSetStarterMotor)(EngineSimHandle, int);
typedef EngineSimResult (*PFN_EngineSimSetIgnition)(EngineSimHandle, int);
typedef EngineSimResult (*PFN_EngineSimShiftGear)(EngineSimHandle, int);
typedef EngineSimResult (*PFN_EngineSimSetClutch)(EngineSimHandle, double);
typedef EngineSimResult (*PFN_EngineSimSetDyno)(EngineSimHandle, int);
typedef EngineSimResult (*PFN_EngineSimSetDynoHold)(EngineSimHandle, int, double);
typedef EngineSimResult (*PFN_EngineSimUpdate)(EngineSimHandle, double);
typedef EngineSimResult (*PFN_EngineSimRender)(EngineSimHandle, float*, int32_t, int32_t*);
typedef EngineSimResult (*PFN_EngineSimReadAudioBuffer)(EngineSimHandle, float*, int32_t, int32_t*);
typedef EngineSimResult (*PFN_EngineSimGetStats)(EngineSimHandle, EngineSimStats*);
typedef const char* (*PFN_EngineSimGetLastError)(EngineSimHandle);
typedef const char* (*PFN_EngineSimGetVersion)(void);
typedef EngineSimResult (*PFN_EngineSimValidateConfig)(const EngineSimConfig*);
typedef EngineSimResult (*PFN_EngineSimLoadImpulseResponse)(EngineSimHandle, int, const int16_t*, int, float);

// Global function pointers (loaded at runtime via dlopen)
struct EngineSimAPI {
    void* libHandle;

    PFN_EngineSimCreate Create;
    PFN_EngineSimLoadScript LoadScript;
    PFN_EngineSimStartAudioThread StartAudioThread;
    PFN_EngineSimDestroy Destroy;
    PFN_EngineSimSetThrottle SetThrottle;
    PFN_EngineSimSetSpeedControl SetSpeedControl;
    PFN_EngineSimSetStarterMotor SetStarterMotor;
    PFN_EngineSimSetIgnition SetIgnition;
    PFN_EngineSimShiftGear ShiftGear;
    PFN_EngineSimSetClutch SetClutch;
    PFN_EngineSimSetDyno SetDyno;
    PFN_EngineSimSetDynoHold SetDynoHold;
    PFN_EngineSimUpdate Update;
    PFN_EngineSimRender Render;
    PFN_EngineSimReadAudioBuffer ReadAudioBuffer;
    PFN_EngineSimGetStats GetStats;
    PFN_EngineSimGetLastError GetLastError;
    PFN_EngineSimGetVersion GetVersion;
    PFN_EngineSimValidateConfig ValidateConfig;
    PFN_EngineSimLoadImpulseResponse LoadImpulseResponse;
};

// Helper macro for loading function pointers
#define LOAD_FUNC(api, name) \
    do { \
        api.name = (PFN_EngineSim##name)dlsym(api.libHandle, "EngineSim" #name); \
        if (!api.name) { \
            std::cerr << "ERROR: Failed to load function EngineSim" #name << ": " << dlerror() << "\n"; \
            dlclose(api.libHandle); \
            return false; \
        } \
    } while(0)

// Load the engine-sim library dynamically
inline bool LoadEngineSimLibrary(EngineSimAPI& api, bool useMock) {
    // Clear any existing errors
    dlerror();

    // Choose library based on mode
    const char* libPath = useMock ? "./libenginesim-mock.dylib" : "./libenginesim.dylib";

    // Load library
    api.libHandle = dlopen(libPath, RTLD_NOW);
    if (!api.libHandle) {
        std::cerr << "ERROR: Failed to load " << libPath << ": " << dlerror() << "\n";
        return false;
    }

    std::cout << "[Library loaded: " << libPath << "]\n";

    // Load all function pointers
    LOAD_FUNC(api, Create);
    LOAD_FUNC(api, LoadScript);
    LOAD_FUNC(api, StartAudioThread);
    LOAD_FUNC(api, Destroy);
    LOAD_FUNC(api, SetThrottle);
    LOAD_FUNC(api, SetSpeedControl);
    LOAD_FUNC(api, SetStarterMotor);
    LOAD_FUNC(api, SetIgnition);
    LOAD_FUNC(api, ShiftGear);
    LOAD_FUNC(api, SetClutch);
    LOAD_FUNC(api, SetDyno);
    LOAD_FUNC(api, SetDynoHold);
    LOAD_FUNC(api, Update);
    LOAD_FUNC(api, Render);
    LOAD_FUNC(api, ReadAudioBuffer);
    LOAD_FUNC(api, GetStats);
    LOAD_FUNC(api, GetLastError);
    LOAD_FUNC(api, GetVersion);
    LOAD_FUNC(api, ValidateConfig);
    LOAD_FUNC(api, LoadImpulseResponse);

    return true;
}

// Unload the library
inline void UnloadEngineSimLibrary(EngineSimAPI& api) {
    if (api.libHandle) {
        dlclose(api.libHandle);
        api.libHandle = nullptr;
    }
}

#endif // ENGINE_SIM_LOADER_H
