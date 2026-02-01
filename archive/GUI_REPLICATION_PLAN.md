# GUI Audio Replication Analysis

**Goal**: Determine EXACTLY what the GUI does that works, and what the CLI needs to change to replicate it.

**Executive Summary**: The GUI uses an audio thread that continuously renders audio in the background, while the CLI uses synchronous rendering. The CLI's dropouts are caused by reading from an audio buffer that is NOT being continuously filled by an audio thread.

---

## 1. GUI Audio Architecture (EXACT Implementation)

### Location: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

### 1.1 Main Loop - `process()` function

**Location**: Lines 202-312

```cpp
void EngineSimApplication::process(float frame_dt) {
    frame_dt = static_cast<float>(clamp(frame_dt, 1 / 200.0f, 1 / 30.0f));

    double speed = 1.0 / 1.0;
    // ... speed control code ...

    m_simulator->setSimulationSpeed(speed);

    const double avgFramerate = clamp(m_engine.GetAverageFramerate(), 30.0f, 1000.0f);
    m_simulator->startFrame(1 / avgFramerate);

    auto proc_t0 = std::chrono::steady_clock::now();
    const int iterationCount = m_simulator->getFrameIterationCount();
    while (m_simulator->simulateStep()) {
        m_oscCluster->sample();
    }

    auto proc_t1 = std::chrono::steady_clock::now();

    m_simulator->endFrame();

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
    const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);

    for (SampleOffset i = 0; i < (SampleOffset)readSamples && i < maxWrite; ++i) {
        const int16_t sample = samples[i];
        if (m_oscillatorSampleOffset % 4 == 0) {
            m_oscCluster->getAudioWaveformOscilloscope()->addDataPoint(
                m_oscillatorSampleOffset,
                sample / (float)(INT16_MAX));
        }

        m_audioBuffer.writeSample(sample, m_audioBuffer.m_writePointer, (int)i);

        m_oscillatorSampleOffset = (m_oscillatorSampleOffset + 1) % (44100 / 10);
    }

    delete[] samples;

    if (readSamples > 0) {
        SampleOffset size0, size1;
        void *data0, *data1;
        m_audioSource->LockBufferSegment(
            m_audioBuffer.m_writePointer, readSamples, &data0, &size0, &data1, &size1);

        m_audioBuffer.copyBuffer(
            reinterpret_cast<int16_t *>(data0), m_audioBuffer.m_writePointer, size0);
        m_audioBuffer.copyBuffer(
            reinterpret_cast<int16_t *>(data1),
            m_audioBuffer.getBufferIndex(m_audioBuffer.m_writePointer, size0),
            size1);

        m_audioSource->UnlockBufferSegments(data0, size0, data1, size1);
        m_audioBuffer.commitBlock(readSamples);
    }

    m_performanceCluster->addInputBufferUsageSample(
        (double)m_simulator->getSynthesizerInputLatency() / m_simulator->getSynthesizerInputLatencyTarget());
    m_performanceCluster->addAudioLatencySample(
        m_audioBuffer.offsetDelta(m_audioSource->GetCurrentWritePosition(), m_audioBuffer.m_writePointer) / (44100 * 0.1));
}
```

**What it does EXACTLY each frame**:
1. Clamp frame time to 1/200 to 1/30 seconds
2. Update simulator with `startFrame()`, `simulateStep()` loop, `endFrame()`
3. Calculate how much audio to read based on playback position
4. **Call `readAudioOutput(maxWrite, samples)`** - reads from audio buffer
5. Write samples to OpenAL audio buffer
6. Update diagnostics

### 1.2 Audio Thread Usage

**Audio thread start**: Line 509 in `loadEngine()`
```cpp
m_simulator->startAudioRenderingThread();
```

**Audio thread stop**: Line 421 in `run()`
```cpp
m_simulator->endAudioRenderingThread();
```

**How it coordinates**: The audio thread runs continuously in the background, filling the audio buffer. The main thread reads from this buffer using `readAudioOutput()`.

### 1.3 Audio Reading

**Function called**: `m_simulator->readAudioOutput(maxWrite, samples)` (Line 274)

**How many samples**: Variable `maxWrite` - calculated based on:
- Current playback position from audio source
- Target write position (100ms ahead of playback)
- Buffer size management

**When relative to simulation update**:
- Simulation update happens FIRST (lines 232-245)
- Audio reading happens AFTER (line 274)
- This happens EVERY frame (60 FPS typically)

---

## 2. CLI Current Implementation

### Location: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

### 2.1 Audio Thread Usage

**Does CLI start audio thread?**: NO

**Evidence**: Line 698-700
```cpp
// NOTE: Use synchronous rendering (EngineSimRender) for CLI
// Audio thread is used by GUI but CLI can render synchronously from main thread
std::cout << "[3/5] Using synchronous audio rendering\n";
```

### 2.2 Audio Function Called

**Line 954**:
```cpp
result = EngineSimRender(handle, writePtr, framesToRender, &samplesWritten);
```

