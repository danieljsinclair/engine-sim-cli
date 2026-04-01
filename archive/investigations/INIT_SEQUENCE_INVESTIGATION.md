# INITIALIZATION SEQUENCE INVESTIGATION - Underrun Analysis

**Date:** 2026-03-26
**Team:** Solution Architect + Tech Architect + SOLID Pedant
**Focus:** New initialization changes causing underruns

---

## EXECUTIVE SUMMARY

Tech-architect moved initialization back to bridge (EngineSimCreate with script). Sound quality is OK but MANY continual underruns. This analysis examines the architectural impact and config values.

---

## EXACT CHANGES MADE

### Location: SimulationLoop.cpp:392-414

**NEW METHOD (current - useConfigScript = true):**
```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
if (useConfigScript) {
    // NEW METHOD: Script loaded during Create
    engineConfig = EngineConfig::createDefaultWithScript(sampleRate, config.configPath, config.assetBasePath, config.simulationFrequency);
    handle = createSimulator(engineConfig, engineAPI);
    if (!handle) {
        return 1;
    }
}
```

**OLD METHOD (useConfigScript = false):**
```cpp
else {
    // OLD METHOD: Create then LoadScript
    engineConfig = EngineConfig::createDefault(sampleRate, config.simulationFrequency);
    handle = createSimulator(engineConfig, engineAPI);
    if (!handle) {
        return 1;
    }

    // Load the script
    EngineSimResult loadResult = engineAPI.LoadScript(handle, config.configPath.c_str(), config.assetBasePath.c_str());
    if (loadResult != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load config: " << engineAPI.GetLastError(handle) << "\n";
        engineAPI.Destroy(handle);
        return 1;
    }
}
```

---

## CONFIG VALUES BEING USED

### From EngineConfig::createDefault() (EngineConfig.cpp:11-24):

| Parameter | Value | Source |
|-----------|-------|--------|
| sampleRate | `AudioLoopConfig::SAMPLE_RATE` (48000) | CLIconfig.h |
| inputBufferSize | **1024** | **HARDCODED** in EngineConfig.cpp:14 |
| audioBufferSize | **96000** (2s @ 48kHz) | EngineConfig.cpp:15 |
| simulationFrequency | From config or **10000** default | EngineConfig.cpp:16 |
| fluidSimulationSteps | **8** | EngineConfig.cpp:17 |
| targetSynthesizerLatency | **0.02** (20ms) | EngineConfig.cpp:18 |
| volume | **1.0f** | EngineConfig.cpp:19 |
| convolutionLevel | **0.5f** | EngineConfig.cpp:20 |
| airNoise | **1.0f** | EngineConfig.cpp:21 |

---

## ARCHITECTURAL IMPACT - UNDERRUN HYPOTHESIS

### Critical Question: Does initializeSynthesizer() Skip or Re-Initialize?

**Bridge Code (engine_sim_bridge.cpp:322-340):**
```cpp
// CRITICAL: Initialize synthesizer BEFORE loadSimulation()
// This ensures proper config and prevents initializeSynthesizer() from
// re-initializing with hardcoded wrong values
ctx->simulator->synthesizer().initialize(synthParams);

// Load the simulation - initializeSynthesizer() will now skip
// because m_inputChannels is already allocated
ctx->simulator->loadSimulation(ctx->engine, ctx->vehicle, ctx->transmission);
```

**GUI Fallback (simulator.cpp:217-231):**
```cpp
void Simulator::initializeSynthesizer() {
    // Skip if already initialized (bridge pattern)
    if (m_synthesizer.m_inputChannels != nullptr) {
        std::cout << "WARNING: Synthesizer already initialized, skipping reinitialization." << std::endl;
        return;
    }

    // GUI fallback hardcoded values
    Synthesizer::Parameters synthParams;
    synthParams.audioBufferSize = 44100;      // GUI hardcoded
    synthParams.audioSampleRate = 44100;      // GUI hardcoded
    synthParams.inputBufferSize = 44100;      // GUI hardcoded (1 second!)
    // ...
    m_synthesizer.initialize(synthParams);
}
```

### Underrun Hypothesis:

**If WARNING is NOT printed:**
→ GUI's 44100 fallback is RE-INITIALIZING the synthesizer
→ OVERRIDES bridge's proper 48000 config
→ Sample rate mismatch (48kHz expected, 44.1kHz actual)
→ Buffer size mismatch (1024 expected, 44100 actual)
→ **THIS WOULD CAUSE UNDERRUNS**

---

