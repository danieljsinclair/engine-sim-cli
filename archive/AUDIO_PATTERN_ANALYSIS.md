# GUI Audio Reading Pattern - EXACT Analysis

**Date**: 2026-01-31
**Mission**: Document the GUI's exact audio reading implementation
**Methodology**: Code-only analysis, no speculation

---

## Executive Summary

The GUI uses a **dynamic pull model** where it calculates exactly how many samples it can safely write to the audio hardware buffer each frame, then requests **only that amount** from the synthesizer. The synthesizer returns fewer samples if underrun occurs, and the GUI handles this gracefully.

---

## 1. Main Loop Pattern

### File: `engine_sim_application.cpp`, Function: `process()` (Line 202)

**EXACT Call Sequence:**

```cpp
// Line 235: Start simulation frame
m_simulator->startFrame(1 / avgFramerate);

// Lines 239-241: Process all simulation steps
while (m_simulator->simulateStep()) {
    m_oscCluster->sample();
}

// Line 245: End simulation frame
m_simulator->endFrame();

// Lines 253-274: AUDIO READING (detailed below)
```

### Audio Reading Algorithm (Lines 253-290)

```cpp
// Line 253: Get current hardware write position (where audio hardware is reading)
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();

// Line 254: Get our current write pointer (where we've written up to)
const SampleOffset writePosition = m_audioBuffer.m_writePointer;

// Line 256-257: Calculate target write position (100ms ahead of hardware)
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));

// Line 258: Calculate how many samples we can safely write
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);

// Line 260-261: Calculate current lead time
SampleOffset currentLead = m_audioBuffer.offsetDelta(safeWritePosition, writePosition);
SampleOffset newLead = m_audioBuffer.offsetDelta(safeWritePosition, targetWritePosition);

// Lines 263-267: Safety check - if we're too far ahead, adjust
if (currentLead > 44100 * 0.5) {
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
    currentLead = m_audioBuffer.offsetDelta(safeWritePosition, m_audioBuffer.m_writePointer);
    maxWrite = m_audioBuffer.offsetDelta(m_audioBuffer.m_writePointer, targetWritePosition);
}

// Lines 269-271: Don't write if we're already ahead of target
if (currentLead > newLead) {
    maxWrite = 0;
}

// Line 273: Allocate buffer for EXACT amount we can write
int16_t *samples = new int16_t[maxWrite];

// Line 274: READ AUDIO (THE CRITICAL CALL)
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);

// Lines 276-287: Write to hardware buffer (only what we actually got)
for (SampleOffset i = 0; i < (SampleOffset)readSamples && i < maxWrite; ++i) {
    const int16_t sample = samples[i];
    // ... oscilloscope update ...
    m_audioBuffer.writeSample(sample, m_audioBuffer.m_writePointer, (int)i);
}

// Line 289: Clean up
delete[] samples;
```

### Key Observations:

1. **Variable sample count**: `maxWrite` is calculated dynamically each frame
2. **Target lead time**: 100ms (`44100 * 0.1` = 4410 samples)
3. **Max lead threshold**: 500ms (`44100 * 0.5` = 22050 samples)
4. **Safety lead**: 50ms (`44100 * 0.05` = 2205 samples)
5. **Uses actual returned count**: `readSamples` may be less than `maxWrite`

---

## 2. Audio Read Function Signature

### File: `synthesizer.h`, Line 71

```cpp
int readAudioOutput(int samples, int16_t *buffer);
```

**Parameters:**
- `samples`: Number of samples requested (IN)
- `buffer`: Output buffer for int16_t samples (OUT)

**Returns:**
- `int`: Actual number of samples read (may be less than requested)

**Call chain:**
```
EngineSimApplication::process()
  -> Simulator::readAudioOutput()
    -> Synthesizer::readAudioOutput()
```

---

## 3. Audio Read Implementation

### File: `synthesizer.cpp`, Function: `readAudioOutput()` (Lines 141-159)

```cpp
int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int newDataLength = m_audioBuffer.size();
    if (newDataLength >= samples) {
        // Buffer has enough - read requested amount
        m_audioBuffer.readAndRemove(samples, buffer);
    }
    else {
        // UNDERRUN - read what's available, fill rest with zeros
        m_audioBuffer.readAndRemove(newDataLength, buffer);
        memset(
            buffer + newDataLength,
            0,
            sizeof(int16_t) * ((size_t)samples - newDataLength));
    }

    const int samplesConsumed = std::min(samples, newDataLength);

    return samplesConsumed;
}
```

### Behavior:

1. **Locks mutex**: Thread-safe access to `m_audioBuffer`
2. **Checks available**: `newDataLength = m_audioBuffer.size()`
3. **If sufficient**: Reads exactly `samples` amount
4. **If insufficient**:
   - Reads all available (`newDataLength`)
   - Fills remainder with **zeros** (silence)
   - Returns actual count consumed
