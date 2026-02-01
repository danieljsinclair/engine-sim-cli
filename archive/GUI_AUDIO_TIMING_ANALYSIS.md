# GUI Audio Timing Analysis

**MISSION**: Understand how the GUI plays audio without delay and avoids buffer exhaustion.

**EXECUTIVE SUMMARY**:
- GUI uses hardware audio feedback (`GetCurrentWritePosition()`) to synchronize writes
- CLI has no feedback mechanism, reads blindly without tracking playback position
- GUI maintains a "lead" of 0.05-0.1 seconds ahead of playback cursor
- CLI reads until buffer exhaustion, causing delays and silence gaps

---

## 1. GUI's Audio Playback System

### 1.1 Audio API: Windows Audio (ysAudioSource)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Lines 176-184**: Audio initialization
```cpp
ysAudioParameters params;
params.m_bitsPerSample = 16;
params.m_channelCount = 1;  // MONO, not stereo!
params.m_sampleRate = 44100;
m_outputAudioBuffer =
    m_engine.GetAudioDevice()->CreateBuffer(&params, 44100);

m_audioSource = m_engine.GetAudioDevice()->CreateSource(m_outputAudioBuffer);
m_audioSource->SetMode((m_simulator->getEngine() != nullptr)
    ? ysAudioSource::Mode::Loop
    : ysAudioSource::Mode::Stop);
```

**KEY FINDING**: GUI uses **MONO** audio, not stereo. This is critical for buffer size calculations.

### 1.2 Buffer Synchronization (The Magic)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Lines 253-271**: Synchronization logic
```cpp
// Get current playback position from audio hardware
const SampleOffset safeWritePosition = m_audioSource->GetCurrentWritePosition();
const SampleOffset writePosition = m_audioBuffer.m_writePointer;

// Calculate target write position: 0.1 seconds AHEAD of playback cursor
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);

SampleOffset currentLead = m_audioBuffer.offsetDelta(safeWritePosition, writePosition);
SampleOffset newLead = m_audioBuffer.offsetDelta(safeWritePosition, targetWritePosition);

// Safety: if lead is too large (>0.5s), reset to 0.05s
if (currentLead > 44100 * 0.5) {
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
    currentLead = m_audioBuffer.offsetDelta(safeWritePosition, m_audioBuffer.m_writePointer);
    maxWrite = m_audioBuffer.offsetDelta(m_audioBuffer.m_writePointer, targetWritePosition);
}

// Safety: if we're already ahead of target, don't write
if (currentLead > newLead) {
    maxWrite = 0;
}
```

**HOW IT WORKS**:
1. **Hardware Feedback**: `GetCurrentWritePosition()` returns the current playback cursor position from the audio hardware
2. **Target Lead**: GUI aims to keep write pointer 0.1 seconds (4410 samples) ahead of playback cursor
3. **Max Write Calculation**: `maxWrite` = how many samples can be written before reaching target lead
4. **Overflow Protection**: If lead > 0.5s, reset to 0.05s to prevent desynchronization
5. **Underrun Protection**: If already ahead of target, write 0 samples (wait for playback to catch up)

**Lines 273-289**: Reading from audio thread buffer
```cpp
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
```

**KEY FINDING**: GUI reads **up to `maxWrite` samples**, not a fixed amount. `maxWrite` is dynamically calculated based on playback position.

### 1.3 Writing to Hardware Buffer

**Lines 291-306**: Lock hardware buffer, write samples, unlock
```cpp
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
```

**HOW IT WORKS**:
1. **LockBufferSegment**: Lock region of hardware buffer at write pointer position
2. **copyBuffer**: Copy from internal buffer to hardware buffer (handles wraparound)
3. **UnlockBufferSegments**: Unlock hardware buffer so audio hardware can play it
4. **commitBlock**: Advance write pointer by `readSamples`

---

## 2. Audio Buffer Management

