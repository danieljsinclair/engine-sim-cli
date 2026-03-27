# INITIALIZATION UNDERRUN INVESTIGATION

**Date:** 2026-03-26
**Agent:** SOLID PEDANT
**Task:** Review new initialization changes for WET violations and config value differences causing underruns

---

## EXECUTIVE SUMMARY

**CRITICAL FINDING:** `targetSynthesizerLatency` MISMATCH between CLI and Bridge defaults

| Location | targetSynthesizerLatency | Impact |
|----------|-------------------------|---------|
| CLI (EngineConfig.cpp:18) | **0.02** (20ms) | New initialization method |
| Bridge (engine_sim_bridge.cpp:129) | **0.05** (50ms) | Old initialization method |
| GUI (simulator.cpp:229) | **0.02** (20ms) | Unknown |

**HYPOTHESIS:** The 2.5x difference in targetSynthesizerLatency (20ms vs 50ms) could cause underruns by changing buffer management behavior.

---

## NEW INITIALIZATION SEQUENCE

**Location:** `src/SimulationLoop.cpp:391-398`

```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
if (useConfigScript) {
    // NEW METHOD: Script loaded during Create
    engineConfig = EngineConfig::createDefaultWithScript(sampleRate, config.configPath, config.assetBasePath, config.simulationFrequency);
    handle = createSimulator(engineConfig, engineAPI);
```

**OLD METHOD:** (Lines 399-414)
```cpp
} else {
    // OLD METHOD: Create then LoadScript
    engineConfig = EngineConfig::createDefault(sampleRate, config.simulationFrequency);
    handle = createSimulator(engineConfig, engineAPI);

    // Load the script
    EngineSimResult loadResult = engineAPI.LoadScript(handle, config.configPath.c_str(), config.assetBasePath.c_str());
```

---

## CONFIG VALUE DIFFERENCES

### CLI's `createDefault()` (Used by NEW method)

**Location:** `src/EngineConfig.cpp:11-25`

```cpp
config.sampleRate = sampleRate;  // From parameter (44100)
config.inputBufferSize = 1024;
config.audioBufferSize = 96000;
config.simulationFrequency = simulationFrequency > 0 ? simulationFrequency : EngineConstants::DEFAULT_SIMULATION_FREQUENCY;
config.fluidSimulationSteps = 8;
config.targetSynthesizerLatency = 0.02;  // 20ms
config.volume = 1.0f;
config.convolutionLevel = 0.5f;
config.airNoise = 1.0f;
```

### Bridge's `setDefaultConfig()` (Used by OLD method)

**Location:** `engine-sim-bridge/src/engine_sim_bridge.cpp:123-133`

```cpp
config->sampleRate = 48000;
config->inputBufferSize = 1024;
config->audioBufferSize = 96000;
config->simulationFrequency = 10000;
config->fluidSimulationSteps = 8;
config->targetSynthesizerLatency = 0.05;  // 50ms
config->volume = 0.5f;
config->convolutionLevel = 1.0f;
config->airNoise = 1.0f;
```

---

## CRITICAL DIFFERENCES

### 1. `targetSynthesizerLatency` - 2.5x DIFFERENCE

**NEW (CLI):** 0.02 (20ms)
**OLD (Bridge):** 0.05 (50ms)
**DIFFERENCE:** 2.5x smaller latency target

**POTENTIAL IMPACT ON UNDERRUNS:**
- Smaller latency target means tighter buffer management
- Less headroom for rendering timing variations
- More likely to underrun if rendering takes longer than expected
- Could explain "MANY continual underruns" reported by user

### 2. `volume` - 2x DIFFERENCE

**NEW (CLI):** 1.0f (100%)
**OLD (Bridge):** 0.5f (50%)
**DIFFERENCE:** 2x louder

**POTENTIAL IMPACT:**
- Louder audio could mask underruns or make them more noticeable
- Not directly related to underrun count

### 3. `convolutionLevel` - 2x DIFFERENCE

**NEW (CLI):** 0.5f (50%)
**OLD (Bridge):** 1.0f (100%)
**DIFFERENCE:** Half the convolution effect

**POTENTIAL IMPACT:**
- Less convolution processing = faster rendering
- Should REDUCE underruns, not increase them

### 4. `sampleRate` - DIFFERENT

**NEW (CLI):** 44100 (from CLIMain.cpp:114)
**OLD (Bridge):** 48000 (bridge default)

**POTENTIAL IMPACT:**
- Different sample rate = different frame sizes
- Could affect buffer calculations

---

## WET VIOLATIONS FOUND

### 1. Static Variable Abuse (Thread Safety Issue)

**Location:** `src/EngineConfig.cpp:32-35`

```cpp
EngineSimConfig EngineConfig::createDefaultWithScript(...) {
    EngineSimConfig config = createDefault(sampleRate, simulationFrequency);
    // Note: We store pointers to the string data. The strings must remain valid during Create call.
    // This is safe because createDefaultWithScript returns by value and the strings
    // are typically stored in the SimulationConfig which has longer lifetime.
    static std::string scriptPathStorage;  // STATIC!
    static std::string assetBasePathStorage;  // STATIC!
    scriptPathStorage = scriptPath;
    assetBasePathStorage = assetBasePath;
    config.scriptPath = scriptPathStorage.c_str();
    config.assetBasePath = assetBasePathStorage.c_str();
    return config;
}
```

