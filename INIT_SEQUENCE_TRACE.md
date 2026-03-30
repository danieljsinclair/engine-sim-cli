# Initialization Sequence Investigation - Technical Analysis

**Date:** 2026-03-26
**Agent:** TECHNICAL ARCHITECT
**Team:** init-sequence-investigation
**Task:** Trace the EXACT initialization sequence and parameter values

## Executive Summary

The user reported that "OLD code called initialization TWICE. The second call's values worked." This investigation traces the exact initialization sequence and parameter values to identify what changed between the working version and the current version.

## Key Finding: TWO Different Bridge Implementations

There are TWO different `engine_sim_bridge.cpp` files in the codebase:

1. **OLD Bridge**: `/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp`
   - Used by the GUI application
   - Has `LoadScript()` as a separate step
   - Initializes synthesizer BEFORE loading engine (with hardcoded values)

2. **NEW Bridge**: `/engine-sim-bridge/src/engine_sim_bridge.cpp`
   - Used by the CLI application
   - Combined Create() that takes script path immediately
   - Loads engine FIRST, then initializes synthesizer with correct values

## Initialization Sequence Comparison

### OLD Bridge (engine-sim/src/engine_sim_bridge.cpp)

**Call Sequence:**
```
1. EngineSimCreate(&config, &handle)
   ├─ Creates simulator
   ├─ Calls synthesizer.initialize() with:
   │  ├─ inputChannelCount = 2 (HARDCODED at line 240!)
   │  ├─ inputBufferSize = config.inputBufferSize
   │  ├─ audioBufferSize = config.audioBufferSize
   │  ├─ inputSampleRate = config.simulationFrequency
   │  └─ audioSampleRate = config.sampleRate (from config, usually 48000)
   └─ Returns handle

2. EngineSimLoadScript(handle, scriptPath, assetBasePath)
   ├─ Compiles and executes script
   ├─ Creates engine, vehicle, transmission
   └─ Calls simulator->loadSimulation(engine, vehicle, transmission)
      └─ Calls initializeSynthesizer()
         ├─ CHECK: if (m_inputChannels != nullptr) return; // SKIP if already init
         └─ OR: Initialize with HARDCODED 44100!
            ├─ audioBufferSize = 44100 (HARDCODED!)
            ├─ audioSampleRate = 44100 (HARDCODED!)
            ├─ inputBufferSize = 44100 (HARDCODED!)
            ├─ inputChannelCount = engine->getExhaustSystemCount() (CORRECT!)
            └─ inputSampleRate = getSimulationFrequency() (CORRECT!)
```

**Problem:** First call uses hardcoded `inputChannelCount = 2` but correct sample rate from config.
**Result:** Second call is skipped due to the check at simulator.cpp:220, so the WRONG channel count is used!

### NEW Bridge (engine-sim-bridge/src/engine_sim_bridge.cpp)

**Call Sequence:**
```
1. EngineSimCreate(&config, scriptPath, assetBasePath, &handle)
   ├─ Compiles and executes script FIRST
   ├─ Creates engine, vehicle, transmission
   ├─ Creates simulator
   ├─ Calls synthesizer.initialize() with:
   │  ├─ inputChannelCount = engine->getExhaustSystemCount() (CORRECT!)
   │  ├─ inputBufferSize = config.inputBufferSize
   │  ├─ audioBufferSize = config.audioBufferSize
   │  ├─ inputSampleRate = config.simulationFrequency
   │  └─ audioSampleRate = config.sampleRate
   └─ Calls simulator->loadSimulation(engine, vehicle, transmission)
      └─ Calls initializeSynthesizer()
         └─ CHECK: if (m_inputChannels != nullptr) return; // SKIP, already init correctly
```

**Improvement:** Engine is loaded FIRST, so `getExhaustSystemCount()` returns the correct value.

## Critical Parameter Values

### Sample Rates

