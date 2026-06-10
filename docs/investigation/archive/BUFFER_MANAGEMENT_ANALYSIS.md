# GUI Buffer Management Analysis

## Executive Summary

The GUI achieves consistent audio playback without dropouts through a **dual-buffer architecture** with hardware feedback. The key insight is that the GUI uses an **intermediate AudioBuffer** that acts as a software-managed circular buffer between the audio thread (producer) and hardware audio (consumer), with hardware position feedback to maintain optimal buffer levels.

## 1. GUI's Buffer Architecture

### 1.1 Buffer Types and Sizes

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Lines 169-177**:
```cpp
m_audioBuffer.initialize(44100, 44100);  // 1 second buffer
m_audioBuffer.m_writePointer = (int)(44100 * 0.1);  // Start at 100ms offset

ysAudioParameters params;
params.m_bitsPerSample = 16;
params.m_channelCount = 1;
params.m_sampleRate = 44100;
m_outputAudioBuffer = m_engine.GetAudioDevice()->CreateBuffer(&params, 44100);
```

**Architecture**:
- **Intermediate AudioBuffer** (`m_audioBuffer`): Custom circular buffer, 44100 samples (1 second at 44.1kHz)
- **Hardware Buffer** (`m_outputAudioBuffer`): DirectSound/Windows Audio buffer, 44100 samples
- **Write Pointer**: Starts at 4410 samples (100ms) to maintain lead distance

### 1.2 AudioBuffer Class Implementation

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/audio_buffer.h`

**Key Methods** (lines 40-54):
```cpp
// Write a single sample at offset + index
inline void writeSample(int16_t sample, int offset, int index = 0) {
    m_samples[getBufferIndex(offset, index)] = sample;
}

// Calculate buffer index with wraparound
inline int getBufferIndex(int offset, int index = 0) const {
    return (((offset + index) % m_bufferSize) + m_bufferSize) % m_bufferSize;
}

// Advance write pointer after writing block
inline void commitBlock(int length) {
    m_writePointer = getBufferIndex(m_writePointer, length);
}
```

**Thread Safety**: **NONE** - AudioBuffer has no mutexes or atomic operations. It relies on:
1. Single-threaded access in `process()` (main thread)
2. Lock-free RingBuffer in synthesizer (audio thread)

## 2. Write Pattern (Producer Side)

### 2.1 Hardware Feedback Loop

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Lines 253-271** (CRITICAL):
```cpp
// Get current hardware playback position (feedback from audio device)
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
const SampleOffset writePosition = m_audioBuffer.m_writePointer;

// Calculate target: stay 100ms ahead of hardware
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);

// Measure current lead distance
SampleOffset currentLead = m_audioBuffer.offsetDelta(safeWritePosition, writePosition);
SampleOffset newLead = m_audioBuffer.offsetDelta(safeWritePosition, targetWritePosition);

// Safety: if too far ahead (over 500ms), reset to 50ms lead
if (currentLead > 44100 * 0.5) {
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
    currentLead = m_audioBuffer.offsetDelta(safeWritePosition, m_audioBuffer.m_writePointer);
    maxWrite = m_audioBuffer.offsetDelta(m_audioBuffer.m_writePointer, targetWritePosition);
}