### 2.1 Circular Buffer Implementation

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/audio_buffer.h`

**Lines 30-38**: `offsetDelta` - calculates distance between two buffer positions
```cpp
inline int offsetDelta(int offset0, int offset1) const {
    if (offset1 == offset0) return 0;
    else if (offset1 < offset0) {
        return (m_bufferSize - offset0) + offset1;  // Wraparound case
    }
    else {
        return offset1 - offset0;  // Normal case
    }
}
```

**PURPOSE**: Calculates how many samples can be written from position A to position B in a circular buffer.

### 2.2 Buffer Initialization

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

**Line 169-170**:
```cpp
m_audioBuffer.initialize(44100, 44100);  // Sample rate, buffer size (1 second)
m_audioBuffer.m_writePointer = (int)(44100 * 0.1);  // Start 0.1s ahead
```

**Buffer Size**: 44100 samples = 1 second @ 44.1kHz (MONO)

---

## 3. Why GUI Avoids Buffer Exhaustion

### 3.1 Synchronization Mechanism

**GUI has FEEDBACK from audio hardware**:
- `GetCurrentWritePosition()` returns the current playback cursor
- GUI knows exactly where the audio hardware is playing
- GUI calculates how many samples to write based on playback position

**Formula**:
```
maxWrite = targetWritePosition - writePosition
where:
  targetWritePosition = playbackCursor + 0.1 seconds (target lead)
  writePosition = current write pointer
```

### 3.2 Protection Mechanisms

**1. Overflow Protection** (lines 263-267):
```cpp
if (currentLead > 44100 * 0.5) {
    // Reset to 0.05s lead if buffer is too full
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
}
```
- If buffer has >0.5s of audio, reset to 0.05s lead
- Prevents writing too far ahead, which would cause stale audio

**2. Underrun Protection** (lines 269-271):
```cpp
if (currentLead > newLead) {
    maxWrite = 0;  // Don't write, wait for playback to catch up
}
```
- If already ahead of target, write 0 samples
- Prevents overwriting unplayed audio

**3. Graceful Underrun Handling** (synthesizer.cpp line 144-154):
```cpp
const int newDataLength = m_audioBuffer.size();
if (newDataLength >= samples) {
    m_audioBuffer.readAndRemove(samples, buffer);
}
else {
    m_audioBuffer.readAndRemove(newDataLength, buffer);
    memset(buffer + newDataLength, 0, sizeof(int16_t) * ((size_t)samples - newDataLength));
}
```
- If not enough samples available, read what's available and fill rest with silence
- Never blocks, never crashes

---

## 4. CLI's Problems

### 4.1 CLI Has NO Hardware Feedback

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**CLI does NOT call**:
- `GetCurrentWritePosition()` - doesn't exist in OpenAL without extensions
- Any equivalent playback position tracking
- Any dynamic `maxWrite` calculation

**CLI reads fixed amount** (lines 1078-1100):
```cpp
const int chunkFrames = 200;  // Small chunks to avoid over-demanding the audio thread
int framesRemaining = framesToRender;
int framesReadTotal = 0;

while (framesRemaining > 0) {
    int framesToReadNow = std::min(chunkFrames, framesRemaining);
    int samplesWritten = 0;
    float* writePtr = audioBuffer.data() + (framesRendered + framesReadTotal) * channels;

    result = EngineSimReadAudioBuffer(handle, writePtr, framesToReadNow, &samplesWritten);

    if (result == ESIM_SUCCESS && samplesWritten > 0) {
        framesReadTotal += samplesWritten;
        framesRemaining -= samplesWritten;
    } else if (samplesWritten == 0) {
        // No samples available - don't spin, just use what we got
        break;
    } else {
        // Error - stop reading
        break;
    }
}
```

**PROBLEM**: CLI reads until it gets 0 samples, but doesn't know:
- Where the playback cursor is
- How much audio is actually queued
- When to stop reading to avoid underrun

### 4.2 OpenAL Doesn't Provide `GetCurrentWritePosition()`

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**CLI uses OpenAL** (lines 173-189):
```cpp
// Check how many buffers have been processed
ALint processed;
alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