| Location | Value | Notes |
|----------|-------|-------|
| OLD Bridge setDefaultConfig() | 48000 | Correct default |
| OLD Bridge synthesizer.init | config.sampleRate | Uses config (48000) |
| OLD initializeSynthesizer() | 44100 | HARDCODED - used if first init fails |
| GUI Application | 44100 | HARDCODED at engine_sim_application.cpp:175 |
| NEW Bridge setDefaultConfig() | 48000 | Correct default |
| NEW Bridge synthesizer.init | config.sampleRate | Uses config (48000) |

### Channel Count

| Location | Value | Notes |
|----------|-------|-------|
| OLD Bridge synthesizer.init | 2 | HARDCODED - WRONG for multi-exhaust engines! |
| OLD initializeSynthesizer() | engine->getExhaustSystemCount() | CORRECT, but usually skipped! |
| NEW Bridge synthesizer.init | engine->getExhaustSystemCount() | CORRECT |

## Why the User's Observation Was Correct

When the user said "the OLD code called initialization TWICE. The second call's values worked", they were describing:

1. **First call** (in `EngineSimCreate`): Used `inputChannelCount = 2` (wrong for multi-exhaust engines)
2. **Second call** (in `initializeSynthesizer` via `loadSimulation`): Would use correct `inputChannelCount` from engine

**However**: The second call is normally SKIPPED by the check at simulator.cpp:220:
```cpp
if (m_synthesizer.m_inputChannels != nullptr) {
    return;  // Already initialized, skip
}
```

So the "second call's values" only work if:
1. The first initialization failed somehow, OR
2. The check was bypassed/removed in an older version

## Actual Flow in Working Version

The current NEW bridge (at ba03f7c) initializes CORRECTLY because:
1. It loads the engine BEFORE calling `synthesizer.initialize()`
2. It can call `getExhaustSystemCount()` to get the correct channel count
3. The sample rate comes from the config (48000 by default)

## File Changes Between Working and Current

The main repo HEAD shows changes in `src/EngineConfig.cpp` that revert to the OLD API:

**Current Working Directory (NEW API):**
```cpp
// Single call with script path
EngineSimResult result = api.Create(&config, configPath.c_str(), assetBasePath.c_str(), &handle);
```

**Committed HEAD (OLD API):**
```cpp
// Two separate calls
EngineSimResult result = api.Create(&config, &handle);
result = api.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());
```

This means the working directory is using the NEW bridge API, but the committed code uses the OLD bridge API.

## Conclusion

The "double initialization" issue the user mentioned refers to:

1. **OLD Bridge**: First init with wrong values (hardcoded channel count=2), second init would correct it but is usually skipped
2. **NEW Bridge**: Single init with correct values (dynamic channel count, correct sample rate)

The NEW bridge in the working directory fixes this by loading the engine FIRST, then initializing the synthesizer with the correct parameters.

## Code Evidence

**OLD Bridge (engine-sim/src/engine_sim_bridge.cpp:240):**
```cpp
synthParams.inputChannelCount = 2; // Stereo - HARDCODED!
```

**NEW Bridge (engine-sim-bridge/src/engine_sim_bridge.cpp:327):**
```cpp
synthParams.inputChannelCount = ctx->engine->getExhaustSystemCount();  // Correct channel count!
```

**initializeSynthesizer() (simulator.cpp:226-231):**
```cpp
Synthesizer::Parameters synthParams;
synthParams.audioBufferSize = 44100;  // HARDCODED!
synthParams.audioSampleRate = 44100;  // HARDCODED!
synthParams.inputBufferSize = 44100;  // HARDCODED!
synthParams.inputChannelCount = m_engine->getExhaustSystemCount();  // CORRECT!
synthParams.inputSampleRate = static_cast<float>(getSimulationFrequency());
m_synthesizer.initialize(synthParams);
```

**Skip check (simulator.cpp:220-223):**
```cpp
if (m_synthesizer.m_inputChannels != nullptr) {
    std::cout << "WARNING: Synthesizer already initialized, skipping reinitialization." << std::endl;
    return;
}
```

## Recommendations

1. **Keep the NEW bridge approach** (single Create with script path)
2. **Remove the OLD bridge** to avoid confusion
3. **Ensure sample rate consistency** (all paths should use 48000, not hardcoded 44100)
4. **Document the initialization order** requirement: Engine must be loaded before synthesizer initialization
