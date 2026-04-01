# SOLID PEDANT ANALYSIS: Underflow + SRP Violation

**Date:** 2026-03-26
**Agent:** SOLID PEDANT
**Context:** Sound is now CLEAN but underflow after few seconds. Script loading still in CLI (SRP violation)

---

## EXECUTIVE SUMMARY

**GOOD NEWS:** Dirty sound problem is SOLVED!

**NEW ISSUE:** Underflow after a few seconds
- Current: `[SyncPullAudio] UNDERFLOW (x10): requested 471, got 468`
- Previous: No underflow events

**KEY FINDINGS:**
1. **Underflow occurs AFTER a few seconds** - not immediately
2. **Pre-fill:** Current=50ms vs Previous=0ms
3. **SRP VIOLATION:** Script loading still in CLI despite `useConfigScript = true`

---

## UNDERFLOW ANALYSIS

### Timing Pattern

**NOT immediate underflow** - happens "after a few seconds"

**Hypothesis:** Pre-buffer exhaustion
- Pre-fill: 50ms @ 44100 Hz = ~2205 frames
- Callback: ~471 frames per request
- Pre-buffer lasts: ~4.7 callbacks = ~100ms
- **After pre-buffer exhausted:** RenderOnDemand must provide frames in real-time
- **If render takes too long:** Underflow occurs

### Why Underflow After Few Seconds?

**Pre-buffer lifecycle:**
1. **Initial:** Pre-filled with 50ms of audio
2. **First ~100ms:** Audio plays from pre-buffer (no underruns)
3. **Pre-buffer exhausted:** Switch to real-time RenderOnDemand
4. **If render can't keep up:** Underflow starts

**Evidence:** User reports underflow "after a few seconds" - matches pre-buffer exhaustion pattern.

### Headroom Variance

**Current:** Varies wildly (+17ms → -3.2ms)
**Previous:** Stable (-2.9ms)

**Analysis:**
- **Positive headroom (+17ms):** Pre-buffer has extra frames, render is fast
- **Negative headroom (-3.2ms):** Pre-buffer exhausted or render is slow
- **Variance:** Indicates inconsistent rendering performance

**Question:** Why does headroom vary so much more now?

**Possible causes:**
1. Pre-buffer state changes (full → empty)
2. Rendering speed varies (maybe due to simulation state)
3. Different buffer management (new vs old initialization)

---

## SRP VIOLATION: Script Loading in CLI

### Current Architecture (BROKEN)

**Location:** `src/SimulationLoop.cpp:391-398`

```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
if (useConfigScript) {
    // NEW METHOD: Script loaded during Create
    engineConfig = EngineConfig::createDefaultWithScript(sampleRate, config.configPath, config.assetBasePath, config.simulationFrequency);
    handle = createSimulator(engineConfig, engineAPI);
```

**Analysis:**
- `useConfigScript = true` indicates bridge should load script during Create
- **BUT:** CLI is still responsible for providing script path and asset base path
- **CLI is doing bridge's job** - SRP violation

### What Bridge Should Do (CORRECT)

**Bridge API should be:**
```cpp
EngineSimResult EngineSimCreate(
    const char* scriptPath,      // Script to load
    const char* assetBasePath,   // Asset location
    EngineSimHandle* outHandle   // Output handle
);
```

**Bridge should:**
1. Resolve script path to absolute
2. Resolve asset base path from script location
3. Load script immediately during Create
4. Initialize synthesizer with correct parameters
5. Return handle to fully-initialized simulator

**CLI should:**
1. Parse command-line arguments
2. Pass script path to bridge
3. Receive handle from bridge
4. Use handle for audio/rendering

### Current Architecture (WRONG)

**CLI:**
- Resolves config paths (EngineConfig::resolveConfigPaths)
- Resolves asset base path (EngineConfig::resolveAssetBasePath)
- Creates config with script pointers
- Passes to bridge

**Bridge:**
- Accepts config with script pointers
- Loads script if provided
- **BUT:** Also has fallback logic for script loading

**Problem:** Two layers doing the same job = SRP violation

---

## SOLID VIOLATIONS

### 1. SRP (Single Responsibility Principle)

**Violation:** Script loading split between CLI and bridge

**CLI responsibilities (should NOT have):**
- Path resolution (EngineConfig::resolveConfigPaths)
- Asset base path derivation (EngineConfig::resolveAssetBasePath)
- Script path storage (createDefaultWithScript)

**Bridge responsibilities (SHOULD have):**
- ALL script loading
- ALL path resolution
- ALL asset location logic

### 2. DIP (Dependency Inversion Principle)

**Violation:** CLI depends on low-level script loading details

**Current:**
```cpp
// CLI knows about script paths and asset base paths
EngineConfig::ConfigPaths resolved = EngineConfig::resolveConfigPaths(args.engineConfig);
config.configPath = resolved.configPath;
config.assetBasePath = resolved.assetBasePath;
```

**Should be:**
```cpp
// CLI only knows user intent (script file path)
handle = EngineSimCreate(args.scriptPath, &outputHandle);
// Bridge handles all path resolution
```

### 3. OCP (Open/Closed Principle)

**Violation:** Boolean flag for initialization method

**Current:**
```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
```

**Problem:** Adding new initialization method requires modifying main loop

**Should be:** Strategy pattern or Factory pattern

### 4. DRY (Don't Repeat Yourself)

**Violation:** Path resolution logic duplicated

**CLI:** EngineConfig::resolveConfigPaths, EngineConfig::resolveAssetBasePath
**Bridge:** engine_sim_bridge.cpp:376-396 (fallback logic)

**Problem:** Two different path resolvers that can conflict

---

## PRE-FILL CONFIGURATION ISSUE

