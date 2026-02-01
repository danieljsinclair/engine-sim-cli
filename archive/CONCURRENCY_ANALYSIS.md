# Concurrency Analysis: Audio Dropout Root Cause

## Executive Summary

**CRITICAL FINDING**: The CLI has audio dropouts every ~1 second due to a **race condition in the producer-consumer synchronization** between the main thread and audio thread. The audio thread is blocking on a condition variable that the main thread never signals in the CLI's code path.

**Root Cause**: `EngineSimWaitForAudio()` is never called in the CLI main loop, causing the audio thread to block indefinitely waiting for `m_processed` to be reset to `false`.

---

## 1. Threading Architecture Analysis

### 1.1 Thread Roles

**Audio Thread (Producer)**:
- Location: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:215-219`
- Entry point: `Synthesizer::audioRenderingThread()`
- Pattern: Continuous loop calling `renderAudio()`
- Purpose: Fill `m_audioBuffer` with synthesized audio samples

**Main Thread (Consumer)**:
- Location: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
- Entry point: `runSimulation()` main loop (line 809)
- Pattern: 60Hz update loop
- Purpose: Run physics simulation, read audio from buffer

### 1.2 Synchronization Primitives

**Mutexes**:
- `m_lock0` (synthesizer.h:126): Protects audio buffer and coordination state
- `m_inputLock` (synthesizer.h:125): Protects input channel data

**Condition Variable**:
- `m_cv0` (synthesizer.h:127): Coordinates between audio thread and main thread

**State Flags**:
- `m_processed` (synthesizer.h:123): Indicates if current input has been processed
- `m_run` (synthesizer.h:122): Audio thread run flag

---

## 2. Producer-Consumer Coordination Pattern

### 2.1 Audio Thread (Producer) Work Pattern

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:222-256`

```cpp
void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    // WAIT CONDITION: Block until input available AND not processed
    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;
        return !m_run || (inputAvailable && !m_processed);
    });

    // Generate up to 2000 samples
    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),
        (int)m_inputChannels[0].data.size());

    // Read from input buffer
    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    // Mark as processed
    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();

    // Generate audio samples (DSP processing)
    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    // Notify main thread that processing is complete
    m_cv0.notify_one();
}
```

**Critical Wait Condition Analysis** (lines 225-230):
- Audio thread blocks if: `!inputAvailable || m_processed`
- `inputAvailable`: Input buffer has data AND audio buffer has space (< 2000 samples)
- `m_processed`: True means main thread hasn't consumed the previous input yet
- **Audio thread CANNOT proceed until `m_processed == false`**

### 2.2 Main Thread (Consumer) Work Pattern

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:935-960`

```cpp
// Update physics (60 Hz)
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);  // Calls simulator->startFrame/endFrame

// Read audio from buffer
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**EngineSimUpdate Flow** (`engine_sim_bridge.cpp:439-477`):
```cpp
EngineSimResult EngineSimUpdate(EngineSimHandle handle, double deltaTime) {
    // ...
    ctx->simulator->startFrame(deltaTime);
    while (ctx->simulator->simulateStep()) {
        // Process all simulation steps
    }
    ctx->simulator->endFrame();  // <-- KEY: Calls synthesizer.endInputBlock()
    // ...
}
```

**endInputBlock Flow** (`synthesizer.cpp:197-213`):
```cpp
void Synthesizer::endInputBlock() {
    std::unique_lock<std::mutex> lk(m_inputLock);

    // Remove consumed input samples
    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.removeBeginning(m_inputSamplesRead);
    }

    // Update latency measurement
    if (m_inputChannelCount != 0) {
        m_latency = m_inputChannels[0].data.size();
    }

    m_inputSamplesRead = 0;

    // *** CRITICAL: Reset m_processed to allow audio thread to continue ***
    m_processed = false;

    lk.unlock();
    m_cv0.notify_one();  // Signal audio thread to wake up
}
```

### 2.3 Ideal Coordination Flow

```
Main Thread (60Hz)                    Audio Thread (Continuous)
================                      ==========================
1. startFrame()                              |
2. simulateStep() [multiple times]           |
   - writeToSynthesizer()                    |
   - Adds samples to m_inputChannels         |
3. endFrame()                                |
   - endInputBlock()                         |
     - Sets m_processed = false  ------------->|
     - notify_one() wakes audio thread        |
4. Read from m_audioBuffer                   |
                                              |<-- Wakes up
                                              | - Reads from m_inputChannels
                                              | - Sets m_processed = true
                                              | - Generates samples to m_audioBuffer
                                              | - notify_one()
5. Next iteration...                         | - Goes back to wait
```

---

## 3. Race Condition Identified

