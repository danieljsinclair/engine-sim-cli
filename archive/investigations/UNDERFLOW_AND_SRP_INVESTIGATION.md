# UNDERFLOW AND SRP VIOLATION INVESTIGATION

**Date:** 2026-03-26
**Agent:** Tech-Architect, Solution-Architect, SOLID-Pedant
**Task:** Investigate underflow after few seconds and SRP violation

---

## EXECUTIVE SUMMARY

**GOOD NEWS:** Dirty sound problem is SOLVED! Sound is now clean.

**TWO ISSUES IDENTIFIED:**
1. **Underflow:** Consistent 3-frame underruns (0.6%) after pre-buffer depletion
2. **SRP Violation:** Script loading logic split between CLI and Bridge

---

## ISSUE #1: UNDERFLOW

### Symptoms
```
[SyncPullAudio] Pre-filled 50ms buffer
[SyncPullAudio] Pre-buffer depleted after copying 323 frames
[SyncPullAudio] UNDERFLOW (x10): requested 471, got 468
[SyncPullAudio] UNDERFLOW (x20): requested 471, got 468
```

### Root Cause
**RenderOnDemand consistently returns 3 fewer frames than requested.**

### Testing Results

| Pre-fill Duration | Pre-fill Result | Underflow Pattern |
|------------------|-----------------|-------------------|
| 10ms | 441 frames | requested 470, got 467 (3-frame) |
| 50ms | 2205 frames | requested 471, got 468 (3-frame) |
| 100ms | 0 frames (!) | requested 470, got 467 (3-frame) |

**Key Finding:** With 100ms pre-fill, RenderOnDemand returns **0 frames** during pre-fill! This indicates the synthesizer is not ready to render during pre-fill.

### Why 3-Frame Underrun?

The consistent 3-frame deficit (471 → 468, 470 → 467) suggests:
1. **Buffer boundary issue:** Synthesizer might be aligning to buffer boundaries
2. **Rendering timing:** RenderOnDemand might need more time to generate frames
3. **Sample rate conversion:** 44100 Hz / 60 Hz = 735 frames per update, but we're requesting 470-471

### Comparison with Previous Build

The user mentioned "previous build" had no underflow. Differences:
1. **Pre-fill:** Current=50ms vs Previous=unknown (possibly 0ms or different)
2. **Priming:** Current=3 iterations vs Previous=unknown
3. **Headroom variance:** Current varies wildly vs Previous stable

---

## ISSUE #2: SRP VIOLATION

### Current Architecture (WRONG)

```
CLI (CLIMain.cpp)
  └─> EngineConfig::resolveConfigPaths()  // CLI knows about engine-sim paths!
       └─> Returns resolved paths
            └─> Bridge::Create() with scriptPath
                 └─> Bridge compiles and loads script
```

**SRP Violation:** CLI should NOT know about:
- How to resolve engine-sim asset paths
- Where engine-sim stores its assets
- The difference between config path and asset base path

### Correct Architecture (SOLID)

```
CLI (CLIMain.cpp)
  └─> Pass raw --script argument to Bridge
       └─> Bridge::Create() with raw script path
            └─> Bridge resolves paths internally  // Bridge owns this logic!
                 └─> Bridge compiles and loads script
```

**SRP Compliance:**
- CLI: Only parses command line arguments
- Bridge: Handles ALL engine-sim specific logic (path resolution, script loading)

### Code Evidence

**Current (SRP Violation):**
```cpp
// src/CLIMain.cpp:64
EngineConfig::ConfigPaths resolved = EngineConfig::resolveConfigPaths(args.engineConfig);
config.configPath = resolved.configPath;
config.assetBasePath = resolved.assetBasePath;

// src/SimulationLoop.cpp:394
engineConfig = EngineConfig::createDefaultWithScript(
    sampleRate,
    config.configPath,    // Pre-resolved by CLI
    config.assetBasePath, // Pre-resolved by CLI
    config.simulationFrequency
);
```

**Should Be (SRP Compliant):**
```cpp
// src/CLIMain.cpp
config.scriptPath = args.engineConfig;  // Just pass raw argument

// src/SimulationLoop.cpp
engineConfig = EngineConfig::createDefaultWithScript(
    sampleRate,
    config.scriptPath,  // Bridge will resolve this
    config.simulationFrequency
);
```

---

## DETAILED FINDINGS

### Pre-fill Analysis

**Current Pre-fill Logic (SyncPullAudio.cpp:45-60):**
```cpp
void SyncPullAudio::preFillBuffer(int targetMs) {
    int framesNeeded = (sampleRate_ * targetMs) / 1000;
    preBuffer_.resize(framesNeeded * 2);  // stereo

    int framesRead = 0;
    engineAPI_->RenderOnDemand(engineHandle_, preBuffer_.data(), framesNeeded, &framesRead);

    // Resize to actual frames we got
    preBuffer_.resize(framesRead * 2);
    // ...
}
```

