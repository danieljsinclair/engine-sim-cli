# CLI vs GUI Audio Reading Pattern Comparison

## Executive Summary

This document provides a code-level comparison of how the GUI and CLI read audio from the engine simulator. **CRITICAL FINDING: Both use the EXACT same audio reading function (`readAudioOutput`), but with different sample count calculations.**

---

## 1. Function Call Pattern

### GUI Main Loop (`engine_sim_application.cpp`)

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Process function (lines 202-312)**:

```cpp
void EngineSimApplication::process(float frame_dt) {
    // Line 235: Start simulation frame
    m_simulator->startFrame(1 / avgFramerate);

    // Lines 239-241: Run physics simulation steps
    while (m_simulator->simulateStep()) {
        m_oscCluster->sample();
    }

    // Line 245: End simulation frame
    m_simulator->endFrame();

    // Lines 253-271: Calculate how many audio samples to read
    const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
    const SampleOffset writePosition = m_audioBuffer.m_writePointer;

    SampleOffset targetWritePosition =
        m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
    SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);

    // Lines 273-274: READ AUDIO (this is the critical call)
    int16_t *samples = new int16_t[maxWrite];
    const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
}
```

**Sequence**: `startFrame()` → `simulateStep()` loop → `endFrame()` → `readAudioOutput()`

---

### CLI Main Loop (`engine_sim_cli.cpp`)

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Main loop (lines 908-1125)**:

```cpp
while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
    // Get statistics
    EngineSimStats stats = {};
    EngineSimGetStats(handle, &stats);

    // Lines 1051-1052: Update physics
    EngineSimSetThrottle(handle, throttle);
    EngineSimUpdate(handle, updateInterval);

    // Lines 1056-1075: Render audio
    int framesToRender = framesPerUpdate;  // framesPerUpdate = 48000 / 60 = 800 frames

    float* writePtr = audioBuffer.data() + framesRendered * channels;

    // Line 1075: READ AUDIO (this is the critical call)
    result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
}
```

**Sequence**: `EngineSimUpdate()` → `EngineSimReadAudioBuffer()`

---

## 2. Audio Read Function Analysis

### GUI: Direct Call to `readAudioOutput()`

**Line 274 in `engine_sim_application.cpp`**:

```cpp
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
```

**What is `maxWrite`?**

```cpp
// Line 258: Calculate available space in audio buffer
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);
```

- `targetWritePosition` = safeWritePosition + 0.1 seconds worth of samples (44100 * 0.1 = 4410 samples)
- `maxWrite` = space between current write pointer and target position
- **Typical value**: Varies, but around 4410 samples (0.1 sec @ 44.1kHz) when buffer is healthy

**How many samples returned?**

- Returns MONO int16 samples (single channel)
- Returns up to `maxWrite` samples, or fewer if buffer underrun
- Actual return value stored in `readSamples`

---

### CLI: Call to `EngineSimReadAudioBuffer()` bridge function

**Line 1075 in `engine_sim_cli.cpp`**:

```cpp
result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

**What is `framesToRender`?**

```cpp
// Line 559: Constant definition
const int framesPerUpdate = sampleRate / 60;  // 48000 / 60 = 800 frames