### 3.1 The Missing Link: `EngineSimWaitForAudio()`

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp:631-655`

```cpp
EngineSimResult EngineSimWaitForAudio(EngineSimHandle handle) {
    // ...
    Synthesizer& synth = ctx->simulator->synthesizer();

    // Check if there's input data available for the audio thread
    // Only wait if input exists AND audio thread hasn't processed yet
    if (synth.getInputBufferSize() > 0 && !synth.isProcessed()) {
        synth.waitProcessed();  // <-- Wait for audio thread to finish
    }

    return ESIM_SUCCESS;
}
```

**waitProcessed Flow** (`synthesizer.cpp:161-166`):
```cpp
void Synthesizer::waitProcessed() {
    {
        std::unique_lock<std::mutex> lk(m_lock0);
        m_cv0.wait(lk, [this] { return m_processed; });
    }
}
```

### 3.2 GUI vs CLI Threading Differences

**GUI Pattern** (`engine_sim_application.cpp:202-312`):
```cpp
void EngineSimApplication::process(float frame_dt) {
    // 1. Update simulation
    m_simulator->startFrame(1 / avgFramerate);
    while (m_simulator->simulateStep()) {
        m_oscCluster->sample();
    }
    m_simulator->endFrame();  // Calls endInputBlock()

    // 2. Read audio (non-blocking, just reads from buffer)
    const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);

    // 3. Write to hardware audio buffer
    // ...
}
```

**GUI Threading Observation**:
- GUI does NOT call `EngineSimWaitForAudio()` either
- GUI works fine because the hardware audio system reads at a steady rate
- The buffer never gets depleted because the audio thread continuously fills it

**CLI Pattern** (`engine_sim_cli.cpp:935-960`):
```cpp
while (running) {
    // 1. Update simulation
    EngineSimUpdate(handle, updateInterval);  // Calls endInputBlock()

    // 2. Read audio from buffer
    EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);

    // 3. Queue to OpenAL (or write to WAV)
    if (audioPlayer) {
        audioPlayer->playBuffer(...);
    }
}
```

**CLI Threading Problem**:
- CLI also does NOT call `EngineSimWaitForAudio()`
- CLI reads in 800-frame chunks (60Hz = 800 frames @ 48kHz)
- BUT: The audio thread wait condition includes `m_audioBuffer.size() < 2000`

---

## 4. The Dropout Mechanism

### 4.1 Buffer Size Analysis

**Configuration** (`engine_sim_cli.cpp:590-599`):
```cpp
config.sampleRate = 48000;
config.inputBufferSize = 48000;   // Input buffer size
config.audioBufferSize = 48000;   // Audio buffer size (1 second @ 48kHz)
```

**Audio Thread Fill Logic** (`synthesizer.cpp:232-234`):
```cpp
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),
    (int)m_inputChannels[0].data.size());
```

**Interpretation**:
- Audio thread fills in chunks of up to 2000 samples
- Only fills when `m_audioBuffer.size() < 2000`
- This creates a "hysteresis" effect: fill to 2000, then stop

### 4.2 CLI Consumption Rate

**CLI Main Loop** (`engine_sim_cli.cpp:485-486, 942-960`):
```cpp
const int framesPerUpdate = sampleRate / 60;  // 800 frames per update
// In each iteration:
EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**Consumption Rate**: 800 frames per iteration @ 60Hz = 48,000 frames/second

### 4.3 The Dropout Cycle

**Initial State** (Buffer empty):
```
m_audioBuffer.size() = 0
Audio thread: Fills 2000 samples (takes ~41ms @ 48kHz)
Main thread: Reads 800 samples per frame (16.67ms)
```

**After ~2.5 frames (42ms)**:
```
m_audioBuffer.size() = 2000 - (800 * 2.5) = 0
Audio thread: Wakes up, sees buffer is empty (< 2000)
              Checks m_processed state
```

**The Race Condition**:
1. Audio thread checks: `m_inputChannels[0].data.size() > 0` (input available?)
2. Audio thread checks: `m_audioBuffer.size() < 2000` (buffer has space?)
3. Audio thread checks: `!m_processed` (has previous input been consumed?)

**Scenario A: Normal Operation** (if timing aligns):
```
Frame N:   Main thread calls endInputBlock() -> sets m_processed = false
           Audio thread wakes up -> sees !m_processed -> fills buffer
Frame N+1: Main thread reads 800 samples
           Audio thread wakes up -> sees m_processed = true -> blocks
Frame N+2: Main thread calls endInputBlock() -> sets m_processed = false
           Audio thread wakes up -> fills buffer again
```