// Prevent underrun
if (currentLead > newLead) {
    maxWrite = 0;
}
```

**Key Insight**: GUI uses **hardware position feedback** to:
1. Calculate exact distance ahead of playback cursor
2. Maintain 100ms lead distance (target)
3. Correct if buffer gets too full (>500ms)
4. Prevent writing past playback position (underrun protection)

### 2.2 Reading from Synthesizer

**Line 274**:
```cpp
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
```

**Synthesizer Implementation** (`synthesizer.cpp` lines 141-159):
```cpp
int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    std::lock_guard<std::mutex> lock(m_lock0);  // THREAD SAFE

    const int newDataLength = m_audioBuffer.size();
    if (newDataLength >= samples) {
        m_audioBuffer.readAndRemove(samples, buffer);
    }
    else {
        // Partial read: return what's available, zero-fill rest
        m_audioBuffer.readAndRemove(newDataLength, buffer);
        memset(buffer + newDataLength, 0, sizeof(int16_t) * ((size_t)samples - newDataLength));
    }

    const int samplesConsumed = std::min(samples, newDataLength);
    return samplesConsumed;
}
```

**Thread Safety**:
- Synthesizer's RingBuffer is protected by `m_lock0` mutex
- Audio thread writes to RingBuffer
- Main thread reads from RingBuffer (locked)

### 2.3 Writing to Intermediate Buffer

**Lines 276-287**:
```cpp
for (SampleOffset i = 0; i < (SampleOffset)readSamples && i < maxWrite; ++i) {
    const int16_t sample = samples[i];

    // Visualization (4:1 downsampling)
    if (m_oscillatorSampleOffset % 4 == 0) {
        m_oscCluster->getAudioWaveformOscilloscope()->addDataPoint(
            m_oscillatorSampleOffset,
            sample / (float)(INT16_MAX));
    }

    // Write to intermediate buffer
    m_audioBuffer.writeSample(sample, m_audioBuffer.m_writePointer, (int)i);

    m_oscillatorSampleOffset = (m_oscillatorSampleOffset + 1) % (44100 / 10);
}
```

## 3. Read Pattern (Consumer Side)

### 3.1 Hardware Buffer Transfer

**Lines 291-306** (CRITICAL):
```cpp
if (readSamples > 0) {
    SampleOffset size0, size1;
    void *data0, *data1;

    // Lock hardware buffer segments (handles wraparound)
    m_audioSource->LockBufferSegment(
        m_audioBuffer.m_writePointer, readSamples,
        &data0, &size0, &data1, &size1);

    // Copy from intermediate buffer to hardware buffer (first segment)
    m_audioBuffer.copyBuffer(
        reinterpret_cast<int16_t *>(data0),
        m_audioBuffer.m_writePointer,
        size0);

    // Copy from intermediate buffer to hardware buffer (second segment, if wrapped)
    m_audioBuffer.copyBuffer(
        reinterpret_cast<int16_t *>(data1),
        m_audioBuffer.getBufferIndex(m_audioBuffer.m_writePointer, size0),
        size1);

    // Unlock hardware buffer
    m_audioSource->UnlockBufferSegments(data0, size0, data1, size1);

    // Advance write pointer
    m_audioBuffer.commitBlock(readSamples);
}
```

**Key Insight**: Hardware buffer transfer is **atomic** (Lock/Unlock) and handles **wraparound** automatically (returns up to 2 segments).

### 3.2 Buffer Copy with Wraparound

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/audio_buffer.h`

**Lines 56-68**:
```cpp
inline void copyBuffer(int16_t *dest, int offset, int length) {
    const int start = getBufferIndex(offset, 0);
    const int end = getBufferIndex(offset, length);

    if (start == end) return;
    else if (start < end) {
        // No wraparound: single memcpy
        memcpy(dest, m_samples + start, length * sizeof(int16_t));
    }
    else {
        // Wraparound: two memcpy operations
        memcpy(dest, m_samples + start, ((size_t)m_bufferSize - start) * sizeof(int16_t));
        memcpy(dest + m_bufferSize - start, m_samples, end * sizeof(int16_t));
    }
}
```

## 4. Thread Safety Mechanisms

### 4.1 Audio Thread (Producer)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Lines 215-256**:
```cpp
void Synthesizer::audioRenderingThread() {
    while (m_run) {
        renderAudio();
    }
}

void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    // Wait for input data and buffer space
    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;  // Max 2000 samples
        return !m_run || (inputAvailable && !m_processed);
    });

    // Read from input buffers
    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),
        (int)m_inputChannels[0].data.size());

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();  // Unlock before expensive rendering

    // Render audio (outside lock)
    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    m_cv0.notify_one();
}
```

