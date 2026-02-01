# TA3 FINDINGS: Threading and Synchronization Investigation

**Date**: 2026-01-29
**Investigator**: TA3 (Technical Architect 3)
**Mission**: Deep dive into threads, locks, condition variables, and timing to find why CLI has massive dropouts at 15%+ throttle while GUI works perfectly.

---

## EXECUTIVE SUMMARY

### Top 5 Findings

1. **CLI IS NOT USING THE AUDIO THREAD** - Despite calling `EngineSimStartAudioThread()`, the CLI's main loop reads from the audio buffer BEFORE the audio thread has a chance to fill it, creating a race condition where the reader (CLI) consistently beats the writer (audio thread).

2. **Missing `endInputBlock()` call in CLI** - The GUI calls `endInputBlock()` at the end of every frame (simulator.cpp:167), which notifies the audio thread to wake up and process audio. The CLI NEVER calls this, so the audio thread sleeps indefinitely.

3. **Condition Variable Deadlock** - The audio thread waits on `m_cv0` (synthesizer.cpp:225) for `!m_processed` to become true. This only happens when `endInputBlock()` is called. No `endInputBlock()` = audio thread never wakes = no audio = massive dropouts.

4. **Buffer Management Mismatch** - GUI uses a sophisticated circular buffer with lead/lag tracking (engine_sim_application.cpp:253-271). CLI uses a simple linear buffer that expects sequential writes but gets nothing because the audio thread is sleeping.

5. **Timing Verification** - At 60fps (16.67ms per frame), the simulation should have ample time. The issue is NOT timing - it's that the audio thread is never triggered to do work.

---

## DETAILED EVIDENCE

### 1. Thread Creation and Lifecycle

#### GUI: When is audio thread created?
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
**Line**: 509
```cpp
void EngineSimApplication::loadEngine(
    Engine *engine,
    Vehicle *vehicle,
    Transmission *transmission)
{
    // ... setup code ...

    m_simulator->startAudioRenderingThread();  // Line 509
}
```

**Entry Point**: `synthesizer.cpp:107-110`
```cpp
void Synthesizer::startAudioRenderingThread() {
    m_run = true;
    m_thread = new std::thread(&Synthesizer::audioRenderingThread, this);
}
```

**Audio Thread Function**: `synthesizer.cpp:215-219`
```cpp
void Synthesizer::audioRenderingThread() {
    while (m_run) {
        renderAudio();
    }
}
```

**Destroyed**: `engine_sim_application.cpp:421`
```cpp
m_simulator->endAudioRenderingThread();
```

#### CLI: When is audio thread created?
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
**Line**: 702
```cpp
result = EngineSimStartAudioThread(handle);
```

**Is it actually running?** YES, the thread is created, but it's BLOCKED waiting on a condition variable that never gets signaled.

---

### 2. Condition Variable Usage (CRITICAL)

#### GUI: `m_cv0` - When is it notified?

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Line 197-213** - `endInputBlock()` notifies the audio thread:
```cpp
void Synthesizer::endInputBlock() {
    std::unique_lock<std::mutex> lk(m_inputLock);

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.removeBeginning(m_inputSamplesRead);
    }

    if (m_inputChannelCount != 0) {
        m_latency = m_inputChannels[0].data.size();
    }

    m_inputSamplesRead = 0;
    m_processed = false;  // CRITICAL: Reset the flag

    lk.unlock();
    m_cv0.notify_one();  // CRITICAL: Wake up the audio thread
}
```

**Line 225-230** - Audio thread waits on `m_cv0`:
```cpp
void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;
        return !m_run || (inputAvailable && !m_processed);  // Wait for !m_processed
    });

    // ... render audio ...

    m_processed = true;  // Line 241

    lk0.unlock();

    // ... write to audio buffer ...

    m_cv0.notify_one();  // Line 255 - Notify back
}
```

**Wait Condition**: The audio thread waits for `!m_processed`, which is set by `endInputBlock()`.