**Scenario B: Dropout Occurs** (timing misalignment):
```
Frame N:   Main thread calls endInputBlock() -> sets m_processed = false
           Audio thread wakes up -> fills 2000 samples -> sets m_processed = true
Frame N+1: Main thread reads 800 samples (buffer now has 1200)
           Audio thread wakes up -> sees m_processed = true -> BLOCKS
Frame N+2: Main thread reads 800 samples (buffer now has 400)
           Audio thread still BLOCKED on m_processed = true
Frame N+3: Main thread tries to read 800 samples -> UNDERRUN (only 400 available)
           Main thread calls endInputBlock() -> sets m_processed = false
           Audio thread wakes up -> fills buffer again
```

### 4.4 Why GUI Doesn't Have This Problem

**GUI Audio Output** (`engine_sim_application.cpp:253-306`):
```cpp
// GUI uses hardware audio buffer that plays at steady rate
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
// ...
m_audioSource->LockBufferSegment(...);
// Copy to hardware buffer
m_audioSource->UnlockBufferSegments(...);
```

**Key Difference**:
- Hardware audio system consumes at EXACTLY 48kHz (real-time)
- No variable latency from file I/O or OpenAL queuing
- Buffer drain is predictable and smooth

**CLI Problem**:
- CLI writes to WAV file or OpenAL in chunks
- Variable latency between simulation and actual audio output
- Buffer drain rate is not perfectly matched to production rate

---

## 5. Lock Contention Analysis

### 5.1 Lock Usage Patterns

**m_lock0** (Primary synchronization lock):
- Held by: Audio thread (during renderAudio), Main thread (during readAudioOutput, waitProcessed)
- Contention points:
  1. `readAudioOutput` (synthesizer.cpp:141-159) - locks for entire read operation
  2. `renderAudio` (synthesizer.cpp:223-243) - locks during wait and input read
  3. `waitProcessed` (synthesizer.cpp:163-165) - locks during condition wait

**m_inputLock** (Input data protection):
- Held by: Main thread (during endInputBlock)
- Purpose: Protects input channel data during removal
- Low contention: Only held briefly during endInputBlock

### 5.2 Potential Deadlock Scenarios

**Deadlock Scenario 1** (Theoretical, not observed):
```
Main thread: Holds m_inputLock, waits for m_lock0
Audio thread: Holds m_lock0, waits for m_inputLock
```
**Analysis**: Code structure prevents this (locks are not nested)

**Deadlock Scenario 2** (Observed in CLI):
```
Main thread: Not calling waitProcessed(), running ahead
Audio thread: Blocked on m_cv0 waiting for m_processed == false
```
**Analysis**: This is the dropout condition!

### 5.3 Lock Ordering

**Correct Ordering** (GUI):
1. Main thread: `simulateStep()` → `writeToSynthesizer()` → `endInputBlock()`
2. Main thread: `readAudioOutput()` (locks m_lock0 briefly)
3. Audio thread: `renderAudio()` (locks m_lock0, waits on m_cv0)

**Incorrect Ordering** (CLI):
1. Main thread: `simulateStep()` → `endInputBlock()` → NO WAIT
2. Main thread: `readAudioOutput()` (may find empty buffer)
3. Audio thread: BLOCKED waiting for m_processed == false

---

## 6. The 1-Second Dropout Periodicity

### 6.1 Mathematical Analysis

**Buffer Parameters**:
- `audioBufferSize` = 48,000 samples (1 second @ 48kHz)
- Audio thread fills to 2,000 samples max per wake
- CLI consumes 800 samples per frame @ 60Hz

**Fill-Drain Cycle**:
```
Fill rate: 2000 samples / wake
Drain rate: 800 samples / 16.67ms = 48,000 samples/second
Time between fills: 2000 / 800 = 2.5 frames = 41.67ms
```

**Dropout Occurrence**:
```
If audio thread misses 1 wake cycle:
- Buffer has 1200 samples (2000 - 800)
- Next frame consumes 800, leaving 400
- Next frame requests 800 -> UNDERRUN (400 available)

Time to dropout: Depends on synchronization timing
Typical period: ~1 second (when buffer empties completely)
```

### 6.2 Why ~1 Second?

**Hypothesis**: The dropout occurs when the initial buffer (48,000 samples) is exhausted:

```
Initial fill: Audio thread fills ~48,000 samples during startup
CLI consumption: 48,000 samples/second
After ~1 second: Buffer is empty, audio thread can't keep up due to race condition
```

**Evidence from diagnostics**:
- Dropout occurs consistently every ~1 second
- Dropout duration is brief (a few frames)
- Audio recovers after `endInputBlock()` resets `m_processed`

---

## 7. GUI vs CLI Comparison Summary