// Check how many buffers are currently queued
ALint queued;
alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);
```

**OpenAL provides**:
- `AL_BUFFERS_PROCESSED`: how many buffers finished playing
- `AL_BUFFERS_QUEUED`: how many buffers are queued

**OpenAL does NOT provide**:
- `GetCurrentWritePosition()`: exact playback cursor position
- Fine-grained buffer level feedback
- Sub-buffer position tracking

**RESULT**: CLI cannot implement the GUI's synchronization mechanism.

### 4.3 CLI's Latency Calculation

**CLI accumulates audio before queuing** (lines 1112-1134):
```cpp
if (audioPlayer && !args.outputWav) {
    // Check if we've accumulated enough frames for a chunk
    if (framesRendered >= chunkSize) {
        // Calculate how many complete chunks we can queue
        int chunksToQueue = framesRendered / chunkSize;

        for (int i = 0; i < chunksToQueue; i++) {
            int chunkOffset = i * chunkSize;
            if (!audioPlayer->playBuffer(audioBuffer.data() + chunkOffset * channels,
                                        chunkSize, sampleRate)) {
            }
        }

        // Keep any remaining frames that don't form a complete chunk
        int remainingFrames = framesRendered % chunkSize;
        if (remainingFrames > 0) {
            // Move remaining frames to start of buffer
            std::memmove(audioBuffer.data(),
                       audioBuffer.data() + (framesRendered - remainingFrames) * channels,
                       remainingFrames * channels * sizeof(float));
        }
        framesRendered = remainingFrames;
    }
}
```

**PROBLEM**:
- CLI accumulates `chunkSize` (48000 frames = 1 second) before queuing
- This adds 1 second of latency before audio starts playing
- GUI has no such accumulation - writes directly to hardware buffer

---

## 5. Synchronization Comparison

### 5.1 GUI Synchronization

```
┌─────────────────────────────────────────────────────────────┐
│                    GUI AUDIO BUFFER (1 second)              │
├─────────────────────────────────────────────────────────────┤
│  ┌─────┐  ┌─────┐  ┌─────┐  ┌─────┐                        │
│  │ 0.1s│  │ 0.1s│  │ 0.1s│  │ 0.1s│  ...                    │
│  └─────┘  └─────┘  └─────┘  └─────┘                        │
│    ▲        ▲        ▲        ▲                             │
│    │        │        │        │                             │
│  Write Ptr  │        │    Playback Cursor                   │
│             │        │      (hardware feedback)             │
│        Target Lead   │                                     │
│        (0.1s ahead)  │                                     │
└─────────────────────────────────────────────────────────────┘

Synchronization:
1. Get playback cursor from hardware
2. Calculate target: cursor + 0.1s
3. Calculate maxWrite: target - writePointer
4. Read up to maxWrite samples
5. Write to hardware buffer at writePointer
6. Advance writePointer by samples written

Result: NEVER exhausts buffer, NEVER overflows
```

### 5.2 CLI Synchronization

```
┌─────────────────────────────────────────────────────────────┐
│                  CLI AUDIO BUFFER (unknown)                 │
├─────────────────────────────────────────────────────────────┤
│  ┌─────────────────┐  ┌─────────────────┐                  │
│  │   Chunk 1       │  │   Chunk 2       │  ...              │
│  │   (1 second)    │  │   (1 second)    │                   │
│  └─────────────────┘  └─────────────────┘                   │
│         ▲                                                    │
│         │                                                    │
│     Accumulate                                               │
│     until full                                               │
│     then queue                                               │
└─────────────────────────────────────────────────────────────┘

Synchronization:
1. NO hardware feedback
2. Read fixed chunk size (200 frames)
3. Accumulate until 1 second buffered
4. Queue to OpenAL
5. Hope for the best