5. **Returns**: `std::min(samples, newDataLength)`

### Critical Point:

**The function ALWAYS fills the output buffer completely** (either with data or zeros). The return value tells the caller how much was real data vs. zeros.

---

## 4. Audio Thread Behavior

### File: `synthesizer.cpp`, Function: `audioRenderingThread()` (Lines 215-219)

```cpp
void Synthesizer::audioRenderingThread() {
    while (m_run) {
        renderAudio();
    }
}
```

### Thread Loop: `renderAudio()` (Lines 222-256)

```cpp
void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    // WAIT CONDITION (Lines 225-230):
    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;
        return !m_run || (inputAvailable && !m_processed);
    });

    // CALCULATE HOW MANY SAMPLES TO GENERATE (Lines 232-234):
    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),
        (int)m_inputChannels[0].data.size()
    );

    // READ FROM INPUT BUFFER (Lines 236-240):
    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();

    // GENERATE AUDIO SAMPLES (Lines 251-253):
    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    // NOTIFY WAITING THREADS (Line 255):
    m_cv0.notify_one();
}
```

### Audio Thread Pattern:

1. **Wait condition**:
   - Wakes when input data available AND audio buffer below 2000 samples
   - OR when `m_run` is false (shutdown)
2. **Generation limit**: Maintains buffer at **2000 samples maximum**
3. **Batches**: Generates `n` samples where `n = min(2000 - bufferSize, inputSize)`
4. **Locking**: Releases lock during DSP processing ( Lines 243-254)
5. **Notification**: Notifies after generating samples

### Sample Rate Implications:

At 44100 Hz:
- 2000 samples = **45ms** of audio
- Thread wakes when buffer drops below 45ms
- Thread fills back up to 45ms

At 48000 Hz:
- 2000 samples = **41.6ms** of audio

---

## 5. Bridge Layer Comparison

### File: `engine_sim_bridge.h`

#### `EngineSimRender()` (Lines 267-272)

**Purpose**: Synchronous rendering (NO audio thread)

**Signature:**
```cpp
EngineSimResult EngineSimRender(
    EngineSimHandle handle,
    float* buffer,
    int32_t frames,
    int32_t* outSamplesWritten
);
```

**Implementation** (`engine_sim_bridge.cpp`, Lines 483-568):
```cpp
// CRITICAL: Call renderAudio() ONCE to generate samples
ctx->simulator->synthesizer().renderAudio();

// Read from synthesizer
int samplesRead = ctx->simulator->readAudioOutput(
    frames,
    ctx->audioConversionBuffer
);

// Convert mono int16 to stereo float32
for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left
    buffer[i * 2 + 1] = sample; // Right
}
```

#### `EngineSimReadAudioBuffer()` (Lines 300-305)

**Purpose**: Asynchronous reading (WITH audio thread)

**Signature:**
```cpp
EngineSimResult EngineSimReadAudioBuffer(
    EngineSimHandle handle,
    float* buffer,
    int32_t frames,
    int32_t* outSamplesRead
);
```

**Implementation** (`engine_sim_bridge.cpp`, Lines 570-629):
```cpp
// IMPORTANT: Does NOT call renderAudio()
// Audio thread is filling the buffer continuously

// Read from synthesizer
int samplesRead = ctx->simulator->readAudioOutput(
    frames,
    ctx->audioConversionBuffer
);

// Convert mono int16 to stereo float32
for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left
    buffer[i * 2 + 1] = sample; // Right
}
```

### EXACT Differences:

| Aspect | `EngineSimRender()` | `EngineSimReadAudioBuffer()` |
|--------|-------------------|----------------------------|
| **Calls `renderAudio()`?** | YES (line 527) | NO |
| **Mode** | Synchronous | Asynchronous |
| **Audio thread** | Not used | Must be running |
| **When to use** | Single-threaded, real-time constrained | Multi-threaded, audio thread available |
| **Matches GUI** | NO | **YES** (GUI never calls `renderAudio()`) |

---

## 6. Summary: The GUI Pattern

### What the GUI Does:

1. **Each frame** (typically 60 FPS = 16.67ms):
   - Update physics (`startFrame()`, `simulateStep()`, `endFrame()`)
   - Calculate safe write position (100ms ahead of hardware)
   - Request `maxWrite` samples from synthesizer
   - Write returned samples to hardware buffer

2. **Never calls `renderAudio()` directly**:
   - Audio thread calls `renderAudio()` automatically
   - Audio thread maintains 2000 sample buffer (~45ms)
   - GUI just reads what's available

3. **Handles underruns gracefully**:
   - If synthesizer has fewer samples than requested
   - GUI writes only what it got
   - Next frame will try again

### Sample Count Flow:

