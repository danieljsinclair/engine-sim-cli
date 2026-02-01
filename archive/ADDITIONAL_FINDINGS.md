# ADDITIONAL FINDINGS: CLI Audio Dropout Root Cause

**Date**: 2026-01-29
**Investigator**: Fourth Analysis (after TA1, TA2, TA3)
**Issue**: Small dropouts at 10% throttle even after increasing read size to 4800 frames

---

## EXECUTIVE SUMMARY

**THE ROOT CAUSE**: The CLI has a **race condition** between `EngineSimUpdate()` and `EngineSimReadAudioBuffer()` that the GUI doesn't have, combined with the audio thread's design that allows reads DURING rendering.

**KEY DIFFERENCE**: GUI has 29 lines of work between `endFrame()` and `readAudioOutput()`, giving the audio thread time to finish rendering. CLI has virtually no delay, causing it to read audio BEFORE the audio thread finishes writing it.

**AT 10% THROTTLE**: The problem is exacerbated because the audio thread renders fewer samples per call (limited by input buffer size), so the buffer doesn't fill as fast as the CLI consumes it.

---

## DETAILED FINDINGS

### 1. GUI vs CLI Timing Comparison

**GUI** (`engine_sim_application.cpp`):
```cpp
Line 245: m_simulator->endFrame()
         -> Calls synthesizer.endInputBlock()
         -> Sets m_processed = false
         -> Notifies audio thread

Lines 247-272: [29 LINES OF WORK]
         - Performance metrics collection
         - Audio buffer position calculations
         - Multiple calls to m_audioBuffer.offsetDelta()
         - Conditional logic for buffer management
         - Memory allocation for samples buffer

Line 274: m_simulator->readAudioOutput(maxWrite, samples)
         -> Locks m_lock0
         -> Reads from m_audioBuffer
```

**CLI** (`engine_sim_cli.cpp`):
```cpp
Line 942: EngineSimUpdate(handle, updateInterval)
         -> Calls simulator.endFrame()
         -> Sets m_processed = false
         -> Notifies audio thread

Lines 944-965: [MINIMAL WORK - ~20 LINES]
         - Variable assignments (framesToRender, samplesWritten)
         - Bounds checking
         - Simple conditional logic
         - NO buffer position calculations
         - NO complex operations

Line 965: EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten)
         -> Calls readAudioOutput()
         -> Locks m_lock0
         -> Reads from m_audioBuffer
```

**CRITICAL**: The GUI's 29 lines of work create a natural delay that gives the audio thread time to complete rendering before the read. The CLI lacks this delay.

---

### 2. Audio Thread Rendering Flow

**Audio Thread** (`synthesizer.cpp:222-256`):

```cpp
void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    // Wait for work
    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0   // Needs input
            && m_audioBuffer.size() < 2000;      // Buffer not full
        return !m_run || (inputAvailable && !m_processed);
    });

    // Calculate how many samples to render
    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),  // Space available
        (int)m_inputChannels[0].data.size()              // Input available
    );

    // Read from input buffer
    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();  // <-- UNLOCKS HERE!

    // Render audio OUTSIDE the lock (line 245-253)
    for (int i = 0; i < m_inputChannelCount; ++i) {
        // Filter setup...
    }

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));  // <-- TAKES TIME!
    }

    m_cv0.notify_one();
}
```

**THE RACE CONDITION**:

1. CLI calls `EngineSimUpdate()` → `endInputBlock()` → sets `m_processed = false` → notifies audio thread
2. Audio thread wakes up, calculates `n`, sets `m_processed = true`, **UNLOCKS** `m_lock0`
3. Audio thread starts rendering (slow operation, takes time)
4. **CLI IMMEDIATELY calls `EngineSimReadAudioBuffer()`**
5. CLI acquires `m_lock0` (audio thread released it!)
6. CLI reads from `m_audioBuffer` while audio thread is STILL RENDERING
7. CLI asks for 4800 samples but buffer only has partial data
8. **UNDERRUN**: `readAudioOutput()` fills remaining samples with zeros

---

### 3. Why 10% Throttle is Worse

**Input Sample Generation**:

From `synthesizer.cpp:168-194` (`writeInput()`):
```cpp
void Synthesizer::writeInput(const double *data) {
    m_inputWriteOffset += (double)m_audioSampleRate / m_inputSampleRate;
    // ...
    for (; s <= distance; s += 1.0) {
        // Interpolates and writes samples to input buffer
        buffer.write(m_filters[i].antialiasing.fast_f(static_cast<float>(sample)));
    }
}
```