Result: Underruns, delays, silence gaps
```

---

## 6. Latency Comparison

### 6.1 GUI Latency

**From engine rev → sound output**:
- **Target lead**: 0.1 seconds (4410 samples)
- **Actual latency**: ~0.05-0.15 seconds (varies with frame rate)

**Why low latency**:
1. Write pointer tracks 0.1s ahead of playback cursor
2. Writes directly to hardware buffer (no accumulation)
3. Dynamic `maxWrite` ensures minimal buffering

### 6.2 CLI Latency

**From engine rev → sound output**:
- **Chunk accumulation**: 1 second (48000 samples)
- **OpenAL queue latency**: ~0.1-0.2 seconds (2 buffers)
- **Total latency**: ~1.1-1.2 seconds

**Why high latency**:
1. Accumulates 1 second before queuing (lines 1114-1134)
2. OpenAL buffers add additional latency
3. No feedback to reduce latency dynamically

### 6.3 Root Cause of CLI Delays

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Line 560**: Chunk size = 1 second
```cpp
const int chunkSize = sampleRate;  // Queue in 1-second chunks
```

**Line 1114**: Wait for full chunk before queuing
```cpp
if (framesRendered >= chunkSize) {  // Wait for 1 second
```

**RESULT**: CLI adds 1 second of latency that GUI doesn't have.

---

## 7. Why CLI Has Delays That GUI Doesn't

### 7.1 Missing Hardware Feedback

**GUI**:
- Has `GetCurrentWritePosition()` from audio hardware
- Can track exact playback cursor position
- Can calculate exact `maxWrite` to maintain target lead

**CLI**:
- OpenAL doesn't provide `GetCurrentWritePosition()`
- Can only query `AL_BUFFERS_PROCESSED` and `AL_BUFFERS_QUEUED`
- Cannot calculate exact `maxWrite`
- Must guess or use fixed chunk sizes

### 7.2 Accumulation Latency

**GUI**:
- Writes directly to hardware buffer every frame
- No accumulation
- Latency = target lead (0.1s)

**CLI**:
- Accumulates 1 second of audio before queuing
- Latency = accumulation (1s) + OpenAL queue (0.1-0.2s)
- Total = 1.1-1.2 seconds

### 7.3 Buffer Exhaustion

**GUI**:
- Calculates `maxWrite` based on playback position
- Never reads more than available space in hardware buffer
- Never exhausts buffer

**CLI**:
- Reads fixed amount (200 frames) without checking buffer level
- May read more than OpenAL can queue
- May exhaust buffer if audio thread can't keep up

### 7.4 Format Mismatch

**GUI**:
- MONO audio (1 channel)
- int16 format
- 44100 Hz

**CLI**:
- STEREO audio (2 channels)
- float32 format (with int16 fallback)
- 48000 Hz

**IMPACT**: CLI processes 2x the data (stereo vs mono), increasing CPU load and buffer pressure.

---

## 8. Key Differences Summary

| Aspect | GUI | CLI |
|--------|-----|-----|
| **Audio API** | Windows Audio (ysAudioSource) | OpenAL |
| **Hardware Feedback** | `GetCurrentWritePosition()` | None |
| **Buffer Size** | 1 second (44100 samples) | Unknown (OpenAL managed) |
| **Write Calculation** | Dynamic `maxWrite` based on playback cursor | Fixed chunk size (200 frames) |
| **Target Lead** | 0.1 seconds ahead of cursor | None |
| **Accumulation** | None (direct write) | 1 second before queuing |
| **Latency** | 0.05-0.15 seconds | 1.1-1.2 seconds |
| **Channels** | MONO (1 channel) | STEREO (2 channels) |
| **Sample Rate** | 44100 Hz | 48000 Hz |
| **Format** | int16 | float32 (or int16) |
| **Exhaustion** | Never (dynamic maxWrite) | Possible (fixed reads) |

---

## 9. Conclusion

**GUI plays audio without delay because**:
1. **Hardware Feedback**: `GetCurrentWritePosition()` provides exact playback cursor
2. **Dynamic Sizing**: `maxWrite` calculated based on playback position
3. **No Accumulation**: Writes directly to hardware buffer every frame
4. **Target Lead**: Maintains 0.1s lead, minimal latency
5. **Protection**: Overflow/underrun protection prevents issues

**CLI has delays because**:
1. **No Hardware Feedback**: OpenAL doesn't provide playback cursor position
2. **Fixed Sizing**: Reads fixed 200-frame chunks without buffer awareness
3. **Accumulation**: Waits for 1 second before queuing to OpenAL
4. **No Target Lead**: No concept of maintaining lead ahead of playback
5. **Format Mismatch**: Stereo + float32 = 2x data processing vs GUI

**THE CRITICAL DIFFERENCE**: GUI has a feedback loop from audio hardware that CLI cannot replicate with OpenAL.