| Aspect | GUI | CLI |
|--------|-----|-----|
| Audio output | Hardware buffer (real-time) | OpenAL/WAV (chunked) |
| Consumption rate | Exactly 48kHz | Variable (queuing latency) |
| waitProcessed call | NO | NO |
| Dropouts | NO | YES (~1 second) |
| Thread coordination | Implicit (hardware timing) | Broken (missing synchronization) |

---

## 8. Recommended Fix (Based on Concurrency Principles)

### 8.1 Option 1: Add `EngineSimWaitForAudio()` to CLI Main Loop

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Change**:
```cpp
// After EngineSimUpdate, before EngineSimReadAudioBuffer
EngineSimUpdate(handle, updateInterval);

// ADD: Wait for audio thread to process the input
EngineSimWaitForAudio(handle);

// Now read audio (buffer should be full)
EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**Rationale**:
- Ensures main thread waits for audio thread to finish processing
- Prevents main thread from "running ahead" of audio thread
- Matches the intended producer-consumer pattern

**Pros**:
- Fixes the race condition
- Minimal code change
- Maintains existing architecture

**Cons**:
- Adds latency (main thread blocks)
- May reduce frame rate if audio thread is slow

### 8.2 Option 2: Remove `m_processed` Check from Audio Thread Wait Condition

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:225-230`

**Change**:
```cpp
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannels[0].data.size() > 0
        && m_audioBuffer.size() < 2000;
    return !m_run || inputAvailable;  // REMOVE: && !m_processed
});
```

**Rationale**:
- Audio thread no longer blocks on `m_processed` flag
- Relies on buffer space condition only
- Eliminates the need for `waitProcessed()`

**Pros**:
- Simpler synchronization (single condition)
- No blocking on main thread
- Audio thread runs as fast as possible

**Cons**:
- May over-produce audio (buffer fills faster)
- Changes fundamental architecture
- May have unintended side effects

### 8.3 Option 3: Adjust Buffer Thresholds

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:228`

**Change**:
```cpp
const bool inputAvailable =
    m_inputChannels[0].data.size() > 0
    && m_audioBuffer.size() < 4000;  // Increase from 2000
```

**Rationale**:
- Audio thread wakes up earlier (when buffer has more space)
- Reduces chance of underrun
- Maintains existing synchronization

**Pros**:
- Minimal change
- Reduces dropout frequency

**Cons**:
- Doesn't fix root cause
- May increase latency
- Band-aid solution

### 8.4 RECOMMENDED: Option 1 (Add `EngineSimWaitForAudio()`)

**Reasoning**:
1. Fixes the root cause (missing synchronization step)
2. Maintains the intended producer-consumer pattern
3. Matches the architect's design intent
4. Minimal code change
5. Proven pattern (similar to GUI's implicit synchronization)

---

## 9. Implementation Details

### 9.1 Code Changes Required

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Location**: Line 938 (after `EngineSimUpdate`)

**Add**:
```cpp
// Update physics
auto simStart = std::chrono::steady_clock::now();
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);
auto simEnd = std::chrono::steady_clock::now();

// ADD: Wait for audio thread to finish processing
// This ensures the audio buffer is being filled before we read from it
EngineSimWaitForAudio(handle);

// Render audio
int framesToRender = framesPerUpdate;
// ...
```

### 9.2 Testing Requirements

1. **Verify no dropouts**: Run CLI for 10+ seconds, check for underruns
2. **Measure latency**: Ensure synchronization doesn't add excessive delay
3. **Performance check**: Verify frame rate remains at 60Hz
4. **Stress test**: High throttle, high RPM conditions

---

## 10. Conclusion

The audio dropout in the CLI is a classic **producer-consumer synchronization bug**:

1. **Producer** (audio thread) waits for consumer signal that never comes
2. **Consumer** (main thread) reads without waiting for producer
3. **Race condition** causes buffer underruns when timing misaligns

The fix is simple: Add the missing synchronization step (`EngineSimWaitForAudio()`) to the CLI main loop. This ensures the producer and consumer coordinate properly, eliminating the race condition and the resulting dropouts.

**Evidence Summary**:
- Audio thread wait condition requires `m_processed == false` (synthesizer.cpp:229)
- Main thread sets `m_processed == false` in `endInputBlock()` (synthesizer.cpp:209)
- CLI never waits for audio thread to finish (missing `EngineSimWaitForAudio()`)
- GUI works because hardware audio provides implicit synchronization
- Dropout periodicity matches buffer exhaustion cycle (~1 second)

**Recommended Action**: Implement Option 1 (Add `EngineSimWaitForAudio()` to CLI main loop) as the primary fix, with Options 2 and 3 as fallback alternatives if performance issues arise.