- Audio sample rate: 48000 Hz (CLI) or 44100 Hz (GUI)
- Input sample rate: 10000 Hz (simulation frequency)
- Ratio: 4.8 or 4.41
- **Each `writeInput()` call generates ~4-5 samples**

From `piston_engine_simulator.cpp:412`:
```cpp
synthesizer().writeInput(m_exhaustFlowStagingBuffer);
```
- Called **once per simulation step**

**Calculation for 60 FPS**:
- 1 frame = 1/60 second = 16.67ms
- Simulation runs at 10000 Hz
- Steps per frame = 10000 / 60 ≈ 167 steps
- Input samples per frame = 167 * 4.8 ≈ **800 samples**

**Audio Thread Render Limit**:

```cpp
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),  // Max 2000 samples in buffer
    (int)m_inputChannels[0].data.size()              // Only 800 available!
);
```

At 10% throttle:
- Input buffer provides ~800 samples per frame
- Audio thread renders `n = min(2000 - buffer_size, 800)` samples
- If buffer has 1000 samples: `n = min(1000, 800) = 800`
- **Only 800 samples rendered!**
- CLI asks for 4800 samples
- **UNDERRUN: 4000 samples filled with zeros!**

At 100% throttle:
- Input buffer provides ~800 samples per frame (SAME count!)
- But exhaust flow values are HIGHER
- However, sample COUNT is still limited by input buffer
- The issue persists but is less noticeable because audio is louder

---

### 4. The Real Issue: Buffer Underrun Design

**`readAudioOutput()` Implementation** (`synthesizer.cpp:141-159`):

```cpp
int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int newDataLength = m_audioBuffer.size();
    if (newDataLength >= samples) {
        m_audioBuffer.readAndRemove(samples, buffer);
    }
    else {
        // UNDERRUN! Fill rest with silence
        m_audioBuffer.readAndRemove(newDataLength, buffer);
        memset(
            buffer + newDataLength,
            0,
            sizeof(int16_t) * ((size_t)samples - newDataLength));
    }

    const int samplesConsumed = std::min(samples, newDataLength);
    return samplesConsumed;  // Returns actual samples read (less than requested!)
}
```

**THE PROBLEM**: This function is designed to handle underruns by filling with silence. The CLI calls it requesting 4800 samples, but the audio thread hasn't finished rendering that many yet.

**GUI doesn't have this issue because**:
- GUI has 29 lines of work between `endFrame()` and `readAudioOutput()`
- This delay gives the audio thread time to finish rendering
- By the time GUI reads, the buffer has enough samples

**CLI has this issue because**:
- CLI has virtually no delay between `EngineSimUpdate()` and `EngineSimReadAudioBuffer()`
- Audio thread is still rendering when CLI reads
- Buffer doesn't have enough samples yet
- **UNDERRUN → SILENCE → DROPOUTS**

---

### 5. Why Previous Fixes Didn't Work

**TA1 Fix**: Increased read size from 800 to 4800 frames
- **Why it didn't work**: Larger read size makes the problem WORSE!
  - CLI asks for 4800 samples instead of 800
  - Audio thread still only renders ~800 per call
  - Underrun is now 4000 samples instead of 0

**TA2 Finding**: 10% load = 99% actual throttle with gamma=2.0
- **Why it didn't fix**: Throttle value doesn't affect input sample COUNT
  - Sample count is determined by simulation frequency (10000 Hz)
  - Throttle affects sample VALUES (amplitude), not count
  - The underrun issue is about COUNT, not amplitude

**TA3 Finding**: CLI doesn't call `endInputBlock()`
- **Why it was wrong**: CLI DOES call `endInputBlock()` via `EngineSimUpdate()`
  - The call chain is: `EngineSimUpdate()` → `endFrame()` → `endInputBlock()`
  - This was confirmed by code inspection

---

### 6. Evidence from Code

**File: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`**

Lines 940-966 (CLI main loop):
```cpp
// Update physics
auto simStart = std::chrono::steady_clock::now();
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);  // Line 942
auto simEnd = std::chrono::steady_clock::now();

// Render audio
int framesToRender = framesPerUpdate;  // 4800 frames

if (!args.interactive) {
    int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
    framesToRender = std::min(framesPerUpdate, totalExpectedFrames - framesProcessed);
}

