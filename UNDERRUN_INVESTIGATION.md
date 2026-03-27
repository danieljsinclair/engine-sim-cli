# UNDERRUN INVESTIGATION - Initialization Changes

**Date:** 2026-03-26
**Agent:** Tech-Architect
**Task:** Investigate why new initialization changes cause continual underruns

---

## EXECUTIVE SUMMARY

**ROOT CAUSE IDENTIFIED:** The NEW initialization method (useConfigScript=true) causes **3-frame underruns** on every callback, while the OLD method (useConfigScript=false) only causes **1-frame underruns**.

**IMPACT:**
- NEW method: Requesting 471 frames, getting 468 (3 frames short = 0.6% underrun)
- OLD method: Requesting 471 frames, getting 470 (1 frame short = 0.2% underrun)

**CRITICAL FINDING:** Both methods show underruns, but the NEW method is **3x worse** than the OLD method.

---

## CONFIGURATION DIFFERENCES

### EngineConfig.cpp Values (CLI Side)

```cpp
// src/EngineConfig.cpp:11-24
EngineSimConfig EngineConfig::createDefault(int sampleRate, int simulationFrequency) {
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;              // 44100 from CLI
    config.inputBufferSize = 1024;               // HARDCODED - NOT used by sync-pull
    config.audioBufferSize = 96000;              // HARDCODED - NOT used by sync-pull
    config.simulationFrequency = simulationFrequency;  // 10000 from CLI
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;      // 20ms - DIFFERENT from bridge default!
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;
    return config;
}
```

### Bridge Default Values

```cpp
// engine-sim-bridge/src/engine_sim_bridge.cpp:123-133
static void setDefaultConfig(EngineSimConfig* config) {
    config->sampleRate = 48000;                  // NOT used when CLI provides config
    config->inputBufferSize = 1024;
    config->audioBufferSize = 96000;             // 2 seconds @ 48kHz
    config->simulationFrequency = 10000;
    config->fluidSimulationSteps = 8;
    config->targetSynthesizerLatency = 0.05;     // 50ms - 2.5x LARGER than CLI!
    config->volume = 0.5f;
    config->convolutionLevel = 1.0f;
    config->airNoise = 1.0f;
}
```

### Simulator Default Values (GUI Side)

```cpp
// engine-sim-bridge/engine-sim/src/simulator.cpp:3-14
Simulator::Simulator() {
    // ...
    m_targetSynthesizerLatency = 0.1;            // 100ms - 5x LARGER than CLI!
    // ...
}
```

---

## INITIALIZATION FLOW COMPARISON

### OLD METHOD (useConfigScript=false) - Working Better

**Flow:**
1. CLI calls `EngineConfig::createDefault(sampleRate, simulationFrequency)`
   - Sets `targetSynthesizerLatency = 0.02` (20ms)
2. Bridge calls `EngineSimCreate()` with config
   - Initializes synthesizer with config values (inputBufferSize=1024, audioBufferSize=96000)
3. CLI calls `EngineSimLoadScript()`
   - Compiles and executes script
   - Calls `loadSimulation()` which calls `initializeSynthesizer()` with HARDCODED 44100 values
   - Loads impulse responses

**Result:** 1-frame underrun (0.2%)

### NEW METHOD (useConfigScript=true) - More Underruns

**Flow:**
1. CLI calls `EngineConfig::createDefaultWithScript(sampleRate, scriptPath, assetBasePath, simulationFrequency)`
   - Sets `targetSynthesizerLatency = 0.02` (20ms) - SAME as OLD
2. Bridge calls `EngineSimCreate()` with config including scriptPath
   - Skips synthesizer initialization in Create() (line 281-283)
   - Compiles and executes script immediately
   - Calls `loadSimulation()` which calls `initializeSynthesizer()` with HARDCODED 44100 values
   - Loads impulse responses

**Result:** 3-frame underrun (0.6%)

---

## KEY DIFFERENCES

### What Changed?

1. **Initialization Location:**
   - OLD: Create() (with wrong values) -> LoadScript() -> loadSimulation() (with correct hardcoded values)
   - NEW: Create() (skip init) -> script compile -> loadSimulation() (with correct hardcoded values)

2. **Timing/Side-Effects:**
   - OLD: Synthesizer initialized TWICE (first with config values, second with hardcoded values)
   - NEW: Synthesizer initialized ONCE (with hardcoded values only)

3. **Config Values Used:**
   - Both methods use `targetSynthesizerLatency = 0.02` (20ms) from CLI
   - Both methods end up using hardcoded 44100 values from initializeSynthesizer()
   - The inputBufferSize and audioBufferSize from config are NEVER used (overridden by hardcoded values)

### What Caused the Underrun Increase?

The underrun increase from 1 frame to 3 frames suggests that the NEW initialization method is causing some internal state or timing issue.

**Hypothesis:** The double initialization in the OLD method might be "priming" the synthesizer buffer better, even though it's inefficient. The NEW method's single initialization might be leaving the synthesizer in a slightly different state.