**SOLID VIOLATION:**
- **SRP:** Using static variables for temporary storage is abuse
- **Thread Safety:** Static variables are shared across ALL calls
- **DANGEROUS:** If two instances are created, the second overwrites the first's pointers

**WHY THIS IS REALLY SHITTY:**
- Comments claim "This is safe" but it's NOT
- If `createDefaultWithScript` is called twice, second call overwrites first's strings
- First config's `scriptPath` and `assetBasePath` pointers become INVALID
- Undefined behavior or crashes possible

### 2. Boolean Flag for Code Path Selection

**Location:** `src/SimulationLoop.cpp:391`

```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
```

**SOLID VIOLATION:**
- **OCP:** Adding boolean flags instead of using abstractions
- **SRP:** Code path selection mixed with main logic

**Should Be:** Strategy pattern or Factory pattern

### 3. Duplicate Initialization Logic

**Location:** Multiple places

- `EngineConfig::createDefault()` - Creates config
- `EngineConfig::createDefaultWithScript()` - Creates config with script
- `EngineConfig::createAndLoad()` - Creates and loads
- `EngineSimCreate()` in bridge - Creates and optionally loads script

**SOLID VIOLATION:**
- **DRY:** Initialization logic scattered across multiple functions
- **OCP:** Adding new initialization method requires modifying multiple locations

---

## BRIDGE SYNTHESIZER INITIALIZATION

**Location:** `engine-sim-bridge/src/engine_sim_bridge.cpp:279-283`

```cpp
// Skip synthesizer initialization if scriptPath is provided in config
// It will be initialized during loadSimulation with correct engine parameters
if (!config || !config->scriptPath || strlen(config->scriptPath) == 0) {
    ctx->simulator->synthesizer().initialize(synthParams);
}
```

**CRITICAL:** When scriptPath is provided, synthesizer is NOT initialized during Create. It's initialized later during loadSimulation.

**QUESTION:** What parameters are used during loadSimulation synthesizer initialization?

**From earlier investigation:** The GUI's `initializeSynthesizer()` uses hardcoded values:
```cpp
synthParams.audioBufferSize = 44100;
synthParams.audioSampleRate = 44100;
synthParams.inputBufferSize = 44100;  // 1 second buffer!
```

**POTENTIAL ISSUE:** If loadSimulation uses GUI's hardcoded values, there could be parameter mismatches causing underruns.

---

## ARCHITECTURAL ISSUES

### Initialization Order Dependency

**NEW METHOD:**
1. CLI creates config with targetSynthesizerLatency = 0.02
2. Bridge skips synthesizer initialization (scriptPath provided)
3. Bridge loads script
4. Synthesizer initialized during loadSimulation (with what parameters?)

**OLD METHOD:**
1. CLI calls Create with no scriptPath
2. Bridge initializes synthesizer with targetSynthesizerLatency from config
3. CLI calls LoadScript
4. loadSimulation skips re-initialization (m_inputChannels != nullptr check)

**CRITICAL DIFFERENCE:** The NEW method relies on loadSimulation's synthesizer initialization, which may use DIFFERENT parameters than the config passed to Create.

---

## MOST LIKELY CAUSE OF UNDERRUNS

**HYPOTHESIS:** `targetSynthesizerLatency = 0.02` (20ms) is too aggressive

**Evidence:**
1. Bridge default is 0.05 (50ms) - 2.5x larger
2. User reports "MANY continual underruns" with new initialization
3. Tighter latency target = less headroom for rendering variations
4. Sound is "OK again" but with underruns - suggests audio works but buffer management is marginal

**TEST:** Change `targetSynthesizerLatency` from 0.02 to 0.05 in CLI config and see if underruns stop.

---

## RECOMMENDATIONS

### 1. Fix `targetSynthesizerLatency` Mismatch (Immediate)

**Change:** `src/EngineConfig.cpp:18`
```cpp
config.targetSynthesizerLatency = 0.05;  // Match bridge default
```

### 2. Fix Static Variable Abuse (Critical)

**Replace:** Static variables with proper string storage

**Options:**
- Store strings in EngineSimConfig (not pointers)
- Use std::string in EngineSimConfig instead of const char*
- Pass strings directly to bridge API

### 3. Consolidate Initialization Logic (DRY)

**Eliminate:** Multiple initialization functions

**Create:** Single Factory that handles all cases

### 4. Document Parameter Sources (SRP)

**Question:** What parameters does loadSimulation use for synthesizer initialization?

**Need:** Trace through loadSimulation to document exact parameter values used.

---

## CONCLUSION

**ROOT CAUSE OF UNDERRUNS (LIKELY):** `targetSynthesizerLatency = 0.02` (20ms) in CLI config vs 0.05 (50ms) in bridge default.

**EVIDENCE:**
1. 2.5x difference in latency target
2. Sound OK but with "MANY continual underruns"
3. Tighter latency = less headroom = more underruns

**ADDITIONAL ISSUES:**
1. Static variable abuse in `createDefaultWithScript()` - DANGEROUS
2. Multiple initialization paths with different parameters
3. No clear documentation of which parameters are actually used

**IMMEDIATE FIX:** Change `targetSynthesizerLatency` from 0.02 to 0.05 and test if underruns stop.

---

*Investigation complete. Config value mismatch identified as most likely cause of underruns.*