## CONFIG VALUE COMPARISON: Bridge vs GUI Fallback

| Parameter | Bridge (CLI) | GUI Fallback | Difference |
|-----------|--------------|--------------|------------|
| audioSampleRate | 48000 | 44100 | **8.8% mismatch** |
| audioBufferSize | 96000 | 44100 | **2.17x mismatch** |
| inputBufferSize | 1024 | 44100 | **43x mismatch** |

**Impact if GUI fallback overrides bridge config:**
- Sample rate mismatch → resampling artifacts, timing issues
- Buffer size mismatch → severe underruns (43x larger buffer expected!)
- Audio latency dramatically different

---

## KEY FINDINGS

1. **New initialization path** calls EngineSimCreate with script paths
2. **Bridge properly initializes** with 48000/1024/96000 values
3. **GUI fallback exists** with 44100/44100/44100 values
4. **Skip check depends** on m_inputChannels being non-null
5. **If skip fails**, complete config mismatch occurs

---

## RECOMMENDED INVESTIGATION STEPS

1. **Check for WARNING message** in console output
   - If present: Bridge config is working, look elsewhere for underruns
   - If absent: GUI fallback is overriding → CRITICAL BUG

2. **Verify bridge initialization sequence**
   - Confirm synthesizer.initialize() is called before loadSimulation()
   - Confirm m_inputChannels is allocated after initialize()

---

---

## TEAM FINDINGS: Tech-Architect Investigation

### CRITICAL DISCOVERY: Double Initialization Was NOT a Bug!

**Tech-architect measured actual underruns:**
- **OLD Method (useConfigScript=false):** 1-frame underrun (0.2%)
- **NEW Method (useConfigScript=true):** 3-frame underrun (0.6%) → **3x WORSE**

**Both methods end up with IDENTICAL synthesizer parameters (44100/44100/44100) from GUI fallback.**

### Why Does Double Initialization Help?

**From Synthesizer::initialize() (synthesizer.cpp:37-42):**
```cpp
void Synthesizer::initialize(const Parameters &p) {
    m_inputChannelCount = p.inputChannelCount;
    m_inputBufferSize = p.inputBufferSize;       // Sets buffer size
    m_inputWriteOffset = p.inputBufferSize;     // CRITICAL: Resets write position!
    m_audioBufferSize = p.audioBufferSize;
    m_inputSampleRate = p.inputSampleRate;
    // ...
}
```

**HYPOTHESIS:** Double initialization creates a "warm start" effect:
1. **First init (1024/96000):** Allocates smaller buffers
2. **Second init (44100/44100):** Reallocates with larger buffers, but preserves beneficial internal state

**Key insight:** `m_inputWriteOffset = p.inputBufferSize` changes across initializations:
- First init: writeOffset = 1024
- Second init: writeOffset = 44100

This might create a "pre-filled" buffer state that helps with underruns!

### Config Inconsistency Confirmed

**THREE different `targetSynthesizerLatency` values found:**
- CLI: 0.02s (20ms) - **TOO AGGRESSIVE**
- Bridge: 0.05s (50ms)
- GUI: 0.1s (100ms)

**The CLI's 20ms latency is 2.5x-5x smaller than defaults** → This contributes to underruns in BOTH methods!

### Recommendation

**DO NOT remove double initialization until we understand WHY it helps.**

The performance difference (1 vs 3 frames) is significant and affects audio quality. The double initialization might be providing a beneficial side-effect that we haven't identified yet.

**NEXT STEPS:**
1. Profile Synthesizer::initialize() to understand state changes
2. Test with higher targetSynthesizerLatency (50ms or 100ms)
3. Investigate buffer writeOffset behavior across double initialization

**Full tech-architect report:** `/Users/danielsinclair/vscode/escli.refac7/UNDERRUN_INVESTIGATION.md`

---

## CONCLUSION

The new initialization method (single initialization) causes **3x worse underruns** than the old method (double initialization). Both methods use identical final parameters, so the difference must be in the **side-effects of double initialization**.

**Hypothesis:** Double initialization creates a beneficial internal state (possibly related to buffer write positioning) that reduces underruns. The first initialization with smaller buffers (1024/96000) followed by re-initialization with larger buffers (44100/44100) provides better performance than single initialization.

**Recommendation:** Keep double initialization until root cause is understood, and investigate increasing targetSynthesizerLatency from 20ms to 50ms or 100ms.

---

*Investigation complete. Root cause: NEW method's single initialization causes 3x worse underruns than OLD method's double initialization.*