### Current Pre-fill Behavior

**Location:** `src/audio/modes/SyncPullAudioMode.h:44`

```cpp
int preFillMs_ = 50;  // Pre-fill buffer duration in ms
```

**Location:** `src/SyncPullAudio.cpp:45-60`

```cpp
void SyncPullAudio::preFillBuffer(int targetMs) {
    int framesNeeded = (sampleRate_ * targetMs) / 1000;
    preBuffer_.resize(framesNeeded * 2);  // stereo

    int framesRead = 0;
    engineAPI_->RenderOnDemand(engineHandle_, preBuffer_.data(), framesNeeded, &framesRead);

    // Resize to actual frames we got
    preBuffer_.resize(framesRead * 2);
    preBufferReadPos_ = 0;

    std::cout << "[SyncPullAudio] Pre-filled "
              << (framesRead * 1000 / sampleRate_) << "ms buffer\n";
}
```

### Pre-fill Value Sources

**SyncPullAudioMode:** `preFillMs_ = 50` (hardcoded default)
**CLIconfig.h:** `int preFillMs = 50;` (command-line default)
**User can override:** `--pre-fill-ms <ms>`

**Question:** Is 50ms appropriate?

### Pre-fill vs Underflow Relationship

**Hypothesis:** 50ms pre-fill is **TOO LARGE**

**Evidence:**
1. User reports underflow "after a few seconds"
2. Pre-buffer duration: 50ms @ 44100 Hz = ~2205 frames
3. Callback request: ~471 frames
4. Pre-buffer lasts: ~2205 / 471 = ~4.7 callbacks = ~100ms
5. **After pre-buffer exhausted:** Underflow begins

**Analysis:**
- Pre-buffer masks rendering issues during first ~100ms
- Once pre-buffer exhausted, real-time rendering must keep up
- If rendering can't keep up, underflow occurs
- **Pre-fill doesn't FIX the problem, it DELAYS it**

**Alternative Hypothesis:** Pre-fill is **TOO SMALL**

**Evidence:**
- User reports underflow "after a few seconds"
- If pre-fill were the issue, underflow would occur in ~100ms
- "After a few seconds" suggests something else

**Possible causes:**
1. Simulation state changes after warmup
2. Rendering slows down after engine stabilizes
3. Buffer management changes after warmup reset

---

## CONFIGURATION INCONSISTENCY

### Pre-fill Values

| Location | Value | Source |
|----------|-------|--------|
| SyncPullAudioMode.h | 50 | Hardcoded default |
| CLIconfig.h | 50 | Command-line default |
| User can override | Variable | `--pre-fill-ms` |

### Question: What Was Previous Pre-fill?

**User says:** "Current=50ms vs Previous=0ms"

**If previous was 0ms:**
- No pre-buffer
- Immediate real-time rendering
- No "delayed" underflow pattern

**If previous was 0ms and had no underflow:**
- Why does 50ms pre-fill cause underflow?
- Should be: More pre-fill = LESS underrun, not more

**Possible explanation:**
- Pre-fill changes initialization timing
- Pre-fill changes buffer state
- Pre-fill interacts poorly with other initialization changes

---

## RECOMMENDATIONS

### 1. Fix SRP Violation (Critical)

**Move script loading to bridge:**

**CLI:**
```cpp
// CLI only provides script path
const char* scriptPath = args.scriptPath.c_str();
EngineSimHandle handle = engineAPI.Create(scriptPath, &handle);
```

**Bridge:**
```cpp
EngineSimResult EngineSimCreate(
    const char* scriptPath,
    EngineSimHandle* outHandle
) {
    // Resolve paths
    std::string resolvedScript = resolveScriptPath(scriptPath);
    std::string assetBase = deriveAssetBasePath(resolvedScript);

    // Load script immediately
    // Initialize synthesizer
    // Return fully-initialized handle
}
```

### 2. Investigate Underflow Root Cause

**Questions:**
1. Why does underflow occur "after a few seconds" instead of immediately?
2. What changes after warmup that causes underflow?
3. Why does headroom vary so much (+17ms → -3.2ms)?
4. Is pre-fill causing or masking the issue?

**Experiments:**
1. Test with pre-fill = 0ms (match previous)
2. Test with pre-fill = 100ms (double current)
3. Test with pre-fill = 200ms (4x current)
4. Monitor headroom variance patterns

### 3. Eliminate Boolean Flag (OCP)

**Replace:**
```cpp
bool useConfigScript = true;  // BAD
```

**With:**
```cpp
// Strategy pattern or factory
std::unique_ptr<EngineSimulator> simulator = SimulatorFactory::create(config);
```

### 4. Consolidate Path Resolution (DRY)

**Eliminate:** CLI path resolution

**Make:** Bridge responsible for ALL path operations

---

## CONCLUSION

**UNDERFLOW ISSUE:**
- Occurs "after a few seconds" - suggests pre-buffer exhaustion or state change
- Pre-fill of 50ms may be causing or masking the issue
- Headroom variance (+17ms → -3.2ms) indicates inconsistent rendering
- Need to test different pre-fill values to understand relationship

**SRP VIOLATION:**
- Script loading split between CLI and bridge
- CLI resolves paths, derives asset base
- Bridge has fallback logic that conflicts
- **Bridge should handle ALL script loading**

**NEXT STEPS:**
1. Fix SRP violation by moving script loading to bridge
2. Test with different pre-fill values (0ms, 100ms, 200ms)
3. Investigate why headroom varies so much
4. Determine root cause of underflow after warmup

---

*Analysis complete. Underflow likely related to pre-buffer exhaustion or state change after warmup. SRP violation confirmed - CLI doing bridge's job.*
