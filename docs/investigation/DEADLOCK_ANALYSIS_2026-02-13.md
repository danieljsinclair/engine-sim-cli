# Deadlock Analysis - Engine Mode Fatal Bugs

**Date:** 2026-02-13
**Investigator:** dropout-investigator (AI Agent)
**Task:** #9 - Reproduce 10% throttle dropouts

## Executive Summary

Engine mode is completely non-functional due to TWO related bugs in the real Synthesizer class:

1. **Buffer Pre-fill Bug:** Pre-fills 96000 samples instead of 2000, violating cv0.wait() predicate
2. **Missing renderAudioSync():** WAV mode calls threading method without audio thread → deadlock

Both bugs were fixed in mock but not propagated to real Synthesizer. Mock works perfectly, real is broken.

## Detailed Analysis

### Bug #1: Buffer Pre-fill Deadlock

**Location:** `engine-sim-bridge/engine-sim/src/synthesizer.cpp:82-84`

**Buggy Code:**
```cpp
for (int i = 0; i < m_audioBufferSize; ++i) {  // 96000 iterations!
    m_audioBuffer.write(0);
}
```

**Correct Code (from mock):**
```cpp
const int preFillAmount = std::min(2000, m_audioBufferSize);
for (int i = 0; i < preFillAmount; ++i) {
    m_audioBuffer.write(0);
}
```

**Why This Breaks:**

The audio thread wait condition (synthesizer.cpp:241-246):
```cpp
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannels[0].data.size() > 0
        && m_audioBuffer.size() < 2000;  // <-- CRITICAL LINE
    return !m_run || (inputAvailable && !m_processed);
});
```

**Deadlock Sequence:**
1. `initialize()` pre-fills m_audioBuffer with 96000 zeros
2. Audio thread checks: `m_audioBuffer.size() < 2000` → FALSE (96000 < 2000 = false)
3. `inputAvailable` = FALSE
4. Wait predicate = FALSE
5. Thread blocks on cv0.wait()
6. Main thread writes input and notifies cv0
7. Thread wakes, re-checks predicate, still FALSE
8. Thread blocks again
9. **PERMANENT DEADLOCK**

**Impact:**
- Playback mode: Audio thread can't start producing samples
- WAV mode: Main thread calling `renderAudio()` blocks forever

### Bug #2: Missing renderAudioSync()

**Location:** Real Synthesizer class (engine-sim/src/synthesizer.cpp)

**Problem:** Mock has `renderAudioSync()`, real doesn't.

**Mock Synthesizer** (mock_engine_sim.cpp:241-263):
```cpp
// Synchronous audio render: process all pending input without cv0/audio thread.
// Used for WAV-only export mode where no audio thread is running.
void renderAudioSync() {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int available = m_inputChannel.size();
    if (available <= 0) return;

    const int n = std::min(available, static_cast<int>(m_transferBuffer.size()));
    m_inputChannel.readAndRemove(n, m_transferBuffer.data());

    for (int i = 0; i < n; ++i) {
        float sample = m_transferBuffer[i];
        sample = std::max(-1.0f, std::min(1.0f, sample));
        int16_t intSample = static_cast<int16_t>(sample * 32767.0f);
        m_audioBuffer.write(intSample);
    }

    m_processed = true;
}
```

**Real Synthesizer:** Missing this method entirely!

**Bridge Calls:**

Mock (mock_engine_sim.cpp:859):
```cpp
ctx->synthesizer.renderAudioSync();  // ✅ No cv0.wait()
```

Real (engine_sim_bridge.cpp:527):
```cpp
ctx->simulator->synthesizer().renderAudio();  // ❌ Has cv0.wait()!
```

**Why This Matters:**

In WAV-only mode:
1. CLI doesn't start audio thread (engine_sim_cli.cpp:1314)
2. Main loop calls `EngineSimRender()` for each frame
3. Bridge calls `renderAudio()` thinking it's synchronous
4. But `renderAudio()` has cv0.wait() expecting audio thread
5. Main thread blocks waiting for thread that doesn't exist
6. **DEADLOCK**

## Code Path Comparison

### Mock (Working)

**Playback Mode:**
```
CLI → StartAudioThread()
Audio Thread → renderAudio() [cv0.wait()] → processes samples
Main Thread → ReadAudioBuffer() → reads from buffer
✅ Works (with buffer pre-fill fix)
```

**WAV Mode:**
```
CLI → EngineSimRender()
Mock → renderAudioSync() [NO cv0.wait()] → processes samples directly
Mock → readAudioOutput() → reads from buffer
✅ Works perfectly
```

### Real (Broken)

**Playback Mode:**
```
CLI → StartAudioThread()
Audio Thread → renderAudio() [cv0.wait()]
   → Checks: m_audioBuffer.size() < 2000
   → FALSE (buffer has 96000 samples)
   → BLOCKS FOREVER
❌ Deadlock
```

**WAV Mode:**
```
CLI → EngineSimRender()
Bridge → renderAudio() [cv0.wait()]
   → Checks: m_audioBuffer.size() < 2000
   → FALSE (buffer has 96000 samples)
   → BLOCKS FOREVER (no audio thread to wake it!)
❌ Deadlock
```

### GUI (Working, Different Architecture)

**Playback Mode:**
```
GUI → startAudioRenderingThread()
Audio Thread → renderAudio() [cv0.wait()] → processes samples
Main Thread → readAudioOutput() → reads from buffer
✅ Works (buffer pre-filled correctly by original code)
```

**Note:** GUI doesn't have WAV-only mode, doesn't use `EngineSimRender()`.

## Test Evidence