**Thread Safety**:
- `m_lock0` mutex protects RingBuffer access
- Condition variable `m_cv0` coordinates producer/consumer
- Lock released during rendering (allows parallelism)

### 4.2 Main Thread (Consumer)

**Lines 141-159** (from `readAudioOutput`):
```cpp
int Synthesizer::readAudioOutput(int samples, int16_t *buffer) {
    std::lock_guard<std::mutex> lock(m_lock0);  // LOCKED READ

    const int newDataLength = m_audioBuffer.size();
    if (newDataLength >= samples) {
        m_audioBuffer.readAndRemove(samples, buffer);
    }
    else {
        m_audioBuffer.readAndRemove(newDataLength, buffer);
        memset(buffer + newDataLength, 0, sizeof(int16_t) * ((size_t)samples - newDataLength));
    }

    return std::min(samples, newDataLength);
}
```

**Thread Safety**:
- Main thread holds lock while reading from RingBuffer
- Audio thread waits on condition variable when buffer full
- Zero-fills on underrun (graceful degradation)

### 4.3 Intermediate Buffer (No Locks)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Lines 274-306** (main thread only):
```cpp
// ALL ON MAIN THREAD - NO LOCKS NEEDED
const int readSamples = m_simulator->readAudioOutput(maxWrite, samples);
for (SampleOffset i = 0; i < (SampleOffset)readSamples && i < maxWrite; ++i) {
    m_audioBuffer.writeSample(sample, m_audioBuffer.m_writePointer, (int)i);
}
m_audioSource->LockBufferSegment(...);
m_audioBuffer.copyBuffer(...);
m_audioSource->UnlockBufferSegments(...);
m_audioBuffer.commitBlock(readSamples);
```

**Key Insight**: Intermediate buffer (`m_audioBuffer`) is **single-threaded** (main thread only). Thread safety is handled in:
1. Synthesizer's RingBuffer (between audio thread and main thread)
2. Hardware buffer (via LockBufferSegment/UnlockBufferSegments)

## 5. What Makes GUI's Audio Consistent

### 5.1 The Three Buffer System

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  Audio Thread   │     │   Main Thread    │     │   Hardware      │
│  (Producer)     │     │  (Consumer)      │     │   (Consumer)    │
└────────┬────────┘     └────────┬─────────┘     └────────┬────────┘
         │                       │                        │
         │  RingBuffer<int16_t>  │                        │
         │  (locked, condvar)    │                        │
         │                       │                        │
         └──────────────────────►│  AudioBuffer           │
                                 │  (single-threaded)     │
                                 │                        │
                                 └───────────────────────►│  DirectSound
                                                           │  (locked)
                                                           ▼
                                                         Speakers
```

**Three-Stage Architecture**:
1. **Stage 1**: Audio Thread → RingBuffer (thread-safe, locked)
2. **Stage 2**: Main Thread → AudioBuffer (single-threaded, hardware feedback)
3. **Stage 3**: Main Thread → Hardware Buffer (atomic, wrapped)

### 5.2 Hardware Feedback is Critical

**Line 253**:
```cpp
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
```

**Why This Matters**:
- Hardware buffer playback position is **ground truth**
- Allows precise calculation of lead distance
- Prevents buffer underrun (writing too little)
- Prevents buffer overrun (writing too much)
- Enables self-correction when buffer drifts

### 5.3 Buffer Lead Management

**Target**: 100ms (4410 samples) ahead of hardware
**Maximum**: 500ms (22050 samples) before correction
**Minimum**: Dynamic (prevents writing past playback cursor)

**Lines 260-271**:
```cpp
SampleOffset currentLead = m_audioBuffer.offsetDelta(safeWritePosition, writePosition);
SampleOffset newLead = m_audioBuffer.offsetDelta(safeWritePosition, targetWritePosition);

// Correct if too far ahead
if (currentLead > 44100 * 0.5) {
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
}

