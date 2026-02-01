# CLI Audio Dropout Diagnosis

## Executive Summary
The CLI experiences dropouts every ~1 second because **the CLI reads a fixed 800 samples per iteration while the GUI reads a variable amount based on the audio buffer's actual fill level**. This causes the CLI to exhaust the synthesizer's internal 2000-sample buffer faster than it can be replenished.

## Root Cause

### The Critical Evidence

#### 1. Synthesizer Buffer Hard Limit (synthesizer.cpp:228, 233)
```cpp
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannels[0].data.size() > 0
        && m_audioBuffer.size() < 2000;  // ← HARD LIMIT: 2000 samples max
    return !m_run || (inputAvailable && !m_processed);
});

const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),  // ← Only fills up to 2000
    (int)m_inputChannels[0].data.size());
```

**The synthesizer's internal audio buffer NEVER exceeds 2000 samples.**

#### 2. CLI Fixed Read Pattern (engine_sim_cli.cpp:485, 959)
```cpp
const int framesPerUpdate = sampleRate / 60;  // 800 frames per update
// ...
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
// framesToRender is ALWAYS 800 (or less at end)
```

**CLI reads 800 samples every iteration at 60Hz = 48,000 samples/second.**

#### 3. GUI Variable Read Pattern (engine_sim_application.cpp:257-258, 274)
```cpp
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);
// ...
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
```

**GUI reads a variable amount based on how much the audio thread has actually produced.**

## The Dropout Mechanism

### Timing Analysis

**At 48kHz sample rate:**
- CLI runs at: 60 Hz (updateInterval = 1/60)
- CLI reads per iteration: 800 samples
- CLI read rate: 800 × 60 = 48,000 samples/second (matches sample rate ✓)
- Synthesizer buffer max: 2000 samples

**The problem:**
1. Audio thread continuously fills synthesizer buffer up to 2000 samples
2. CLI reads 800 samples every 16.67ms (1/60 second)
3. Time to exhaust 2000 samples: 2000 ÷ 800 = 2.5 iterations = ~42ms
4. Audio thread must render 800 new samples every 16.67ms
5. When audio thread can't keep up, CLI reads less than 800 samples (line 149-153 in synthesizer.cpp)
6. This creates periodic underruns every ~1 second

### Why the ~1 Second Pattern?

The "1-second dropout" pattern occurs because:
- The CLI is reading at the exact rate (48kHz) that audio is being produced
- Any timing jitter (rendering takes too long, thread scheduling delays) causes immediate underrun
- The CLI doesn't check "how much is available" like the GUI does
- The CLI just demands 800 samples every time, even if only 500 are ready

## The GUI's Superior Approach

The GUI calculates `maxWrite` based on:
1. Current buffer write position
2. Safe write position from audio hardware (how much has been consumed)
3. Target write position (100ms ahead at 44.1kHz = 4410 samples)

This means:
- GUI reads HOWEVER MANY SAMPLES ARE ACTUALLY AVAILABLE
- If audio thread is slow, GUI reads less THIS iteration
- If audio thread is fast, GUI reads more THIS iteration
- The buffer acts as a shock absorber for timing variations

The CLI does NOT do this. It says "Give me 800 samples NOW" even if only 100 are ready.

## Specific Code Location

### CLI (WRONG):
**File:** `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
**Lines:** 485, 942-959

```cpp
const int framesPerUpdate = sampleRate / 60;  // 800 frames per update
// ...
int framesToRender = framesPerUpdate;  // ← ALWAYS 800
// ...
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

### GUI (CORRECT):
**File:** `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
**Lines:** 253-274

```cpp
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
const SampleOffset writePosition = m_audioBuffer.m_writePointer;

SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);
// ...
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
```

## The Fix Required

The CLI needs to implement the SAME dynamic read calculation as the GUI:

1. **Query the synthesizer for available samples** before reading
2. **Calculate how much to read based on buffer position**, not a fixed constant
3. **Read whatever is available** (up to a maximum), not demand a fixed amount

### Implementation Required

**Step 1: Add a bridge function to query available samples**

The synthesizer has `m_audioBuffer.size()` (RingBuffer::size() at line 130-134 of ring_buffer.h), but it's not exposed through the bridge. Need to add:

```cpp
// In synthesizer.h (add after line 71):
int getAvailableAudioSamples() const {
    return m_audioBuffer.size();
}

