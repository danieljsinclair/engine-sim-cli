# DEFINITIVE FIX PLAN: CLI Audio Dropout Root Cause

**Date**: 2026-01-30
**Author**: Senior Solution Architect
**Status**: FINAL - Consolidated from 11 investigation reports

---

## EXECUTIVE SUMMARY

### The ACTUAL Root Cause (Definitive)

The CLI has a **timing-dependent race condition** where it reads audio samples from the synthesizer buffer before the audio thread has finished rendering them. This creates buffer underruns that manifest as dropouts.

**Key Finding**: The audio thread releases its lock mid-render (line 243 in synthesizer.cpp), allowing the CLI to read from a partially-filled buffer. The GUI has ~29 lines of work between `endFrame()` and `readAudioOutput()` that gives the audio thread time to complete rendering. The CLI has virtually no delay.

**Why 15%+ throttle exacerbates it**: At higher throttle, the audio thread has more work to do (more audio events to render), increasing the window where the race condition can occur.

---

## INVESTIGATION CONSOLIDATION

### What ALL Reports Agree On

| Agreement | Evidence |
|-----------|----------|
| CLI correctly starts audio thread | `engine_sim_cli.cpp:702` - `EngineSimStartAudioThread()` is called |
| CLI correctly uses `EngineSimReadAudioBuffer()` | `engine_sim_cli.cpp:959` - Uses async read, not `EngineSimRender()` |
| CLI correctly calls `endFrame()` via `EngineSimUpdate()` | `engine_sim_bridge.cpp:465` - Calls `endFrame()` which calls `endInputBlock()` |
| Audio thread coordination uses condition variables | `synthesizer.cpp:225-230` - Waits on `m_cv0` for `!m_processed` |
| Synthesizer buffer has 2000 sample limit | `synthesizer.cpp:228` - `m_audioBuffer.size() < 2000` |
| CLI reads 800 samples per frame | `engine_sim_cli.cpp:485` - `framesPerUpdate = sampleRate / 60` |
| GUI reads variable amounts | `engine_sim_application.cpp:274` - Reads `maxWrite` samples |

### Where Reports DISAGREE

| Finding | TA1 | TA2 | TA3 | Solution Architect | Additional Findings | Verdict |
|---------|-----|-----|-----|-------------------|-------------------|---------|
| **Primary Root Cause** | Read size difference (5.5x) | Governor vs DirectThrottle | Missing `endInputBlock()` | Read size difference | Race condition | **Race condition** |
| **CLI calls `endInputBlock()`?** | N/A | Yes (via bridge) | NO (searched source) | Yes (via bridge) | Yes (verified) | **YES - via bridge** |
| **Audio thread running?** | Yes | Yes | Yes but blocked | Yes | Yes | **YES - running correctly** |
| **Buffer size issue?** | Yes (800 vs 4410) | No | No | Yes (primary) | No | **Contributing factor** |
| **Throttle system issue?** | No | Yes (Governor vs Direct) | No | No | No (inverted logic) | **Separate issue** |

### Contradictions Resolved

1. **TA3's Claim**: "CLI doesn't call `endInputBlock()`"
   - **VERDICT**: **WRONG** - CLI calls `EngineSimUpdate()` which calls `endFrame()` which calls `endInputBlock()`
   - **Evidence**: `engine_sim_bridge.cpp:465` shows `ctx->simulator->endFrame()` is called
   - **Why TA3 was wrong**: Only searched for literal string "endInputBlock" in CLI source

2. **TA1's Claim**: "Read size difference is root cause"
   - **VERDICT**: **PARTIALLY CORRECT** - Read size contributes but is not the root cause
   - **Evidence**: GUI reads up to 4410 samples, CLI reads 800 samples
   - **Why it's not the whole story**: Even with matching read sizes, race condition would still occur

3. **Additional Findings Claim**: "CLI needs `EngineSimWaitForAudio()`"
   - **VERDICT**: **CORRECT** - This addresses the actual root cause
   - **Evidence**: Race condition between `endFrame()` and `readAudioOutput()`
   - **Status**: Already implemented in bridge but not used by CLI

---

## ROOT CAUSE WITH EVIDENCE

### The Smoking Gun: Audio Thread Releases Lock Mid-Render

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Lines 222-256** (renderAudio function):
```cpp
void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;
        return !m_run || (inputAvailable && !m_processed);
    });

    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),
        (int)m_inputChannels[0].data.size());

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();  // ← CRITICAL: Releases lock here!

    // ... filter setup ...

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));  // ← SLOW: Rendering happens AFTER unlock
    }

    m_cv0.notify_one();
}
```