// Prevent underrun
if (currentLead > newLead) {
    maxWrite = 0;
}
```

**Key Insight**: Buffer has **self-regulating lead distance** that:
1. Maintains 100ms cushion against underrun
2. Corrects drift when buffer gets too full
3. Prevents writing past playback cursor (would cause glitch)

### 5.4 Graceful Degradation

**Synthesizer** (`synthesizer.cpp` lines 148-154):
```cpp
if (newDataLength >= samples) {
    m_audioBuffer.readAndRemove(samples, buffer);
}
else {
    // Partial read: return what's available, zero-fill rest
    m_audioBuffer.readAndRemove(newDataLength, buffer);
    memset(buffer + newDataLength, 0, sizeof(int16_t) * ((size_t)samples - newDataLength));
}
```

**Key Insight**: System handles **partial reads gracefully**:
- Returns available samples
- Zero-fills remainder (silent, not glitch)
- No exceptions or crashes on underrun

## 6. Cross-Platform Considerations

### 6.1 Windows (GUI Implementation)

**Audio System**: DirectSound via `ysAudioSource`
**Features**:
- `GetCurrentWritePosition()`: Hardware feedback
- `LockBufferSegment()`: Atomic buffer access with wraparound
- `UnlockBufferSegments()`: Commit writes
- Circular buffer hardware support

### 6.2 macOS/iOS/ES32 (Required for CLI)

**Challenge**: No DirectSound on these platforms

**Options**:

#### Option 1: Core Audio (macOS/iOS)
```cpp
// AudioQueue API
AudioQueueRef queue;
AudioQueueBufferRef buffers[3];

// Get current playback position
AudioQueueGetCurrentTime(queue, NULL, &timestamp, NULL);

// No direct buffer locking - must use callback-based architecture
```

**Limitation**: No `GetCurrentWritePosition()` equivalent
**Workaround**: Track write position manually, use timestamp for sync

#### Option 2: OpenAL (Cross-Platform)
```cpp
// Already used in CLI
ALuint source;
ALint processed;

alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
alSourceUnqueueBuffers(source, 1, &buffer);
// ... fill buffer ...
alSourceQueueBuffers(source, 1, &buffer);
```

**Limitation**: No position feedback, only buffer count
**Workaround**: Track position based on sample rate and time

#### Option 3: PortAudio (Cross-Platform)
```cpp
// Callback-based
int paCallback(const void *input, void *output,
               unsigned long frameCount,
               const PaStreamCallbackTimeInfo* timeInfo,
               PaStreamCallbackFlags statusFlags) {
    // timeInfo->outputBufferDacTime provides timing
}
```

**Advantage**: `PaStreamCallbackTimeInfo` provides timing info
**Limitation**: No direct buffer position feedback

### 6.3 Recommended Approach for CLI

**Strategy**: Emulate GUI's three-stage architecture

```
┌─────────────────┐     ┌──────────────────┐     ┌─────────────────┐
│  Audio Thread   │     │   Main Thread    │     │   PortAudio     │
│  (Producer)     │     │  (Consumer)      │     │   Callback      │
└────────┬────────┘     └────────┬─────────┘     └────────┬────────┘
         │                       │                        │
         │  RingBuffer<int16_t>  │                        │
         │  (locked, condvar)    │                        │
         │                       │                        │
         └──────────────────────►│  AudioBuffer           │
                                 │  (emulated)            │
                                 │                        │
                                 └───────────────────────►│  PA Stream
                                                           │  callback
                                                           ▼
                                                         Speakers
```

**Implementation**:
1. **Keep existing**: RingBuffer in synthesizer (thread-safe)
2. **Add**: Emulated AudioBuffer in main thread
3. **Track**: Estimated hardware position using `PaStreamCallbackTimeInfo`
4. **Maintain**: 100ms lead distance like GUI

## 7. CLI Current Implementation (Analysis)

### 7.1 Current Architecture

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Lines 1071-1137**:
```cpp
// Read in smaller chunks and handle partial reads gracefully
const int chunkFrames = 200;  // Small chunks to avoid over-demanding the audio thread
int framesRemaining = framesToRender;
int framesReadTotal = 0;