// In simulator.h:
int getAvailableAudioSamples() const {
    return m_synthesizer.getAvailableAudioSamples();
}

// In engine_sim_bridge.h (add new API function):
EngineSimResult EngineSimGetAvailableAudioSamples(
    EngineSimHandle handle,
    int32_t* outAvailableSamples
);

// In engine_sim_bridge.cpp (implement):
EngineSimResult EngineSimGetAvailableAudioSamples(
    EngineSimHandle handle,
    int32_t* outAvailableSamples)
{
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    EngineSimContext* ctx = getContext(handle);
    *outAvailableSamples = ctx->simulator->getAvailableAudioSamples();
    return ESIM_SUCCESS;
}
```

**Step 2: Update CLI to query before reading**

```cpp
// In engine_sim_cli.cpp line 942-959:

// OLD (WRONG):
int framesToRender = framesPerUpdate;  // Fixed 800
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);

// NEW (CORRECT):
int availableSamples = 0;
EngineSimGetAvailableAudioSamples(handle, &availableSamples);
int framesToRender = std::min(framesPerUpdate, availableSamples);
if (framesToRender > 0) {
    result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
}
```

## Additional Evidence

### Synthesizer Read Behavior (synthesizer.cpp:141-159)

```cpp
int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int newDataLength = m_audioBuffer.size();
    if (newDataLength >= samples) {
        m_audioBuffer.readAndRemove(samples, buffer);  // ← Returns requested amount
    }
    else {
        m_audioBuffer.readAndRemove(newDataLength, buffer);  // ← Returns LESS than requested
        memset(
            buffer + newDataLength,
            0,  // ← ZEROS the rest = DROPOUT
            sizeof(int16_t) * ((size_t)samples - newDataLength));
    }

    const int samplesConsumed = std::min(samples, newDataLength);
    return samplesConsumed;
}
```

**When the CLI asks for 800 but only 100 are available, the synthesizer returns 100 samples + 700 zeros.**

Those zeros are the dropouts you hear every second.

## Conclusion

The CLI's audio architecture is correct (using audio thread), but the **read pattern is fundamentally flawed**:
- CLI: Demands fixed 800 samples every 16.67ms,不管 ready or not
- GUI: Reads however many samples are actually available, up to a maximum

The CLI must adopt the GUI's dynamic read pattern to eliminate dropouts.

---

## Summary of Evidence

### Code Locations

| Component | File | Lines | Evidence |
|-----------|------|-------|----------|
| **Synthesizer buffer limit** | `engine-sim-bridge/engine-sim/src/synthesizer.cpp` | 228, 233 | `m_audioBuffer.size() < 2000` - hard limit |
| **CLI fixed read** | `src/engine_sim_cli.cpp` | 485, 959 | `framesPerUpdate = 800` - constant |
| **CLI audio thread start** | `src/engine_sim_cli.cpp` | 699 | `EngineSimStartAudioThread(handle)` - correct |
| **GUI variable read** | `engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` | 257-274 | `maxWrite` calculated from buffer position |
| **Dropout (zeros) behavior** | `engine-sim-bridge/engine-sim/src/synthesizer.cpp` | 149-153 | `memset(buffer + newDataLength, 0, ...)` when insufficient samples |

### The Numbers

- **CLI read rate:** 800 samples/frame × 60 frames/sec = 48,000 samples/sec (exact match to sample rate)
- **Synthesizer buffer max:** 2,000 samples
- **Time to exhaust buffer:** 2,000 ÷ 800 = 2.5 iterations = ~42ms
- **Dropout period:** Every ~1 second (when audio thread can't keep up with demand)

### The Smoking Gun

Line 149-153 in `synthesizer.cpp` proves the dropouts:
```cpp
else {
    m_audioBuffer.readAndRemove(newDataLength, buffer);
    memset(
        buffer + newDataLength,
        0,  // ← ZEROS FILLED IN = DROPOUT
        sizeof(int16_t) * ((size_t)samples - newDataLength));
}
```

When CLI asks for 800 but only 100 are available, 700 samples are ZERO.

### Why GUI Works

GUI calculates `maxWrite` dynamically based on `GetCurrentWritePosition()` from the audio hardware. It reads however many samples are ACTUALLY AVAILABLE, never demanding more than exists. This allows the buffer to act as a timing shock absorber.

CLI demands a fixed 800 samples every time, even if only 100 are ready. This causes immediate underrun when audio thread is slow.