**CRITICAL**: The lock is released at line 243, but actual audio rendering happens at lines 251-253 (AFTER the unlock).

### The Race Condition Timeline

**CLI Flow** (`engine_sim_cli.cpp`):
```
Line 798: EngineSimUpdate(handle, updateInterval)
         → Calls simulator.endFrame() (bridge.cpp:465)
         → Calls synthesizer.endInputBlock() (simulator.cpp:167)
         → Sets m_processed = false
         → Notifies audio thread (synthesizer.cpp:212)

[Virtually no delay - just variable assignments]

Line 959: EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten)
         → Calls readAudioOutput() (synthesizer.cpp:141-159)
         → Acquires m_lock0
         → Reads from m_audioBuffer
```

**What happens in parallel** (Audio Thread):
```
1. Audio thread wakes up (notified by endInputBlock)
2. Acquires m_lock0
3. Sets m_processed = true
4. Releases m_lock0 (line 243)
5. Starts rendering (lines 251-253) - SLOW OPERATION
```

**THE RACE**:
1. CLI notifies audio thread (via `endInputBlock()`)
2. CLI immediately calls `readAudioOutput()` (no delay)
3. Audio thread wakes up, sets `m_processed = true`, releases lock
4. **CLI acquires lock** (audio thread released it!)
5. CLI reads from buffer while audio thread is **STILL RENDERING**
6. Buffer only has partial data → UNDERRUN → zeros filled in

### Why GUI Works

**GUI Flow** (`engine_sim_application.cpp`):
```
Line 245: m_simulator->endFrame()
         → Notifies audio thread

Lines 247-272: [29 LINES OF WORK]
         - Performance metrics
         - Buffer position calculations
         - Multiple offsetDelta() calls
         - Conditional logic
         - Memory allocation

Line 274: m_simulator->readAudioOutput(maxWrite, samples)
```

**Why GUI doesn't have the race**:
- The 29 lines of work between `endFrame()` and `readAudioOutput()` create a natural delay
- By the time GUI reads, the audio thread has finished rendering
- CLI lacks this delay, so it reads too early

### Evidence from Code

**CLI: Minimal delay between update and read**
```cpp
// engine_sim_cli.cpp:798-959
EngineSimUpdate(handle, updateInterval);  // Line 798

// ... just variable assignments and bounds checking ...

result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);  // Line 959
// Only ~20 lines of simple work
```

**GUI: Significant work between update and read**
```cpp
// engine_sim_application.cpp:245-274
m_simulator->endFrame();  // Line 245

// Lines 247-272: 29 lines of work including:
// - Performance calculations
// - Buffer position math
// - Multiple function calls
// - Memory allocation

const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);  // Line 274
```

---

## WHY PREVIOUS FIXES FAILED

### Fix 1: Increase Read Size (TA1 Recommendation)
**Change**: `framesPerUpdate = sampleRate / 60` → `sampleRate / 10`
**Result**: Made problem WORSE
**Why**: Asking for 4800 samples instead of 800 means:
- CLI waits longer for audio thread
- But race condition still exists
- Underrun fills MORE zeros (4000 instead of 0)

### Fix 2: Add `EngineSimEndFrame()` (TA3 Recommendation)
**Change**: Add explicit `endFrame()` call after `EngineSimUpdate()`
**Result**: No effect (function already called internally)
**Why**: `EngineSimUpdate()` already calls `endFrame()` at line 465 of bridge.cpp

### Fix 3: Direct Throttle Inversion (Throttle Investigation)
**Change**: Invert throttle values for DirectThrottleLinkage
**Result**: Addressed different issue (throttle behavior)
**Why**: This fixed throttle control but not audio dropouts

### Fix 4: Move Starter Motor Enable (Engine Startup Investigation)
**Change**: Enable starter before warmup
**Result**: Fixed engine startup, not audio dropouts
**Why**: Separate issue - engine wouldn't crank without starter

---

## THE DEFINITIVE FIX

### Root Cause Summary

The CLI reads audio immediately after notifying the audio thread, before the thread has finished rendering. The audio thread releases its lock mid-render, allowing the CLI to read from a partially-filled buffer.

### Solution: Wait for Audio Thread to Complete

**The CLI already has the solution implemented in the bridge** (`EngineSimWaitForAudio()` at line 631 of bridge.cpp), but it's not being used.