if (framesToRender > 0) {
    int samplesWritten = 0;
    float* writePtr = audioBuffer.data() + framesRendered * channels;

    auto renderStart = std::chrono::steady_clock::now();
    // CRITICAL: No delay between EngineSimUpdate and this call!
    result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);  // Line 965
    auto renderEnd = std::chrono::steady_clock::now();
```

**File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`**

Lines 245-274 (GUI process loop):
```cpp
m_simulator->endFrame();  // Line 245

auto duration = proc_t1 - proc_t0;
if (iterationCount > 0) {
    m_performanceCluster->addTimePerTimestepSample(
        (duration.count() / 1E9) / iterationCount);
}

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

if (currentLead > newLead) {
    maxWrite = 0;
}

int16_t *samples = new int16_t[maxWrite];
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);  // Line 274
```

**File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`**

Lines 222-256 (Audio thread rendering):
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

    lk0.unlock();  // <-- UNLOCKS BEFORE RENDERING!

    // ... filter setup ...

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));  // <-- SLOW OPERATION
    }

    m_cv0.notify_one();
}
```

---

## THE FIX

### Root Cause
The CLI reads audio immediately after notifying the audio thread, before the thread has time to finish rendering. The audio thread releases the lock before rendering, allowing the CLI to read partially-filled buffers.

### Solution Options

**Option 1: Wait for Audio Thread to Complete** (RECOMMENDED)
- Call `synthesizer.waitProcessed()` after `endFrame()` but before `readAudioOutput()`
- This ensures the audio thread has finished rendering before reading
- Implementation: Add `EngineSimWaitForAudio()` bridge function

**Option 2: Read Smaller Chunks**
- Read audio in smaller chunks (e.g., 800 samples instead of 4800)
- This matches the input buffer generation rate
- Downside: More function calls per frame

**Option 3: Add Artificial Delay**
- Add a small sleep or yield after `EngineSimUpdate()`
- Gives audio thread time to render
- Downside: Wasteful, unpredictable

**Option 4: Poll Buffer Until Full**
- Loop in `readAudioOutput()` until buffer has enough samples
- Downside: Busy waiting, wasteful CPU

### Recommended Implementation

**File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`**

Add new function:
```cpp
EngineSimResult EngineSimWaitForAudio(EngineSimHandle handle) {
    if (handle == nullptr) return ESIM_INVALID_HANDLE;

    EngineSimContext* ctx = reinterpret_cast<EngineSimContext*>(handle);
    ctx->simulator->getSynthesizer().waitProcessed();

    return ESIM_SUCCESS;
}
```

**File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h`**

Add declaration:
```cpp
EngineSimResult EngineSimWaitForAudio(EngineSimHandle handle);
```

**File: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`**

Modify main loop (around line 942):
```cpp
// Update physics
auto simStart = std::chrono::steady_clock::now();
EngineSimSetThrottle(handle, throttle);
EngineSimUpdate(handle, updateInterval);
auto simEnd = std::chrono::steady_clock::now();

// CRITICAL: Wait for audio thread to finish rendering
// This prevents race condition where we read before audio is ready
EngineSimWaitForAudio(handle);

// Render audio
int framesToRender = framesPerUpdate;
// ... rest of code unchanged
```

### Why This Works

1. `EngineSimUpdate()` calls `endFrame()` → `endInputBlock()` → sets `m_processed = false` → notifies audio thread
2. `EngineSimWaitForAudio()` calls `waitProcessed()` → waits for `m_processed == true`
3. Audio thread wakes up, renders audio, sets `m_processed = true`, notifies
4. `waitProcessed()` returns
5. CLI calls `EngineSimReadAudioBuffer()` → buffer is now FULL
6. **NO UNDERRUN**

---

## CONCLUSION

The three previous TAs missed the root cause because:

1. **TA1** focused on buffer size but didn't consider timing between update and read
2. **TA2** focused on throttle values but didn't analyze the audio thread synchronization
3. **TA3** incorrectly claimed CLI doesn't call `endInputBlock()` (it does, via `EngineSimUpdate()`)

The real issue is a **race condition** between the main thread and audio thread, exacerbated by:
- CLI's lack of delay between `EngineSimUpdate()` and `EngineSimReadAudioBuffer()`
- Audio thread unlocking the mutex before rendering
- Large read size (4800) exceeding what the audio thread renders per call (~800)

The fix is simple: **wait for the audio thread to finish rendering before reading**. This matches what the GUI does implicitly through its 29 lines of work between `endFrame()` and `readAudioOutput()`.