```
Physics Update (60 Hz):
  - startFrame(1/60)
  - simulateStep() loop
    - writeInput() called
    - endInputBlock() called
      - Notifies audio thread
  - endFrame()

Audio Thread (Event-driven):
  - Wakes when: inputAvailable && bufferSize < 2000
  - Generates up to 2000 samples
  - Goes back to sleep

Main Loop (60 Hz):
  - Calculates maxWrite (dynamic, varies each frame)
  - readAudioOutput(maxWrite, buffer)
    - Returns up to maxWrite samples
  - Writes returned samples to hardware
```

---

## 7. Constants and Magic Numbers

| Constant | Value | Meaning | Location |
|----------|-------|---------|----------|
| Sample rate (GUI) | 44100 | Audio sample rate | `engine_sim_application.cpp:169` |
| Target lead | 44100 * 0.1 = 4410 samples | 100ms ahead of hardware | Line 257 |
| Max lead threshold | 44100 * 0.5 = 22050 samples | 500ms - safety check | Line 263 |
| Safety lead | 44100 * 0.05 = 2205 samples | 50ms after reset | Line 264 |
| Audio buffer target | 2000 samples | ~45ms @ 44.1kHz | `synthesizer.cpp:228` |
| Audio buffer size | 44100 samples | 1 second @ 44.1kHz | `engine_sim_application.cpp:169` |
| Input buffer size | 1024 samples | Configuration | `synthesizer.h:35` |

---

## 8. Key Findings

### Finding 1: Dynamic Sample Requests

**The GUI does NOT request a fixed number of samples.**

Each frame, it calculates `maxWrite` based on:
- Current hardware read position
- Target lead time (100ms)
- Current buffer fill level
- Safety checks

This can vary from **0 to thousands of samples** per frame.

### Finding 2: Underrun Handling

**The synthesizer ALWAYS fills the requested buffer size.**

If underrun occurs:
- Reads available samples
- Fills remainder with zeros
- Returns actual sample count

This ensures the audio callback never gets partial buffers.

### Finding 3: Audio Thread Independence

**The audio thread runs completely independently.**

- Wakes on condition variable (input available && buffer < 2000)
- Generates samples in batches (up to 2000)
- Notifies waiting threads
- Goes back to sleep

The main loop never explicitly calls `renderAudio()`.

### Finding 4: Bridge Layer Mismatch

**`EngineSimRender()` does NOT match the GUI pattern.**

- GUI: `readAudioOutput()` only (audio thread calls `renderAudio()`)
- `EngineSimRender()`: Calls `renderAudio()` THEN `readAudioOutput()`
- `EngineSimReadAudioBuffer()`: Matches GUI (only calls `readAudioOutput()`)

---

## 9. Recommendations for CLI Implementation

### To Match GUI Behavior:

1. **Use audio thread** (`EngineSimStartAudioThread()`)
2. **Call `EngineSimReadAudioBuffer()`** NOT `EngineSimRender()`
3. **Handle variable sample counts** (check `outSamplesRead`)
4. **Handle underruns gracefully** (zeros are already filled by synthesizer)
5. **Don't call `renderAudio()` explicitly** (let audio thread do it)

### Required Call Pattern:

```cpp
// Initialization
EngineSimCreate(&config, &handle);
EngineSimLoadScript(handle, scriptPath, assetPath);
EngineSimStartAudioThread(handle); // Start audio thread

// Main loop (60 Hz)
while (running) {
    // Update physics
    EngineSimUpdate(handle, deltaTime);

    // Read audio (variable amount)
    int32_t samplesRead;
    EngineSimReadAudioBuffer(handle, buffer, frames, &samplesRead);

    // Handle what we got (may be less than frames)
    if (samplesRead < frames) {
        // Underrun occurred - CLI received silence for remainder
        // This is normal, audio thread will catch up
    }

    // Write samplesRead * 2 floats to audio output
}
```

---

## 10. Evidence Locations

All line numbers refer to:
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/`

### Files Analyzed:

1. **`engine_sim_application.cpp`**
   - `process()`: Lines 202-312
   - Audio reading: Lines 253-290
   - Audio buffer init: Lines 169-177

2. **`synthesizer.cpp`**
   - `readAudioOutput()`: Lines 141-159
   - `audioRenderingThread()`: Lines 215-219
   - `renderAudio()`: Lines 222-256
   - Buffer init: Lines 81-83

3. **`synthesizer.h`**
   - `readAudioOutput()` signature: Line 71
   - Constants: Lines 33-40

4. **`engine_sim_bridge.h`**
   - `EngineSimRender()`: Lines 267-272
   - `EngineSimReadAudioBuffer()`: Lines 300-305

5. **`engine_sim_bridge.cpp`**
   - `EngineSimRender()`: Lines 483-568
   - `EngineSimReadAudioBuffer()`: Lines 570-629

---

**END OF ANALYSIS**

All information sourced directly from code. No speculation.