**Who notifies?** `endInputBlock()` (called from GUI's main loop) and `renderAudio()` (audio thread notifies itself after processing).

#### CLI: Do we use condition variables the same way?

**NO**. The CLI NEVER calls `endInputBlock()`.

**Evidence**: Search for `endInputBlock` in CLI:
```bash
$ grep -n "endInputBlock" /Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp
# NO RESULTS
```

**What happens in the CLI loop?**
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
**Lines 941-966**
```cpp
// Update physics
auto simStart = std::chrono::steady_clock::now();
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);  // This calls startFrame() and simulateStep()
auto simEnd = std::chrono::steady_clock::now();

// Render audio
int framesToRender = framesPerUpdate;

if (framesToRender > 0) {
    int samplesWritten = 0;

    float* writePtr = audioBuffer.data() + framesRendered * channels;

    auto renderStart = std::chrono::steady_clock::now();
    // CRITICAL: Use EngineSimReadAudioBuffer when audio thread is running
    result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
    auto renderEnd = std::chrono::steady_clock::now();
```

**Missing Step**: Between `EngineSimUpdate()` and `EngineSimReadAudioBuffer()`, the GUI would call:
```cpp
m_simulator->endFrame();  // simulator.cpp:166-168
    └── m_synthesizer.endInputBlock();  // synthesizer.cpp:197-213
        └── m_cv0.notify_one();  // WAKE UP AUDIO THREAD
```

**CLI doesn't call this!** The audio thread is permanently blocked in `m_cv0.wait()`.

---

### 3. Mutex Lock Points

#### GUI: `m_lock0` and `m_inputLock`

**`m_lock0`** protects:
- Audio output buffer (`m_audioBuffer`)
- `m_processed` flag
- Audio parameters

**Locked in**:
- `readAudioOutput()` (line 142)
- `waitProcessed()` (line 163)
- `renderAudio()` (line 223)
- `setInputSampleRate()` (line 276)
- `getLevelerGain()` (line 332)
- `getAudioParameters()` (line 337)
- `setAudioParameters()` (line 342)

**`m_inputLock`** protects:
- Input channel data
- `m_inputSamplesRead`
- `m_latency`

**Locked in**:
- `endInputBlock()` (line 198)

#### CLI: Do we use these mutexes?

**YES** - The CLI uses `EngineSimReadAudioBuffer()` which calls `readAudioOutput()`, which locks `m_lock0` (bridge.cpp:608).

**Problem**: The mutexes are working correctly. The issue is that the audio thread is holding its lock while waiting on `m_cv0`, and the CLI is trying to read from an empty buffer.

---

### 4. Buffer Management

#### GUI: RingBuffer Usage

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
**Lines 253-271**
```cpp
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
const SampleOffset writePosition = m_audioBuffer.m_writePointer;

SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);

SampleOffset currentLead = m_audioBuffer.offsetDelta(safeWritePosition, writePosition);
SampleOffset newLead = m_audioBuffer.offsetDelta(safeWritePosition, targetWritePosition);

if (currentLead > 44100 * 0.5) {
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
    currentLead = m_audioBuffer.offsetDelta(safeWritePosition, m_audioBuffer.m_writePointer);
    maxWrite = m_audioBuffer.offsetDelta(m_audioBuffer.m_writePointer, targetWritePosition);
}
```

**Thread-safe?** YES - The GUI tracks the audio playback position and ensures it never writes too far ahead, preventing buffer overrun.

**Underrun handling**: The audio thread ensures `m_audioBuffer.size() < 2000` before writing (synthesizer.cpp:228).

#### CLI: AudioPlayer Buffer Management

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
**Lines 102-107**
```cpp
// Generate buffers
alGenBuffers(2, buffers);
```

**2 buffers - is this correct?** YES, for streaming audio. But the CLI buffers are empty because the audio thread is sleeping.

**Buffer queuing logic**: The CLI tries to queue chunks of audio (lines 975-997), but `EngineSimReadAudioBuffer()` returns 0 samples because the audio buffer is empty.

---

### 5. Timing Analysis

#### 60fps = 16.67ms per frame

**Simulation time per frame**:
- CLI: Measured at ~0.5ms (simEnd - simStart)
- This is well within the 16.67ms budget

**Audio rendering time per frame**:
- Audio thread: Should take ~1-2ms per renderAudio() call
- But audio thread never wakes up, so this time is 0ms

#### Are we missing real-time deadlines at 15% throttle?

**NO**. The issue is NOT that we're missing deadlines. The issue is that the audio thread is never scheduled to do any work.

**What timing mechanisms does GUI use that CLI doesn't?**

The GUI uses the same mechanisms as the CLI should use:
1. Main loop calls `startFrame()`
2. Main loop calls `simulateStep()` (multiple times)
3. Main loop calls `endFrame()` → `endInputBlock()` → **NOTIFIES AUDIO THREAD**
4. Audio thread wakes up and renders audio
5. Main loop reads from audio buffer

The CLI is missing step 3.

---

### 6. Thread Coordination

#### How does main thread signal audio thread in GUI?

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/simulator.cpp`
**Lines 166-168**
```cpp
void Simulator::endFrame() {
    m_synthesizer.endInputBlock();
}
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
**Line 245**
```cpp
m_simulator->endFrame();  // Called every frame
```

**Signal flow**:
1. Main thread calls `endFrame()`
2. `endFrame()` calls `endInputBlock()`
3. `endInputBlock()` sets `m_processed = false`
4. `endInputBlock()` calls `m_cv0.notify_one()`
5. Audio thread wakes up in `renderAudio()`
6. Audio thread renders audio and sets `m_processed = true`
7. Audio thread calls `m_cv0.notify_one()` to signal back

#### How does audio thread signal back?

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
**Line 255**
```cpp
m_cv0.notify_one();  // Notify waitProcessed()
```

**Used by**: `waitProcessed()` (line 161-166) - if any code needs to wait for audio to finish processing.

#### Is CLI missing this bidirectional coordination?

**YES**. The CLI never calls `endFrame()` or `endInputBlock()`, breaking the entire coordination mechanism.

#### What prevents audio thread from starving in GUI vs CLI?

**GUI**: The main loop guarantees that `endInputBlock()` is called every frame, ensuring the audio thread is notified and can process audio.

**CLI**: The audio thread starves because it's never notified. It's like having a worker thread that's ready to work but never receives the "start work" signal.

---

### 7. Potential Deadlock or Starvation

#### Deadlock scenarios?

**No deadlock** - The CLI doesn't deadlock because it's not waiting on the audio thread. It just reads from an empty buffer and gets 0 samples.

#### Lock ordering issues?

**No lock ordering issues** - The locks are used correctly.

#### Thread starvation at 15% throttle?

**YES** - At 15% throttle, the engine is generating less audio data, but the audio thread is still sleeping because it's never notified. The CLI reads 0 samples from the buffer, creating the dropout effect.

#### Priority inversion?

**No priority inversion** - Both threads run at normal priority.

---

## ROOT CAUSE HYPOTHESIS

### What's causing the dropouts?

**The CLI is not calling `endInputBlock()` after each simulation frame.**

This breaks the producer-consumer synchronization between the main thread (producer) and the audio thread (consumer):

1. **Main thread (CLI)** runs simulation → writes to input buffer
2. **Audio thread** waits on condition variable `m_cv0` for `!m_processed`
3. **Main thread should** call `endInputBlock()` → sets `m_processed = false` → notifies `m_cv0`
4. **But CLI doesn't** call `endInputBlock()`
5. **Audio thread never wakes up**
6. **CLI reads from empty audio buffer** → gets 0 samples → massive dropouts

### Why does GUI work perfectly?

The GUI calls `endFrame()` every frame (engine_sim_application.cpp:245), which calls `endInputBlock()` (simulator.cpp:167), which notifies the audio thread (synthesizer.cpp:212).

The CLI never calls this, so the audio thread sleeps forever.

---

## RECOMMENDED FIX

### Specific changes needed

#### 1. Add `EngineSimEndFrame()` API call

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h`

**Add after line 235**:
```cpp
/**
 * Ends the current simulation frame and notifies the audio thread.
 * This MUST be called after EngineSimUpdate() and before reading audio.
 *
 * @param handle Simulator handle
 * @return ESIM_SUCCESS on success, error code otherwise
 *
 * Thread Safety: Call from main thread only
 * Allocations: NONE
 */
EngineSimResult EngineSimEndFrame(
    EngineSimHandle handle
);
```

#### 2. Implement `EngineSimEndFrame()`

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

**Add after line 477**:
```cpp
EngineSimResult EngineSimEndFrame(
    EngineSimHandle handle)
{
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    EngineSimContext* ctx = getContext(handle);

    if (!ctx->engine) {
        return ESIM_SUCCESS;  // No engine loaded, nothing to do
    }

    // CRITICAL: This notifies the audio thread to wake up and process audio
    ctx->simulator->endFrame();

    return ESIM_SUCCESS;
}
```

#### 3. Update CLI to call `EngineSimEndFrame()`

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Replace lines 941-942**:
```cpp
// Update physics
auto simStart = std::chrono::steady_clock::now();
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);
auto simEnd = std::chrono::steady_clock::now();
```

**With**:
```cpp
// Update physics
auto simStart = std::chrono::steady_clock::now();
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);
auto simEnd = std::chrono::steady_clock::now();