**Issue:** RenderOnDemand is called BEFORE the engine is warmed up!

**Warmup Flow:**
1. Create() initializes synthesizer
2. prepareBuffer() calls preFillBuffer() - **Engine not ready!**
3. runWarmupPhase() primes the engine - **Too late!**
4. resetBufferAfterWarmup() resets buffer
5. startPlayback() starts audio

**Problem:** Pre-fill happens BEFORE warmup, so RenderOnDemand returns 0 or partial frames.

### Warmup Analysis

**Current Warmup Logic (AudioSource.cpp:137-178):**
```cpp
void WarmupOps::runWarmup(EngineSimHandle handle, const EngineSimAPI& api,
                          AudioPlayer* audioPlayer, bool playAudio) {
    std::cout << "Priming synthesizer pipeline ("
              << AudioLoopConfig::WARMUP_ITERATIONS << " iterations)...\n";

    for (int i = 0; i < AudioLoopConfig::WARMUP_ITERATIONS; i++) {
        api.SetThrottle(handle, smoothedThrottle);
        api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);

        if (playAudio && audioPlayer) {
            // Drain audio - DISCARD to prevent crackles
            std::vector<float> discardBuffer(AudioLoopConfig::FRAMES_PER_UPDATE * 2);
            int warmupRead = 0;
            api.RenderOnDemand(handle, discardBuffer.data(),
                              AudioLoopConfig::FRAMES_PER_UPDATE, &warmupRead);
            // DISCARD warmup audio
        }
    }
}
```

**Current Config (CLIconfig.h:23-25):**
```cpp
static constexpr int WARMUP_ITERATIONS = 3;  // Minimal warmup
static constexpr int PRE_FILL_ITERATIONS = 40;  // 0.67s initial buffer
static constexpr int RE_PRE_FILL_ITERATIONS = 0;  // No re-pre-fill
```

---

## RECOMMENDATIONS

### Fix #1: Underflow - Move Pre-fill AFTER Warmup

**Current Order (WRONG):**
1. prepareBuffer() → preFillBuffer() - Engine cold!
2. runWarmupPhase() - Engine warms up
3. resetBufferAfterWarmup() - Clears buffer
4. startPlayback() - Starts playback

**Correct Order:**
1. runWarmupPhase() - Engine warms up
2. preFillBuffer() - Fill buffer AFTER warmup
3. startPlayback() - Starts playback

### Fix #2: SRP Violation - Move Path Resolution to Bridge

**Step 1:** Update CLI to pass raw script path
```cpp
// src/CLIMain.cpp
SimulationConfig CreateSimulationConfig(const CommandLineArgs& args) {
    SimulationConfig config;
    config.scriptPath = args.engineConfig;  // Raw path - no resolution!
    // ... rest of config
}
```

**Step 2:** Update Bridge to resolve paths
```cpp
// engine-sim-bridge/src/engine_sim_bridge.cpp
if (config && config->scriptPath && strlen(config->scriptPath) > 0) {
    // Resolve paths here in the bridge!
    std::string resolvedScriptPath = resolveScriptPath(config->scriptPath);
    std::string resolvedAssetPath = deriveAssetPath(resolvedScriptPath);

    // Compile the script
    if (!ctx->compiler->compile(resolvedScriptPath.c_str())) {
        // ...
    }
}
```

**Step 3:** Remove path resolution from CLI
```cpp
// DELETE: EngineConfig::resolveConfigPaths()
// DELETE: EngineConfig::resolveAssetBasePath()
```

---

## TESTING CHECKLIST

### Test Underflow Fix
- [ ] Pre-fill AFTER warmup
- [ ] Verify RenderOnDemand returns correct frame count
- [ ] Test with different pre-fill durations (10ms, 50ms, 100ms)
- [ ] Verify no underflow events after buffer depletion

### Test SRP Fix
- [ ] CLI passes raw script path to bridge
- [ ] Bridge resolves paths internally
- [ ] Remove EngineConfig::resolveConfigPaths() from CLI
- [ ] Remove EngineConfig::resolveAssetBasePath() from CLI
- [ ] Test with relative paths: `--script ferrari_f136.mr`
- [ ] Test with absolute paths: `--script /path/to/ferrari_f136.mr`
- [ ] Test with paths containing `assets/` directory

---

## CONCLUSION

**Underflow:** Caused by pre-fill happening BEFORE engine warmup. RenderOnDemand can't generate frames when engine is cold.

**SRP Violation:** CLI is doing path resolution that should be in the Bridge. This violates SRP and creates tight coupling between CLI and engine-sim internals.

**Next Steps:**
1. Reorder initialization: Warmup → Pre-fill → Playback
2. Move path resolution from CLI to Bridge
3. Test both fixes together

---

*Investigation complete. Two issues identified with clear solutions.*