### Test 1: Sine Mode (Mock) ✅
```bash
./engine-sim-cli --sine --rpm 1000 --load 0.1 --duration 10 --play
```
**Result:** Audio plays smoothly, exits cleanly

### Test 2: Engine Mode Playback ❌
```bash
./engine-sim-cli ../engine-sim-bridge/engine-sim/assets/main.mr --load 0.1 --duration 20 --play
```
**Output:**
```
[SIMSTEP] simulateStep() FIRST CALL!
[WTS] writeToSynthesizer() FIRST CALL!
[INPUT] writeInput() FIRST CALL - engine IS feeding data!
[INPUT] Write #0 data[0]=0
[SYNTH] renderAudio(int) FIRST CALL - synthesizer IS being used!
```
**Then:** Infinite hang, no audio, process doesn't exit

### Test 3: Engine Mode WAV Export ❌
```bash
./engine-sim-cli ../engine-sim-bridge/engine-sim/assets/main.mr --load 0.1 --duration 5 /tmp/test.wav
```
**Result:** Exit code 139 (SEGFAULT) or infinite hang, no WAV file created

## Historical Context

From MEMORY.md (Crackle Fix 2026-02-10):
> **Root cause 3:** Audio buffer pre-filled with 96000 zeros blocked audio thread (target level is 2000)
> **Fixes:** ... reduced pre-fill ...

**The fix was applied to mock but NOT to real Synthesizer!**

This is a classic DRY violation - maintaining two diverging implementations led to bug fixes not being propagated.

## Fix Options

### Option 1: Minimal Fix (Buffer Pre-fill Only)

**File:** `engine-sim-bridge/engine-sim/src/synthesizer.cpp:82-84`

**Change:**
```cpp
const int preFillAmount = std::min(2000, m_audioBufferSize);
for (int i = 0; i < preFillAmount; ++i) {
    m_audioBuffer.write(0);
}
```

**Pros:**
- Simple, one-line fix
- Matches mock's corrected behavior
- May fix playback mode

**Cons:**
- May not fix WAV mode (still calls wrong method)
- Doesn't address architectural issue

### Option 2: Add renderAudioSync() (Complete Fix)

**File:** `engine-sim-bridge/engine-sim/include/synthesizer.h`

Add declaration:
```cpp
void renderAudioSync();
```

**File:** `engine-sim-bridge/engine-sim/src/synthesizer.cpp`

Add implementation (adapted from mock for multi-channel):
```cpp
void Synthesizer::renderAudioSync() {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int available = m_inputChannels[0].data.size();
    if (available <= 0) return;

    const int n = std::min(available, m_inputBufferSize);

    // Read from all input channels
    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.readAndRemove(n, m_inputChannels[i].transferBuffer);
    }

    // Process samples
    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    m_processed = true;
}
```

**File:** `engine-sim-bridge/src/engine_sim_bridge.cpp:527`

Change call:
```cpp
ctx->simulator->synthesizer().renderAudioSync();  // No cv0.wait()
```

**Pros:**
- Complete fix for both modes
- Matches mock's architecture
- Proper separation of sync vs async rendering

**Cons:**
- More invasive change
- Modifies engine-sim submodule

### Option 3: Hacky Workaround (NOT RECOMMENDED)

Change bridge to drain buffer before calling renderAudio():
```cpp
// Drain pre-filled buffer
int16_t drain[96000];
ctx->simulator->readAudioOutput(96000, drain);
// Now call renderAudio()
ctx->simulator->synthesizer().renderAudio();
```

**Why NOT recommended:** Hacky, doesn't fix root cause, wastes cycles.

## Recommended Fix

**Implement Option 2** (Add renderAudioSync()):
1. Fixes both bugs properly
2. Brings real Synthesizer in line with mock
3. Provides proper architecture for sync vs async rendering
4. Future-proof

Then apply buffer pre-fill fix (Option 1) as well for defense-in-depth.

## Impact on Original Issue

**User's "10% throttle dropouts":**
- Cannot reproduce because engine mode doesn't run at all
- Once fixed, need to re-test to see if dropouts actually exist
- Dropouts may have been misdiagnosis of complete failure

**Next Steps After Fix:**
1. Apply fixes
2. Rebuild and test engine mode runs
3. Generate WAV files at 10-15% throttle
4. Use `tools/analyze_crackles.py` to detect anomalies
5. If dropouts still occur, investigate buffer management
6. If no dropouts, report fixed and close issue

## Files Modified for Investigation

- `/Users/danielsinclair/.claude/projects/-Users-danielsinclair-vscode-engine-sim-cli/memory/MEMORY.md`
- `/Users/danielsinclair/vscode/engine-sim-cli/docs/investigation/DROPOUT_INVESTIGATION_2026-02-13.md`
- `/Users/danielsinclair/vscode/engine-sim-cli/docs/investigation/DEADLOCK_ANALYSIS_2026-02-13.md` (this file)

## Files Requiring Changes

### For Option 1 (Minimal):
- `engine-sim-bridge/engine-sim/src/synthesizer.cpp:82-84`

### For Option 2 (Complete):
- `engine-sim-bridge/engine-sim/include/synthesizer.h` (add declaration)
- `engine-sim-bridge/engine-sim/src/synthesizer.cpp` (add renderAudioSync + fix pre-fill)
- `engine-sim-bridge/src/engine_sim_bridge.cpp:527` (change call)

## Conclusion

The investigation successfully identified TWO critical bugs preventing engine mode from functioning:

1. ✅ Buffer pre-fill bug causing cv0.wait() deadlock
2. ✅ Missing renderAudioSync() method for WAV mode

Both fixes exist in mock but were never propagated to real Synthesizer, demonstrating the cost of DRY violations in maintaining parallel implementations.

**All reproduction testing is blocked until these fixes are applied.**