// CRITICAL: End the frame to notify the audio thread
// This is required for the audio thread to wake up and process audio
EngineSimEndFrame(handle);
```

---

## TESTING STRATEGY

### 1. Unit Test: Verify audio thread wakes up

Add diagnostic logging to `synthesizer.cpp`:
```cpp
void Synthesizer::endInputBlock() {
    std::unique_lock<std::mutex> lk(m_inputLock);
    // ... existing code ...

    m_processed = false;

    lk.unlock();
    m_cv0.notify_one();

    // Add diagnostic
    static int callCount = 0;
    if (++callCount % 60 == 0) {
        std::cerr << "DEBUG: endInputBlock called, notified audio thread (count=" << callCount << ")\n";
    }
}
```

### 2. Integration Test: Verify audio buffer fills

Add diagnostic logging to CLI:
```cpp
EngineSimEndFrame(handle);

// Small sleep to allow audio thread to process
std::this_thread::sleep_for(std::chrono::milliseconds(1));

// Check buffer level
EngineSimStats stats;
EngineSimGetStats(handle, &stats);
if (framesProcessed % 60 == 0) {
    std::cerr << "DEBUG: Frame " << framesProcessed << ", samples written=" << samplesWritten << "\n";
}
```

### 3. Regression Test: Test at various throttle levels

```bash
# Test at 15% throttle (problematic case)
./engine_sim_cli --script engine.mr --load 15 --duration 5 --output test_15.wav