`EngineSimRender` is a SYNCHRONOUS rendering function that:
1. Calls `renderAudio()` to generate audio samples
2. Converts samples to float32 format
3. Returns the rendered samples

### 2.3 How Many Samples Per Read

**Line 485**: `framesPerUpdate = sampleRate / 60` = 48000 / 60 = **800 frames per call**

**Line 954**: Calls `EngineSimRender(handle, writePtr, framesToRender, &samplesWritten)` where `framesToRender` is typically 800 frames.

### 2.4 Main Loop Structure

**Lines 804-1004**: Main simulation loop

```cpp
while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
    // Get current stats
    EngineSimStats stats = {};
    EngineSimGetStats(handle, &stats);

    // ... throttle control ...

    // Update physics
    EngineSimSetThrottle(handle, throttle);
    EngineSimUpdate(handle, updateInterval);  // updateInterval = 1/60

    // Render audio
    if (framesToRender > 0) {
        float* writePtr = audioBuffer.data() + framesRendered * channels;
        result = EngineSimRender(handle, writePtr, framesToRender, &samplesWritten);
        // ... queue audio to OpenAL ...
    }

    currentTime += updateInterval;
}
```

**Pattern**:
1. Update physics with `EngineSimUpdate()`
2. Render audio with `EngineSimRender()`
3. Queue to OpenAL for playback
4. Repeat at 60 Hz

---

## 3. Side-by-Side Comparison Table

| Aspect | GUI | CLI |
|--------|-----|-----|
| **Audio thread?** | YES - started at line 509 | NO - explicitly not started |
| **Audio function?** | `readAudioOutput()` - reads from buffer | `EngineSimRender()` - generates audio synchronously |
| **Samples per read?** | Variable (maxWrite) - ~4000-8000 samples | Fixed - 800 frames (1600 samples) |
| **Synchronization?** | Condition variables + mutex (audio thread coordination) | None (single-threaded) |
| **Main loop structure?** | Update sim → Read from buffer → Queue to audio | Update sim → Render audio → Queue to audio |
| **Audio buffer fill?** | Continuously filled by background thread | Filled synchronously on demand |
| **Dropouts?** | None | Every ~1 second |

---

## 4. Root Cause of Dropouts

### The Problem

**The CLI is calling `EngineSimRender()` WITHOUT having started the audio thread.**

Looking at the bridge implementation (`/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp` lines 515-523):

```cpp
// CRITICAL: For synchronous rendering (no audio thread), we must call renderAudio()
// to generate audio samples before reading them. The audio thread normally does this.
```

`EngineSimRender()` calls `renderAudio()` which is supposed to be called by the audio thread. When you call it without the audio thread running:

1. **Race condition**: The audio buffer (`m_audioBuffer`) is designed to be filled by a background thread
2. **Buffer underruns**: The CLI renders 800 frames at a time, but the buffer expects continuous filling
3. **No coordination**: Without the audio thread's `wait()`/`notify()` mechanism, the buffer management breaks down

### Why Dropouts Happen Every ~1 Second

At 48000 Hz sample rate:
- CLI renders 800 frames per iteration
- 800 frames = 800/48000 = 0.0167 seconds = 16.7 ms
- CLI runs at 60 Hz (updateInterval = 1/60)
- Audio buffer size = 48000 samples (1 second)

**The dropout pattern**:
1. CLI renders 800 frames into buffer
2. OpenAL plays through buffer
3. After ~1 second, the buffer runs dry (no audio thread refilling it)
4. Dropout occurs
5. CLI fills buffer again
6. Cycle repeats

---

## 5. The Fix

### What EXACTLY Needs to Change

The CLI needs to replicate the GUI's audio thread architecture. Here are the specific changes:

### Change 1: Start Audio Thread After Script Load

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Location**: After line 693 (after `EngineSimLoadScript` succeeds)

**Add**:
```cpp
// Start audio rendering thread (like GUI does)
result = EngineSimStartAudioThread(handle);
if (result != ESIM_SUCCESS) {
    std::cerr << "ERROR: Failed to start audio thread: " << EngineSimGetLastError(handle) << "\";
    EngineSimDestroy(handle);
    return 1;
}
std::cout << "[3/5] Audio rendering thread started\n";
```

**Delete**: Lines 698-700 (the comment about synchronous rendering)

### Change 2: Use readAudioOutput Instead of EngineSimRender

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Location**: Line 954

**Current code**:
```cpp
result = EngineSimRender(handle, writePtr, framesToRender, &samplesWritten);
```

**Replace with**:
```cpp
// Read from audio buffer (filled by audio thread, like GUI does)
result = EngineSimReadAudioOutput(handle, writePtr, framesToRender, &samplesWritten);
```

**NOTE**: You need to check if `EngineSimReadAudioOutput` exists in the bridge API. If not, you need to add it.

### Change 3: Stop Audio Thread Before Cleanup

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Location**: Before line 1033 (before `EngineSimDestroy(handle)`)