while (framesRemaining > 0) {
    int framesToReadNow = std::min(chunkFrames, framesRemaining);
    int samplesWritten = 0;
    float* writePtr = audioBuffer.data() + (framesRendered + framesReadTotal) * channels;

    // Read from synthesizer
    result = EngineSimReadAudioBuffer(handle, writePtr, framesToReadNow, &samplesWritten);

    if (result == ESIM_SUCCESS && samplesWritten > 0) {
        framesReadTotal += samplesWritten;
        framesRemaining -= samplesWritten;
    } else if (samplesWritten == 0) {
        // No samples available - don't spin
        break;
    } else {
        break;
    }
}

// Queue audio in chunks for real-time playback
if (audioPlayer && !args.outputWav) {
    if (framesRendered >= chunkSize) {
        // Calculate how many complete chunks we can queue
        int chunksToQueue = framesRendered / chunkSize;

        for (int i = 0; i < chunksToQueue; i++) {
            int chunkOffset = i * chunkSize;
            audioPlayer->playBuffer(...);
        }

        // Keep any remaining frames
        int remainingFrames = framesRendered % chunkSize;
        if (remainingFrames > 0) {
            std::memmove(audioBuffer.data(), ..., ...);
        }
        framesRendered = remainingFrames;
    }
}
```

**Problems**:
1. **No hardware feedback**: Can't track playback position
2. **Chunk-based**: Uses fixed chunks, not dynamic lead distance
3. **OpenAL queuing**: No position feedback, only buffer count
4. **No intermediate buffer**: Direct synthesizer → hardware

### 7.2 Why CLI Has Dropouts

**Root Cause**: **Missing intermediate buffer + hardware feedback**

**Evidence**:
1. CLI reads directly from synthesizer to OpenAL buffer
2. OpenAL doesn't provide `GetCurrentWritePosition()` equivalent
3. CLI can't maintain optimal lead distance
4. CLI can't detect impending underrun until it happens
5. CLI can't self-correct buffer drift

## 8. How to Replicate GUI Consistency

### 8.1 Required Components

**Must Have**:
1. **Intermediate AudioBuffer**: Between synthesizer and hardware
2. **Hardware Position Tracking**: Via timing or API
3. **Dynamic Lead Management**: Maintain 100ms cushion
4. **Self-Correction**: Detect and fix buffer drift

### 8.2 Implementation Plan

#### Step 1: Add AudioBuffer to CLI
```cpp
class AudioBuffer {
    int16_t* m_samples;
    int m_bufferSize;      // 44100
    int m_writePointer;    // Track position
    int m_playbackPointer; // Estimated playback position

    void initialize(int sampleRate, int bufferSize);
    void writeSample(int16_t sample, int offset, int index);
    void copyBuffer(int16_t* dest, int offset, int length);
    int getBufferIndex(int offset, int index);
    void commitBlock(int length);
};
```

#### Step 2: Track Hardware Position
```cpp
// In PortAudio callback
int paCallback(...) {
    // Update estimated playback position
    playbackPosition += frameCount;

    // Get current time for sync
    double currentTime = timeInfo->outputBufferDacTime;
    double estimatedLatency = currentTime - lastCallbackTime;

    return paContinue;
}
```

#### Step 3: Implement GUI's Write Logic
```cpp
// In main thread
const SampleOffset safeWritePosition = getEstimatedPlaybackPosition();
const SampleOffset writePosition = audioBuffer.getWritePointer();

// Calculate target: 100ms ahead
SampleOffset targetWritePosition = audioBuffer.getBufferIndex(
    safeWritePosition,
    (int)(44100 * 0.1)
);

SampleOffset maxWrite = audioBuffer.offsetDelta(writePosition, targetWritePosition);
SampleOffset currentLead = audioBuffer.offsetDelta(safeWritePosition, writePosition);

// Correct if too far ahead
if (currentLead > 44100 * 0.5) {
    audioBuffer.setWritePointer(
        audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05))
    );
}

// Read from synthesizer
int readSamples = simulator->readAudioOutput(maxWrite, samples);