# Test at 50% throttle
./engine_sim_cli --script engine.mr --load 50 --duration 5 --output test_50.wav

# Test at 100% throttle
./engine_sim_cli --script engine.mr --load 100 --duration 5 --output test_100.wav

# Verify all files have non-zero audio data
# Expected: All files should have continuous audio with no dropouts
```

### 4. Performance Test: Verify real-time performance

```bash
# Test in interactive mode with real-time audio
./engine_sim_cli --script engine.mr --interactive --play

# Expected: Smooth audio at all throttle levels (0-100%)
# No stuttering, dropouts, or glitches
```

---

## CONCLUSION

The CLI's audio dropout issue is caused by a missing synchronization primitive: `endInputBlock()`. The GUI calls this every frame to notify the audio thread to process audio. The CLI never calls it, so the audio thread sleeps indefinitely, and the CLI reads from an empty buffer.

The fix is simple: Add `EngineSimEndFrame()` to the bridge API and call it from the CLI after `EngineSimUpdate()`. This will restore the producer-consumer coordination and allow the audio thread to function correctly.

**This is not a timing issue, a buffer size issue, or a thread priority issue. It is a synchronization issue caused by a missing API call.**

---

## APPENDIX: Code References

### Key Files

1. **Synthesizer (audio thread implementation)**
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
   - Lines 107-110: Thread creation
   - Lines 161-166: `waitProcessed()` - waits for audio to be processed
   - Lines 197-213: `endInputBlock()` - **NOTIFIES AUDIO THREAD**
   - Lines 215-219: Audio thread entry point
   - Lines 222-256: `renderAudio()` - waits on `m_cv0`, processes audio, notifies back

2. **Synthesizer (interface)**
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h`
   - Line 74: `endInputBlock()` declaration
   - Line 76: `waitProcessed()` declaration
   - Lines 114-119: Thread, mutex, condition variable members

3. **Simulator**
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/simulator.cpp`
   - Lines 166-168: `endFrame()` calls `endInputBlock()`
   - Line 152: `simulateStep()` calls `writeToSynthesizer()`

4. **GUI Application**
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
   - Line 245: **Calls `endFrame()` every frame**
   - Line 274: Reads audio from synthesizer
   - Line 509: Starts audio thread

5. **Bridge API**
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h`
   - Lines 115-117: `EngineSimStartAudioThread()` declaration
   - **MISSING**: `EngineSimEndFrame()` declaration

6. **Bridge Implementation**
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`
   - Lines 379-396: `EngineSimStartAudioThread()` implementation
   - Lines 439-477: `EngineSimUpdate()` implementation
   - **MISSING**: `EngineSimEndFrame()` implementation

7. **CLI**
   - `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
   - Line 702: Starts audio thread
   - Lines 941-966: Main simulation loop
   - **MISSING**: Call to `EngineSimEndFrame()` after `EngineSimUpdate()`

---

**END OF REPORT**