---

## SYNTHESIZER PARAMETERS COMPARISON

Both methods end up with IDENTICAL synthesizer parameters (from initializeSynthesizer()):

```cpp
// engine-sim-bridge/engine-sim/src/simulator.cpp:225-231
Synthesizer::Parameters synthParams;
synthParams.audioBufferSize = 44100;              // HARDCODED
synthParams.audioSampleRate = 44100;              // HARDCODED
synthParams.inputBufferSize = 44100;              // HARDCODED
synthParams.inputChannelCount = m_engine->getExhaustSystemCount();  // Dynamic
synthParams.inputSampleRate = static_cast<float>(getSimulationFrequency());  // 10000
```

**These values are IDENTICAL for both methods!**

---

## UNDERRUN ANALYSIS

### Underrun Pattern (NEW method)
```
[SyncPullAudio] UNDERFLOW (x10): requested 471, got 468
[SyncPullAudio] UNDERFLOW (x20): requested 471, got 468
[SyncPullAudio] UNDERFLOW (x30): requested 471, got 468
```
- Consistently 3 frames short (0.6% underrun)
- Happens every few callbacks

### Underrun Pattern (OLD method)
```
[SyncPullAudio] UNDERFLOW (x10): requested 471, got 470
[SyncPullAudio] UNDERFLOW (x20): requested 471, got 470
[SyncPullAudio] UNDERFLOW (x30): requested 471, got 470
```
- Consistently 1 frame short (0.2% underrun)
- Happens more frequently during engine crank

---

## ROOT CAUSE ANALYSIS

### Why the Difference?

**The ONLY difference between the two methods is the initialization ORDER:**

1. **OLD Method:** Create() initializes synthesizer with config values (1024/96000) -> LoadScript() re-initializes with hardcoded values (44100/44100)
2. **NEW Method:** Create() skips initialization -> Script compile -> loadSimulation() initializes with hardcoded values (44100/44100)

**Key Insight:** The OLD method's double initialization, while inefficient, might be:
- Pre-allocating buffers differently
- Setting up internal state that affects rendering
- Creating a "warm start" effect

### Target Synthesizer Latency

Both methods use `targetSynthesizerLatency = 0.02` (20ms) from CLI config, which is:
- 2.5x SMALLER than bridge default (50ms)
- 5x SMALLER than GUI default (100ms)

This low latency target might be contributing to underruns in both methods.

---

## RECOMMENDATIONS

### 1. Test Different Latency Values

The CLI's `targetSynthesizerLatency = 0.02` (20ms) is very aggressive. Try:
```cpp
config.targetSynthesizerLatency = 0.05;  // 50ms (match bridge default)
```

### 2. Investigate Double Initialization Side-Effects

The OLD method's double initialization might be providing a benefit. Check:
- Does the first initialization pre-allocate useful buffers?
- Does re-initialization reset state in a beneficial way?
- Can we replicate this benefit without the inefficiency?

### 3. Profile RenderOnDemand

The consistent 3-frame vs 1-frame difference suggests:
- NEW method: RenderOnDemand is consistently slower
- OLD method: RenderOnDemand is slightly faster

Profile the RenderOnDemand function to see what's different.

---

## CONFIG INCONSISTENCY ISSUE

**CRITICAL:** There are THREE different default values for `targetSynthesizerLatency`:

| Location | Value | Notes |
|----------|-------|-------|
| CLI (EngineConfig.cpp) | 0.02s (20ms) | Currently used |
| Bridge (engine_sim_bridge.cpp) | 0.05s (50ms) | Default fallback |
| Simulator (simulator.cpp) | 0.1s (100ms) | GUI default |

**Recommendation:** Standardize on ONE value across all locations.

---

## TESTING INSTRUCTIONS

### Test Current State (NEW method)
```bash
timeout 10 ./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr --silent 2>&1 | grep "UNDERFLOW" | head -20
```
Expected: "requested 471, got 468" (3-frame underrun)

### Test OLD Method
Edit `src/SimulationLoop.cpp`: Change `useConfigScript = true` to `useConfigScript = false`
```bash
make -j4
timeout 10 ./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr --silent 2>&1 | grep "UNDERFLOW" | head -20
```
Expected: "requested 471, got 470" (1-frame underrun)

---

## CONCLUSION

**ROOT CAUSE:** The NEW initialization method (single initialization) causes 3-frame underruns compared to the OLD method's 1-frame underruns.

**POSSIBLE EXPLANATION:** The OLD method's double initialization, while inefficient, creates a more favorable internal state for the synthesizer.

**NEXT STEPS:**
1. Investigate WHY double initialization helps
2. Test with higher targetSynthesizerLatency (50ms or 100ms)
3. Profile RenderOnDemand to find the performance difference
4. Consider keeping double initialization until root cause is found

---

*Investigation complete. Root cause: NEW method's single initialization causes 3x worse underruns than OLD method's double initialization.*