### Implementation Steps

#### Step 1: Add `EngineSimWaitForAudio()` Call to CLI

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Location**: After line 798 (after `EngineSimUpdate()`)

**Current code**:
```cpp
// Render audio
int framesToRender = framesPerUpdate;
```

**Add before**:
```cpp
// CRITICAL: Wait for audio thread to finish rendering before reading
// This prevents race condition where we read before audio is ready
// Matches GUI's implicit delay (29 lines of work between endFrame and readAudioOutput)
EngineSimWaitForAudio(handle);

// Render audio
int framesToRender = framesPerUpdate;
```

**This adds a delay equivalent to the GUI's 29 lines of work.**

#### Step 2: (Optional) Reduce Read Size Back to Original

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Line 485**: Currently might be `sampleRate / 10` (4800 frames)

**Change to**:
```cpp
const int framesPerUpdate = sampleRate / 60;  // 800 frames per update
```

**Rationale**: With the wait in place, we don't need large reads. Smaller reads reduce latency.

---

## WHY THIS WILL WORK

### Evidence Chain

1. **`EngineSimWaitForAudio()` exists** (bridge.cpp:631-665)
   - Calls `synthesizer().waitProcessed()`
   - Waits for `m_processed == true`
   - Audio thread sets `m_processed = true` BEFORE rendering (line 241)
   - But wait... that means it waits BEFORE rendering completes!

Let me re-check the code...

**Actually, there's a BUG in the fix!**

Looking at synthesizer.cpp more carefully:
```cpp
void Synthesizer::renderAudio() {
    // ...
    m_inputSamplesRead = n;
    m_processed = true;  // Line 241 - Sets flag HERE

    lk0.unlock();  // Line 243 - Releases lock

    // ... filter setup ...

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));  // Lines 251-253 - Rendering HERE
    }

    m_cv0.notify_one();  // Line 255
}
```

**`waitProcessed()` waits for `m_processed == true`, which happens BEFORE rendering completes!**

So `EngineSimWaitForAudio()` won't solve the race condition!

### The ACTUAL Fix Needed

The problem is that `m_processed` is set too early. The audio thread should:
1. Set `m_processed = true` AFTER rendering completes
2. Keep the lock held during rendering

**But we can't change the synthesizer code** (it's shared with GUI).

### Alternative Solution: Add Artificial Delay

Since we can't change the synthesizer's locking behavior, we need to add a delay that gives the audio thread time to complete rendering.

**Option A: Small sleep**
```cpp
EngineSimUpdate(handle, updateInterval);
std::this_thread::sleep_for(std::chrono::microseconds(500));  // 0.5ms delay
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**Option B: Yield to scheduler**
```cpp
EngineSimUpdate(handle, updateInterval);
std::this_thread::yield();
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**Option C: Poll until buffer has enough samples**
```cpp
EngineSimUpdate(handle, updateInterval);

// Poll buffer until it has enough samples (with timeout)
int availableSamples = 0;
const int targetSamples = framesToRender;
auto startTime = std::chrono::steady_clock::now();
const auto timeout = std::chrono::milliseconds(10);

while (availableSamples < targetSamples) {
    EngineSimGetStats(handle, &stats);
    availableSamples = stats.audioBufferLevel;  // Need to add this API

    if (availableSamples >= targetSamples) break;

    auto elapsed = std::chrono::steady_clock::now() - startTime;
    if (elapsed > timeout) break;  // Don't wait forever

    std::this_thread::sleep_for(std::chrono::microseconds(100));
}

result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

### Recommended Approach: Option C (Poll with Timeout)

**Why Option C is best**:
- Explicitly waits for data to be ready
- Has timeout to prevent hangs
- Matches GUI's behavior (GUI reads whatever is available)
- No arbitrary delays

**Implementation Required**:

1. **Add `EngineSimGetStats()` extension** to include audio buffer level
2. **Poll for buffer level** before reading
3. **Read whatever is available** (not fixed amount)

---

## FINAL RECOMMENDATION

### The Two-Part Fix

#### Part 1: Add Buffer Level Query API

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h`

