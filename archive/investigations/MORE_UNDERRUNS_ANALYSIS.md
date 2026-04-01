# SOLID PEDANT ANALYSIS: Why MORE Underruns Than Before?

**Date:** 2026-03-26
**Agent:** SOLID PEDANT
**Context:** User clarified - underruns are expected, but why MORE underruns than previous version?

---

## EXECUTIVE SUMMARY

**USER'S QUESTION:** "Why are we getting MORE underruns than the previous working version?"

**KEY OBSERVATION:** "We breach the underrun dam compared to the previous"

**EVIDENCE:**
- **Previous version:** Stable headroom (-2.9ms), fewer underruns
- **Current version:** Wild headroom variance (+17ms → -3.2ms), MORE underruns

---

## COMPARATIVE ANALYSIS

### What Changed Between Versions?

**Version Comparison:**

| Aspect | Previous Version | Current Version | Impact? |
|--------|------------------|-----------------|---------|
| Script loading | CLI (with path resolution) | Bridge (raw path) | **YES** |
| Pre-fill timing | After warmup? | Before warmup? | **YES** |
| Pre-fill timing (after fix) | Unknown | After warmup | **MAYBE** |
| Path resolution | CLI | Bridge | **YES** |
| Buffer sizes | Unknown | 44100/44100/44100 | **MAYBE** |

---

## HYPOTHESIS #1: Path Resolution Timing Impact

### Change: Path Resolution Moved from CLI to Bridge

**Before (CLI):**
```cpp
// CLI resolves paths BEFORE calling bridge
EngineConfig::ConfigPaths resolved = EngineConfig::resolveConfigPaths(args.engineConfig);
// Uses std::filesystem::absolute() - CWD dependent
```

**After (Bridge):**
```cpp
// CLI passes raw path
config.configPath = args.engineConfig;
// Bridge resolves paths internally
```

### Potential Impact on Underruns

**Question:** Does path resolution timing affect underrun count?

**Analysis:**
- **CLI path resolution:** Uses `std::filesystem::absolute()` which is CWD dependent
- **Bridge path resolution:** Uses different logic (string searching for `/assets/`)
- **Timing difference:** CLI resolution happens BEFORE bridge create, bridge resolution happens DURING create

**Hypothesis:** Path resolution timing differences could affect initialization timing, which could affect buffer state.

**Evidence Needed:**
- Does bridge path resolution take LONGER than CLI resolution?
- Does this delay affect buffer initialization?
- Could this cause buffer state differences?

### SOLID Violations

**SRP:** Path resolution moved from CLI to Bridge (GOOD)
**But:** Both use different methods, could have different timing characteristics

---

## HYPOTHESIS #2: Pre-fill Buffer Size Difference

### Change: Pre-fill Happens at Different Time

**Before (Unknown):**
- Pre-fill timing uncertain
- User reports "Previous=0ms" but this might be inaccurate
- Need to verify previous pre-fill behavior

**After (Current):**
- Pre-fill after warmup (just fixed)
- 50ms pre-fill @ 44100 Hz = ~2205 frames
- Gets 323 frames (efficiency improvement)

### Question: What Was Previous Pre-fill?

**If previous was 0ms:**
- No pre-buffer
- Immediate real-time rendering
- Consistent performance (no variance)

**If previous was 50ms but at different time:**
- Pre-fill at different initialization point
- Different engine state when pre-filled
- Different buffer state

**If previous was 100ms:**
- Larger pre-buffer
- More headroom against underruns
- Current 50ms is less than previous

**CRITICAL:** We don't actually KNOW what the previous pre-fill was!

### SOLID Violations

**DRY:** Pre-fill configuration is not centralized
- SyncPullAudioMode: `preFillMs_ = 50` (hardcoded)
- CLIconfig.h: `int preFillMs = 50` (default)
- ThreadedAudioMode: 100ms pre-fill (different!)

---

## HYPOTHESIS #3: Buffer Initialization Differences

### Change: Synthesizer Initialization Timing

**Before (Unknown):**
- When was synthesizer initialized?
- What parameters were used?
- Was there double initialization?

**After (Current):**
- Script loaded during Create (useConfigScript = true)
- Synthesizer initialized during loadSimulation
- Parameters: audioBufferSize=44100, inputBufferSize=44100

### Tech-Architect Finding (from earlier):**

**Double Initialization Effect:**
- **OLD method:** Create (1024/96000) → LoadScript (44100/44100) → 1-frame underrun
- **NEW method:** Create (skipped) → LoadScript (44100/44100) → 3-frame underrun

**Analysis:** Double initialization created larger buffer (memory allocator re-use)

**Current State:**
- We're using single initialization (NEW method)
- Getting 1-frame underrun (after pre-fill fix)
- **But previous might have used double initialization (OLD method)**

**Question:** Did previous version use double initialization?

### SOLID Violations

**Hidden State:** Memory allocator behavior affects performance
- Non-deterministic across different allocators
- Accidental "benefit" from double initialization
- Not reproducible or reliable

---

## HYPOTHESIS #4: Headroom Variance Source

### Observation: Wild Headroom Variance

**Current:** +17ms → -3.2ms (wild variance)
**Previous:** -2.9ms (stable)