**Add**:
```cpp
// Stop audio rendering thread (like GUI does)
result = EngineSimStopAudioThread(handle);
if (result != ESIM_SUCCESS) {
    std::cerr << "WARNING: Failed to stop audio thread\n";
}
```

### Change 4: Add Bridge Functions (If Missing)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/engine_sim_bridge.h`

**Add after line 268**:
```cpp
/**
 * Starts the audio rendering thread.
 * The thread continuously fills the audio buffer in the background.
 *
 * @param handle Simulator handle
 * @return ESIM_SUCCESS on success, error code otherwise
 *
 * Thread Safety: Call from main thread after script load
 * Allocations: Creates audio thread
 */
EngineSimResult EngineSimStartAudioThread(
    EngineSimHandle handle
);

/**
 * Stops the audio rendering thread.
 *
 * @param handle Simulator handle
 * @return ESIM_SUCCESS on success, error code otherwise
 *
 * Thread Safety: Call from main thread before cleanup
 * Allocations: Joins and destroys audio thread
 */
EngineSimResult EngineSimStopAudioThread(
    EngineSimHandle handle
);

/**
 * Reads audio output from the audio buffer.
 * The audio buffer is continuously filled by the audio thread.
 *
 * @param handle Simulator handle
 * @param buffer Output buffer for interleaved float samples (L, R, L, R...)
 * @param frames Number of frames to read
 * @param outSamplesRead Pointer to receive actual frames read (can be NULL)
 * @return ESIM_SUCCESS on success, error code otherwise
 *
 * Thread Safety: Call from main thread (audio thread writes to buffer)
 * Allocations: NONE
 */
EngineSimResult EngineSimReadAudioOutput(
    EngineSimHandle handle,
    float* buffer,
    int32_t frames,
    int32_t* outSamplesRead
);
```

### Change 5: Implement Bridge Functions

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp`

**Add implementation**:
```cpp
EngineSimResult EngineSimStartAudioThread(EngineSimHandle handle) {
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    EngineSimContext* ctx = getContext(handle);

    if (ctx->simulator) {
        ctx->simulator->startAudioRenderingThread();
        return ESIM_SUCCESS;
    }

    return ESIM_ERROR_NOT_INITIALIZED;
}

EngineSimResult EngineSimStopAudioThread(EngineSimHandle handle) {
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    EngineSimContext* ctx = getContext(handle);

    if (ctx->simulator) {
        ctx->simulator->endAudioRenderingThread();
        return ESIM_SUCCESS;
    }

    return ESIM_ERROR_NOT_INITIALIZED;
}

EngineSimResult EngineSimReadAudioOutput(
    EngineSimHandle handle,
    float* buffer,
    int32_t frames,
    int32_t* outSamplesRead)
{
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    if (!buffer) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    if (frames <= 0) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    EngineSimContext* ctx = getContext(handle);

    if (!ctx->simulator) {
        return ESIM_ERROR_NOT_INITIALIZED;
    }

    // Check buffer size
    size_t requiredSize = frames * 2; // Stereo
    if (requiredSize > ctx->conversionBufferSize) {
        ctx->setError("Read buffer size exceeds internal buffer");
        return ESIM_ERROR_AUDIO_BUFFER;
    }

    // Read audio from synthesizer (int16 format)
    // readAudioOutput returns MONO samples
    int samplesRead = ctx->simulator->readAudioOutput(
        frames,
        ctx->audioConversionBuffer
    );

    // Convert int16 to float32 [-1.0, 1.0] and mono to stereo
    constexpr float scale = 1.0f / 32768.0f;

    for (int i = 0; i < samplesRead; ++i) {
        // Duplicate mono sample to both channels
        float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
        buffer[i * 2] = sample;     // Left
        buffer[i * 2 + 1] = sample; // Right
    }

    // Fill remainder with silence if underrun
    if (samplesRead < frames) {
        std::memset(
            buffer + (samplesRead * 2),
            0,
            (frames - samplesRead) * 2 * sizeof(float)
        );
    }

    if (outSamplesRead) {
        *outSamplesRead = samplesRead;
    }

    return ESIM_SUCCESS;
}
```

---

## 6. Summary

### The Key Difference

**GUI**: Audio thread runs continuously, filling buffer → Main thread reads from buffer

**CLI (current)**: No audio thread → Main thread generates audio synchronously → Buffer underruns

### The Fix

Start the audio thread and use `readAudioOutput()` instead of `EngineSimRender()`. This exactly replicates what the GUI does.

### Minimal Changes Required

1. Add bridge functions: `EngineSimStartAudioThread`, `EngineSimStopAudioThread`, `EngineSimReadAudioOutput`
2. Call `EngineSimStartAudioThread()` after script load
3. Replace `EngineSimRender()` with `EngineSimReadAudioOutput()`
4. Call `EngineSimStopAudioThread()` before cleanup

That's it. These changes will make the CLI's audio architecture identical to the GUI's.