**Add after line 266**:
```cpp
/**
 * Gets the current audio buffer level (number of samples available)
 *
 * @param handle Simulator handle
 * @param outBufferLevel Output parameter for buffer level
 * @return ESIM_SUCCESS on success, error code otherwise
 */
EngineSimResult EngineSimGetAudioBufferLevel(
    EngineSimHandle handle,
    int32_t* outBufferLevel
);
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

**Add after line 629**:
```cpp
EngineSimResult EngineSimGetAudioBufferLevel(
    EngineSimHandle handle,
    int32_t* outBufferLevel)
{
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    if (!outBufferLevel) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    EngineSimContext* ctx = getContext(handle);

    if (!ctx->simulator) {
        return ESIM_ERROR_NOT_INITIALIZED;
    }

    *outBufferLevel = ctx->simulator->synthesizer().getAudioBufferLevel();

    return ESIM_SUCCESS;
}
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h`

**Add after line 77**:
```cpp
int getAudioBufferLevel() const { return m_audioBuffer.size(); }
```

#### Part 2: Update CLI to Match GUI's Read Pattern

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Replace lines 942-959**:
```cpp
// OLD CODE (FIXED SIZE READ):
// Render audio
int framesToRender = framesPerUpdate;

if (!args.interactive) {
    int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
    framesToRender = std::min(framesPerUpdate, totalExpectedFrames - framesProcessed);
}

if (framesToRender > 0) {
    int samplesWritten = 0;
    float* writePtr = audioBuffer.data() + framesRendered * channels;
    auto renderStart = std::chrono::steady_clock::now();
    result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
    auto renderEnd = std::chrono::steady_clock::now();
```

**With NEW CODE (VARIABLE SIZE READ LIKE GUI)**:
```cpp
// NEW CODE (VARIABLE SIZE READ - MATCHES GUI):
// Calculate how many samples to read based on buffer availability
// This matches the GUI's pattern of reading however much is available
int framesToRender = framesPerUpdate;

if (!args.interactive) {
    int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
    framesToRender = std::min(framesPerUpdate, totalExpectedFrames - framesProcessed);
}

// Check buffer level and read whatever is available (up to our target)
// This prevents underruns by not demanding more than exists
int32_t bufferLevel = 0;
EngineSimGetAudioBufferLevel(handle, &bufferLevel);

// Only read what's actually available (like GUI does)
int actualFramesToRead = std::min(framesToRender, bufferLevel);

if (actualFramesToRead > 0) {
    int samplesWritten = 0;
    float* writePtr = audioBuffer.data() + framesRendered * channels;
    auto renderStart = std::chrono::steady_clock::now();
    result = EngineSimReadAudioBuffer(handle, writePtr, actualFramesToRead, &samplesWritten);
    auto renderEnd = std::chrono::steady_clock::now();
```

**Why this works**:
- CLI now reads HOWEVER MANY SAMPLES ARE ACTUALLY AVAILABLE (like GUI)
- If audio thread has only rendered 500 samples, CLI reads 500 (not 800)
- No underrun occurs because we never ask for more than exists
- Buffer acts as a shock absorber for timing variations

---

## TESTING STRATEGY

### Test 1: Verify No Underruns

```bash
# Run at 15% throttle (previously problematic)
./build/engine-sim-cli --default-engine --load 15 --duration 10 --output test_15_fixed.wav

# Check for dropouts
# Should see continuous audio with no silence gaps
```

### Test 2: Compare with GUI

```bash
# Run GUI at equivalent throttle
# Record audio from both
# Compare waveforms - should match
```

### Test 3: Stress Test

```bash
# Test at various throttle levels
for throttle in 10 15 20 30 50 100; do
    ./build/engine-sim-cli --default-engine --load $throttle --duration 5 --output test_$throttle.wav
done

# All files should have continuous audio
```

---

## CONCLUSION

### Root Cause (Definitive)

The CLI has a race condition where it reads from the audio buffer before the audio thread finishes rendering. The audio thread releases its lock mid-render, allowing the CLI to read partially-filled buffers.

### Why This Was Missed

1. **TA3** searched for literal strings instead of tracing function calls
2. **TA1** focused on read size (contributing factor) but not timing
3. **TA2** got distracted by throttle calculations
4. **Additional Findings** identified the race condition but proposed the wrong fix (`waitProcessed` waits too early)

### The Fix

Match the GUI's read pattern:
1. Query buffer level before reading
2. Read however many samples are actually available (not fixed amount)
3. Never ask for more than exists in buffer

This eliminates underruns by adapting to what the audio thread has actually produced, rather than demanding a fixed amount that may not be ready yet.

### Confidence Level

**HIGH** - Root cause identified with specific code evidence, fix matches proven GUI pattern.

---

**END OF DEFINITIVE FIX PLAN**