// Write to intermediate buffer
for (int i = 0; i < readSamples; ++i) {
    audioBuffer.writeSample(samples[i], audioBuffer.getWritePointer(), i);
}

// Copy to hardware (via callback or queue)
audioBuffer.copyBuffer(hardwareBuffer, audioBuffer.getWritePointer(), readSamples);
audioBuffer.commitBlock(readSamples);
```

#### Step 4: PortAudio Callback (Consumer)
```cpp
int paCallback(
    const void* input,
    void* output,
    unsigned long frameCount,
    const PaStreamCallbackTimeInfo* timeInfo,
    PaStreamCallbackFlags statusFlags
) {
    float** out = (float**)output;

    // Read from intermediate buffer
    int samplesToRead = frameCount;
    int samplesAvailable = audioBuffer.getAvailableSamples(playbackPosition);
    int samplesToCopy = std::min(samplesToRead, samplesAvailable);

    // Copy samples
    for (int i = 0; i < samplesToCopy; ++i) {
        out[0][i] = audioBuffer.readSample(playbackPosition + i) / 32768.0f;
        out[1][i] = out[0][i]; // Stereo
    }

    // Zero-fill remainder
    for (int i = samplesToCopy; i < frameCount; ++i) {
        out[0][i] = 0.0f;
        out[1][i] = 0.0f;
    }

    // Update playback position
    playbackPosition = audioBuffer.getBufferIndex(playbackPosition, samplesToCopy);

    return paContinue;
}
```

### 8.3 Cross-Platform Compatibility

**macOS/iOS**:
- Use **PortAudio** with callback architecture
- Track position using `PaStreamCallbackTimeInfo`
- Emulate `GetCurrentWritePosition()` using timing

**ES32 (Embedded)**:
- Use **dedicated audio thread** with DMA
- Hardware provides position via DMA register
- Same three-stage architecture applies

**Windows**:
- Can use **DirectSound** (like GUI) or PortAudio
- DirectSound has native `GetCurrentWritePosition()`
- PortAudio approach works across all platforms

## 9. Key Takeaways

### 9.1 What GUI Does Right

1. **Hardware Feedback**: Uses `GetCurrentWritePosition()` for ground truth
2. **Intermediate Buffer**: Decouples audio thread from hardware
3. **Dynamic Lead**: Maintains 100ms cushion with self-correction
4. **Graceful Degradation**: Zero-fills on underrun, doesn't crash
5. **Thread Safety**: Locks only where needed (RingBuffer), single-threaded elsewhere

### 9.2 What CLI Missing

1. **No Hardware Feedback**: Can't track playback position
2. **No Intermediate Buffer**: Direct path causes timing issues
3. **Fixed Chunking**: Can't adapt to buffer conditions
4. **No Self-Correction**: Buffer drift not detected/corrected

### 9.3 Why This Matters

**Dropouts occur when**:
- Buffer underrun (playback catches up to write position)
- Buffer overrun (write position laps playback position)
- Timing drift (no feedback to correct)

**GUI avoids dropouts by**:
- Maintaining 100ms lead (cushion against underrun)
- Using hardware feedback (accurate position tracking)
- Self-correcting drift (keeps buffer in optimal range)
- Graceful degradation (zero-fill instead of glitch)

## 10. Conclusion

The GUI's buffer management system achieves consistency through a **three-stage architecture** with hardware feedback. The intermediate AudioBuffer is the key innovation, allowing precise lead distance management and self-correction.

To replicate this on macOS/iOS/ES32:
1. Add intermediate AudioBuffer to CLI
2. Implement hardware position tracking (via timing or API)
3. Port GUI's write logic with dynamic lead management
4. Use callback-based audio system (PortAudio, Core Audio, etc.)

**The critical missing piece in CLI is the intermediate buffer with hardware feedback**. Adding this will eliminate dropouts by maintaining optimal buffer levels like the GUI.

---

**Files Referenced**:
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/audio_buffer.h`
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/ring_buffer.h`
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