**Question:** What causes headroom variance?

**Potential Causes:**
1. **Pre-buffer state transitions:** Full → Empty → Real-time rendering
2. **Engine state changes:** Cold → Warm → Stabilized
3. **Rendering speed variance:** Different simulation complexity
4. **Buffer management changes:** Different buffer sizes

**Analysis:**
- Pre-buffer masks initial rendering issues
- Once pre-buffer exhausted, real-time rendering varies
- If simulation complexity increases, rendering slows down
- Slower rendering = negative headroom = underruns

**Question:** Does current version have different simulation complexity than previous?

---

## CODE QUALITY ISSUES

### 1. Pre-fill Configuration Fragmented

**SyncPullAudioMode.h:44**
```cpp
int preFillMs_ = 50;  // Hardcoded
```

**CLIconfig.h:**
```cpp
int preFillMs = 50;  // Default
```

**ThreadedAudioMode.cpp:122-123**
```cpp
// 100ms pre-fill
context->writePointer.store(static_cast<int>(sampleRate * 0.1));
```

**Problem:** THREE different pre-fill values/locations

**DRY Violation:** Pre-fill logic not centralized

### 2. Boolean Flag Still Present

**SimulationLoop.cpp:391**
```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
```

**Problem:** Still allowing both code paths

**OCP Violation:** Not using Strategy pattern

### 3. Unknown Previous Behavior

**Critical Gap:** We don't actually KNOW:
- What pre-fill value previous version used
- Whether previous used double or single initialization
- What buffer sizes previous version used
- When previous version did pre-fill relative to warmup

**Evidence Problem:** Comparing to UNKNOWN baseline

---

## ROOT CAUSE ANALYSIS

### Most Likely Cause: Unknown Previous Behavior

**Problem:** We're comparing to a "previous version" without knowing:
1. What pre-fill value it used
2. What initialization order it used
3. Whether it used double initialization
4. What buffer sizes it used

**Hypothesis:** Previous version might have had:
- Larger pre-fill (100ms vs 50ms)
- Different initialization order (pre-fill AFTER warmup AND after something else)
- Double initialization (accidental buffer size increase)
- Different buffer sizes (96000 vs 44100)

### Secondary Cause: Headroom Variance

**Current:** Wild variance (+17ms → -3.2ms)
**Previous:** Stable (-2.9ms)

**Analysis:**
- Pre-buffer creates artificial headroom (+17ms)
- Once exhausted, headroom goes negative (-3.2ms)
- Previous might have had:
  - Larger pre-buffer (more consistent headroom)
  - Different buffer management (more stable)
  - Different simulation complexity (more consistent rendering)

---

## RECOMMENDATIONS

### 1. Establish Baseline (Critical)

**Need:** Actually KNOW what previous version was doing

**Action:**
1. Check git history for previous working version
2. Measure actual pre-fill value in previous version
3. Determine initialization order in previous version
4. Compare buffer sizes between versions

### 2. Test Pre-fill Values

**Experiment:**
- Test with 0ms pre-fill (match claimed previous)
- Test with 100ms pre-fill (double current)
- Test with 200ms pre-fill (4x current)
- Measure underrun count for each

**Question:** Does increasing pre-fill reduce underrun count?

### 3. Compare Initialization Methods

**Experiment:**
- Test with `useConfigScript = false` (OLD method)
- Test with `useConfigScript = true` (NEW method)
- Measure underrun count for each

**Question:** Does double initialization reduce underruns?

### 4. Profile Rendering Performance

**Action:**
- Measure actual rendering time per callback
- Compare between versions
- Identify performance differences

**Question:** Is current version slower at rendering?

---

## CONCLUSION

**THE PROBLEM:** We're comparing to UNKNOWN baseline

**CRITICAL GAP:** We don't actually KNOW:
- What pre-fill value previous version used
- What initialization order previous version used
- Whether previous used double initialization
- What buffer sizes previous version used

**MOST LIKELY CAUSES:**

1. **Unknown previous pre-fill value**
   - Current: 50ms
   - Previous: UNKNOWN (might be 0ms, 100ms, or something else)
   - If previous was 100ms: More pre-buffer = fewer underruns

2. **Unknown previous initialization method**
   - Current: Single initialization (script during Create)
   - Previous: UNKNOWN (might be double initialization)
   - If previous was double: Larger buffer (accidental) = fewer underruns

3. **Headroom variance source**
   - Current: Pre-buffer creates artificial +17ms, then drops to -3.2ms
   - Previous: Stable -2.9ms suggests different buffer management
   - Previous might have had larger/more consistent buffer

**RECOMMENDED ACTIONS:**

1. **ESTABLISH BASELINE:** Determine actual previous version behavior
2. **TEST PRE-FILL VALUES:** 0ms, 50ms, 100ms, 200ms
3. **TEST INITIALIZATION METHODS:** Single vs double
4. **PROFILE RENDERING:** Measure actual performance differences

**WITHOUT KNOWING THE BASELINE, WE'RE GUESSING.**

---

*Analysis complete. Root cause is UNKNOWN previous baseline. Need to establish what previous version was actually doing.*