// Line 1056: Used directly
int framesToRender = framesPerUpdate;
```

- **Fixed value**: 800 frames per update
- This is 800 stereo frames = 1600 float samples (L,R pairs)

**How many samples returned?**

- The bridge function converts internally (see section 4)
- Returns MONO int16 samples internally, then converts to stereo float32
- Returns up to `framesToRender` frames (800 = 1600 samples)

---

## 3. Sample Count Comparison

| Aspect | GUI | CLI |
|--------|-----|-----|
| **Sample rate** | 44100 Hz | 48000 Hz |
| **Request function** | `readAudioOutput(maxWrite, ...)` | `EngineSimReadAudioBuffer(handle, ..., framesToRender, ...)` |
| **Request size** | Variable (calculated) | Fixed 800 frames |
| **Typical request** | ~4410 samples (0.1 sec) | 800 frames (0.0133 sec @ 48kHz) |
| **Request frequency** | Every frame (60 Hz) | Every update (60 Hz) |
| **Samples per second** | ~264,600 (4410 * 60) | 48,000 (800 * 60) |

**CRITICAL DIFFERENCE**: GUI requests ~5.5x more samples per call than CLI!

---

## 4. Bridge Implementation

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

### `EngineSimReadAudioBuffer()` implementation (lines 570-629)

```cpp
EngineSimResult EngineSimReadAudioBuffer(
    EngineSimHandle handle,
    float* buffer,
    int32_t frames,
    int32_t* outSamplesRead)
{
    EngineSimContext* ctx = getContext(handle);

    // Line 608: Call the SAME readAudioOutput function as GUI!
    int samplesRead = ctx->simulator->readAudioOutput(
        frames,                           // Request 'frames' mono samples
        ctx->audioConversionBuffer        // Store in int16 buffer
    );

    // Lines 618-622: Convert mono int16 to stereo float32
    constexpr float scale = 1.0f / 32768.0f;

    for (int i = 0; i < samplesRead; ++i) {
        const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
        buffer[i * 2] = sample;     // Left channel
        buffer[i * 2 + 1] = sample; // Right channel
    }

    if (outSamplesRead) {
        *outSamplesRead = samplesRead;
    }

    return ESIM_SUCCESS;
}
```

**What it does internally**:

1. Calls `readAudioOutput(frames, ...)` - **same function as GUI**
2. Gets MONO int16 samples back
3. Converts each int16 to float32 and duplicates to stereo (L, R)
4. Returns number of **frames** (not samples) read

**Sample format conversion**:

- Input: MONO int16 (range: -32768 to +32767)
- Output: STEREO float32 (range: -1.0 to +1.0)
- Conversion: `float = int16 / 32768.0`

---

## 5. What `readAudioOutput()` Actually Does

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Implementation (lines 141-159)**:

```cpp
int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int newDataLength = m_audioBuffer.size();
    if (newDataLength >= samples) {
        m_audioBuffer.readAndRemove(samples, buffer);
    }
    else {
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

**Behavior**:

1. Locks audio buffer mutex (thread-safe)
2. Checks how many samples are available in `m_audioBuffer`
3. If enough available: reads and removes requested samples
4. If not enough: reads all available, fills rest with zeros
5. Returns actual number of samples read (excluding zero-padding)

**IMPORTANT**: This function does NOT generate audio - it only reads from the internal buffer that the audio thread fills.

---

## 6. Audio Thread Architecture

Both GUI and CLI use the **same audio thread architecture**:

### GUI (line 509 in `engine_sim_application.cpp`):

```cpp
m_simulator->startAudioRenderingThread();
```

### CLI (line 775 in `engine_sim_cli.cpp`):

```cpp
result = EngineSimStartAudioThread(handle);
```

**What the audio thread does** (`synthesizer.cpp` lines 215-219):

```cpp
void Synthesizer::audioRenderingThread() {
    while (m_run) {
        renderAudio();  // Generate audio samples from engine state
    }
}
```

**What `renderAudio()` does** (lines 222-256):

1. Waits for input data (engine simulation steps)
2. Reads up to 2000 input samples from input buffer
3. Generates audio by processing each sample through filters
4. Writes generated samples to `m_audioBuffer` (ring buffer)
5. Signals completion

**Key point**: The audio thread runs continuously in the background, filling the buffer. The main thread (GUI or CLI) only reads from this buffer.

---

## 7. Threading Differences

### GUI Threading:

- **Main thread**: Runs physics simulation + reads audio
- **Audio thread**: Runs continuously, generates audio
- **Synchronization**: Mutex-protected ring buffer
- **Audio device**: Uses ysAudioSource with callback-based playback

### CLI Threading:

- **Main thread**: Runs physics simulation + reads audio + plays audio
- **Audio thread**: Runs continuously, generates audio (SAME as GUI)
- **Synchronization**: Mutex-protected ring buffer (SAME as GUI)
- **Audio device**: Uses OpenAL with manual queueing

**Conclusion**: Identical threading model for audio generation.

---

## 8. Buffer Management Differences

### GUI Buffer Management:

```cpp
// GUI uses a ring buffer with dynamic write position calculation
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);
```

- Dynamic calculation based on audio device position
- Maintains 100ms lead ahead of playback
- Adjusts if lead gets too large (> 0.5 sec)

### CLI Buffer Management:

```cpp
// CLI uses fixed-size requests
const int framesPerUpdate = sampleRate / 60;  // 800 frames
int framesToRender = framesPerUpdate;
```

- Fixed 800 frames per update
- No dynamic adjustment based on buffer state
- Simpler, but less adaptive

---

## 9. Summary of Key Differences

| Aspect | GUI | CLI |
|--------|-----|-----|
| **Audio read function** | `m_simulator->readAudioOutput()` | `EngineSimReadAudioBuffer()` → calls same `readAudioOutput()` |
| **Samples per call** | ~4410 (variable, 0.1 sec @ 44.1kHz) | 800 (fixed, 0.0133 sec @ 48kHz) |
| **Sample rate** | 44100 Hz | 48000 Hz |
| **Buffer size calculation** | Dynamic, based on device position | Fixed, based on update interval |
| **Audio thread** | Yes (`startAudioRenderingThread()`) | Yes (`EngineSimStartAudioThread()`) |
| **Synchronization** | Mutex-protected ring buffer | Mutex-protected ring buffer |
| **Sample format** | MONO int16 (internal) | MONO int16 (internal) → STEREO float32 (output) |
| **Main loop rate** | 60 Hz (variable) | 60 Hz (fixed) |

---

## 10. Critical Findings

### Finding 1: Same Core Function

Both GUI and CLI call the **exact same core function**: `Synthesizer::readAudioOutput()`.

- GUI: Direct call via `m_simulator->readAudioOutput()`
- CLI: Indirect call via `EngineSimReadAudioBuffer()` → `ctx->simulator->readAudioOutput()`

### Finding 2: Sample Count Difference

**GUI requests ~5.5x more samples per call than CLI**:

- GUI: ~4410 samples (variable, typically 0.1 sec worth)
- CLI: 800 samples (fixed, 1/60 second worth)

This is the PRIMARY difference in audio reading patterns.

### Finding 3: Sample Rate Difference

- GUI: 44100 Hz
- CLI: 48000 Hz

This affects:
- Audio quality (CLI has 9% higher sample rate)
- Buffer sizes (CLI uses larger buffers)
- Frequency response (CLI can capture higher frequencies)

### Finding 4: No `renderAudio()` Call in Main Thread

**IMPORTANT**: Neither GUI nor CLI calls `renderAudio()` in the main thread.

- The audio thread calls `renderAudio()` continuously
- The main thread only calls `readAudioOutput()` to read from the buffer
- This is the correct architecture (producer-consumer pattern)

### Finding 5: CLI Conversion Overhead

CLI has additional conversion step:

```cpp
// Convert mono int16 to stereo float32
for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left
    buffer[i * 2 + 1] = sample; // Right
}
```

GUI uses int16 samples directly without conversion.

---

## 11. Code References

### GUI Code Paths:

1. Main loop: `engine_sim_application.cpp:202-312` (`process()` function)
2. Audio read: `engine_sim_application.cpp:274` (`readAudioOutput()` call)
3. Audio thread start: `engine_sim_application.cpp:509` (`startAudioRenderingThread()`)

### CLI Code Paths:

1. Main loop: `engine_sim_cli.cpp:908-1125` (main simulation loop)
2. Audio read: `engine_sim_cli.cpp:1075` (`EngineSimReadAudioBuffer()` call)
3. Audio thread start: `engine_sim_cli.cpp:775` (`EngineSimStartAudioThread()` call)

### Bridge Code Paths:

1. `EngineSimReadAudioBuffer()`: `engine_sim_bridge.cpp:570-629`
2. `EngineSimStartAudioThread()`: `engine_sim_bridge.cpp:379-396`
3. Audio conversion: `engine_sim_bridge.cpp:618-622` (mono int16 → stereo float32)

### Core Synthesizer Code:

1. `readAudioOutput()`: `synthesizer.cpp:141-159`
2. `renderAudio()`: `synthesizer.cpp:222-256`
3. Audio thread: `synthesizer.cpp:215-219`

---

## 12. Conclusion

The CLI and GUI use **identical audio generation architecture**:

1. Both start an audio rendering thread
2. Audio thread continuously calls `renderAudio()` to generate samples
3. Main thread calls `readAudioOutput()` to read samples from buffer
4. Samples are stored in a mutex-protected ring buffer

**Key difference**: Sample count per call
- GUI: Variable, typically ~4410 samples (0.1 sec @ 44.1kHz)
- CLI: Fixed 800 samples (0.0133 sec @ 48kHz)

This difference likely contributes to audio quality/performance variations between GUI and CLI output.

---

**Generated**: 2025-01-31
**Files analyzed**:
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
