# Dropout Investigation - 2026-02-13

## Task #9: Reproduce 10% Throttle Dropouts

**Agent:** dropout-investigator
**Status:** BLOCKED - Critical deadlock bug found preventing engine mode from running

## Executive Summary

**Finding:** Engine mode does NOT work at all - it deadlocks immediately on startup. The reported "dropouts at 10% throttle" cannot be reproduced because engine mode never plays audio.

## Investigation Process

### 1. Architecture Analysis

Verified code sharing between mock (sine mode) and real (engine mode) implementations:

**Mock Implementation** (`mock_engine_sim.cpp`):
- Lines 16-88: Custom `MockRingBuffer<T>` template class
- Lines 103-353: Custom `MockSynthesizer` class with own threading
- Lines 324-330: Own cv0/mutex/threading primitives
- Lines 272-322: Own `audioRenderingThread()` and `renderAudio()` implementations

**Real Implementation** (`engine_sim_bridge.cpp`):
- Uses engine-sim's built-in `Synthesizer` class (include on line 8)
- Uses engine-sim's built-in `RingBuffer` (internal to engine-sim)
- Uses engine-sim's threading model (not duplicated in bridge)

**CRITICAL FINDING:** Mock and real DO NOT share buffer/thread/sync code (violates DRY principle).

### 2. Reproduction Attempts

**Sine Mode Test:**
```bash
./engine-sim-cli --sine --rpm 1000 --load 0.1 --duration 10 --play
```
**Result:** ✅ WORKS - smooth audio, no dropouts

**Engine Mode Test:**
```bash
./engine-sim-cli ../engine-sim-bridge/engine-sim/assets/main.mr --load 0.1 --duration 20 --play
```
**Result:** ❌ HANGS - process deadlocks after initial debug messages

**Console Output Before Hang:**
```
[SIMSTEP] simulateStep() FIRST CALL!
[WTS] writeToSynthesizer() FIRST CALL!
[INPUT] writeInput() FIRST CALL - engine IS feeding data!
[INPUT] Write #0 data[0]=0
[SYNTH] renderAudio(int) FIRST CALL - synthesizer IS being used!
```
Then: complete silence, no further output, process hangs indefinitely.

**WAV Export Test:**
```bash
./engine-sim-cli ../engine-sim-bridge/engine-sim/assets/main.mr --load 0.1 --duration 20 /tmp/test.wav
```
**Result:** ❌ HANGS - same behavior, no WAV file created

## Root Cause Analysis

### The Deadlock Bug

**Location:** `engine-sim-bridge/engine-sim/src/synthesizer.cpp:82-84`

**Buggy Code:**
```cpp
for (int i = 0; i < m_audioBufferSize; ++i) {  // Pre-fills 96000 samples!
    m_audioBuffer.write(0);
}
```

**Wait Condition (line 241-246):**
```cpp
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannels[0].data.size() > 0
        && m_audioBuffer.size() < 2000;  // <-- FAILS when buffer = 96000
    return !m_run || (inputAvailable && !m_processed);
});
```

### Deadlock Sequence

1. `Synthesizer::initialize()` pre-fills `m_audioBuffer` with 96000 zeros
2. `startAudioRenderingThread()` launches audio thread
3. Audio thread enters `renderAudio()`, evaluates cv0 wait predicate
4. `m_audioBuffer.size() < 2000` evaluates to FALSE (96000 < 2000 = false)
5. `inputAvailable` = FALSE
6. Wait condition = FALSE
7. Audio thread blocks on `cv0.wait()`
8. Main thread writes input samples, calls `endInputBlock()`, notifies cv0
9. Audio thread wakes, re-evaluates condition, still FALSE (buffer still has 96000 samples)
10. Audio thread blocks again
11. **PERMANENT DEADLOCK** - audio never drains because thread can't run to drain it

### Why Mock Works

**Mock's Corrected Code** (`mock_engine_sim.cpp:147-150`):
```cpp
const int preFillAmount = std::min(2000, m_audioBufferSize);
for (int i = 0; i < preFillAmount; ++i) {
    m_audioBuffer.write(0);
}
```

Pre-fills only 2000 samples (the target buffer level), so:
- Initial buffer size = 2000
- CLI starts draining buffer via `readAudioOutput()`
- Buffer size drops below 2000
- `m_audioBuffer.size() < 2000` becomes TRUE
- Audio thread can run
- System operates normally

### Why the Bug Exists

From MEMORY.md (Crackle Fix 2026-02-10):
> **Root cause 3:** Audio buffer pre-filled with 96000 zeros blocked audio thread (target level is 2000)
> **Fixes:** ... reduced pre-fill ...

**The fix was applied to mock_engine_sim.cpp but NOT to the real synthesizer.cpp!**

## Impact Assessment

### Current State
- **Sine mode (mock):** ✅ Fully functional, no dropouts
- **Engine mode (real):** ❌ Completely non-functional, deadlocks on startup
- **User-reported "dropouts":** ⚠️ Cannot be reproduced with current code

### Implications

**The reported "10% throttle dropouts" are impossible to reproduce** because:
1. Engine mode never starts running
2. No audio is ever produced
3. User cannot have experienced dropouts from code that doesn't execute

**Possible explanations:**
1. User has local modifications not in the repository
2. User is describing behavior from an earlier working version
3. User means something different by "engine mode"
4. There's an alternative code path that bypasses the deadlock

## Recommended Fix

**File:** `engine-sim-bridge/engine-sim/src/synthesizer.cpp`
**Lines:** 82-84

**Change FROM:**
```cpp
for (int i = 0; i < m_audioBufferSize; ++i) {
    m_audioBuffer.write(0);
}
```

**Change TO:**
```cpp
const int preFillAmount = std::min(2000, m_audioBufferSize);
for (int i = 0; i < preFillAmount; ++i) {
    m_audioBuffer.write(0);
}
```

This makes the real Synthesizer match the mock's corrected behavior.

## Next Steps

**Before proceeding, need user to confirm:**

1. **Can user run engine mode successfully?**
   - If YES → user has local modifications, need diff
   - If NO → confirms deadlock, apply fix above

2. **After deadlock is fixed:**
   - Re-test engine mode playback
   - Attempt to reproduce 10% throttle dropouts
   - If dropouts still occur, instrument code to capture them

## Architectural Concerns

**DRY Violation:** Mock and real maintain separate, diverging implementations of:
- Ring buffer data structures
- Threading primitives (cv0, mutexes)
- Audio rendering loops
- Buffer management logic

**Consequence:** Bugs fixed in one (like this pre-fill issue) are not automatically fixed in the other.

**Recommendation:** Refactor to share common buffer/threading code between mock and real implementations.

## Files Modified

- `/Users/danielsinclair/.claude/projects/-Users-danielsinclair-vscode-engine-sim-cli/memory/MEMORY.md` - Updated with deadlock findings

## Files Requiring Changes

- `engine-sim-bridge/engine-sim/src/synthesizer.cpp:82-84` - Apply pre-fill fix

## Test Commands

**Verify sine mode (should work):**
```bash
cd build
./engine-sim-cli --sine --rpm 1000 --load 0.1 --duration 10 --play
```

**Verify engine mode (currently hangs):**
```bash
cd build
./engine-sim-cli ../engine-sim-bridge/engine-sim/assets/main.mr --load 0.1 --duration 10 --play
```

**After fix, engine mode should:**
- Complete startup without hanging
- Play engine audio
- Exit cleanly after duration expires
