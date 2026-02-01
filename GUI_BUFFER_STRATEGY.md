# GUI Buffer Strategy Investigation

## Executive Summary

**The GUI uses a CIRCULAR BUFFER (RingBuffer) with a fixed capacity of 44,100 samples.**

This is NOT a "larger buffer" strategy - it's a classic circular buffer implementation with modulo arithmetic for wraparound.

---

## 1. GUI Buffer Type

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h`

**Line 107**:
```cpp
RingBuffer<int16_t> m_audioBuffer;
```

**Fact**: The audio buffer is explicitly typed as `RingBuffer<int16_t>`, not a vector or other container.

---

## 2. GUI Buffer Allocation

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h`

**Lines 34-40** (Parameters struct):
```cpp
struct Parameters {
    int inputChannelCount = 1;
    int inputBufferSize = 1024;
    int audioBufferSize = 44100;  // <-- FIXED SIZE: 44,100 samples
    float inputSampleRate = 10000;
    float audioSampleRate = 44100;
    AudioParameters initialAudioParameters;
};
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Line 51** (Initialization):
```cpp
m_audioBuffer.initialize(p.audioBufferSize);  // Capacity = 44,100
```

**Lines 81-83** (Pre-filling with zeros):
```cpp
for (int i = 0; i < m_audioBufferSize; ++i) {
    m_audioBuffer.write(0);
}
```

**Fact**: Buffer capacity is fixed at 44,100 samples (1 second at 44.1kHz).

---

## 3. GUI Buffer Management (Circular Buffer Behavior)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/ring_buffer.h`

### Write Implementation (Lines 40-46):
```cpp
inline void write(T_Data data) {
    m_buffer[m_writeIndex] = data;

    if (++m_writeIndex >= m_capacity) {
        m_writeIndex = 0;  // <-- WRAPAROUND with modulo
    }
}
```

### Read Implementation (Lines 94-113):
```cpp
inline void readAndRemove(size_t n, T_Data *target) {
    if (m_start + n < m_capacity) {
        memcpy(target, m_buffer + m_start, n * sizeof(T_Data));
    }
    else {
        // Handle wraparound: copy end, then beginning
        memcpy(
            target,
            m_buffer + m_start,
            (m_capacity - m_start) * sizeof(T_Data));
        memcpy(
            target + (m_capacity - m_start),
            m_buffer,
            (n - (m_capacity - m_start)) * sizeof(T_Data));
    }

    m_start += n;
    if (m_start >= m_capacity) {
        m_start -= m_capacity;  // <-- WRAPAROUND with modulo
    }
}
```

### Size Calculation (Lines 130-134):
```cpp
inline size_t size() const {
    return (m_writeIndex < m_start)
        ? m_writeIndex + (m_capacity - m_start)  // Wrapped case
        : m_writeIndex - m_start;                 // Normal case
}
```

**Fact**: This is classic circular buffer behavior with explicit wraparound handling.

---

## 4. GUI Read/Write Pattern

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

### readAudioOutput (Lines 141-159):
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

**Key observation**: Returns only what's available - underflow results in silence, not blocking.

### renderAudio Thread (Lines 222-256):
```cpp
void Synthesizer::renderAudio() {
    std::unique_lock<std::mutex> lk0(m_lock0);

    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;  // <-- THROTTLE at 2000 samples
        return !m_run || (inputAvailable && !m_processed);
    });

    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),
        (int)m_inputChannels[0].data.size());

    // ... render and write ...
    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));  // <-- Circular write
    }
}
```

**Key observation**: Audio thread fills up to 2000 samples, then waits. Consumer reads continuously.

---

## 5. CLI vs GUI Comparison

### CLI AudioBuffer (Lines 71-140 in engine_sim_cli.cpp):
```cpp
class AudioBuffer {
private:
    std::vector<float> m_buffer;
    size_t m_writePos = 0;
    size_t m_readPos = 0;
    size_t m_capacity = 0;
    const size_t m_targetLead = 4800;  // 100ms at 48kHz stereo
    std::mutex m_mutex;

public:
    void write(const float* samples, size_t count) {
        // ... wraparound write ...
        m_writePos = (m_writePos + count) % m_capacity;  // <-- MODULO
    }

    size_t read(float* output, size_t count) {
        // ... wraparound read ...
        m_readPos = (m_readPos + readNow) % m_capacity;  // <-- MODULO
        return readNow;
    }
};
```

**Fact**: CLI also uses a circular buffer with modulo arithmetic!

---

## 6. The Answer

**Which approach does GUI use?**

**CIRCULAR BUFFER with modulo arithmetic.**

### Evidence:
1. **Type**: `RingBuffer<int16_t>` (synthesizer.h:107)
2. **Capacity**: Fixed at 44,100 samples (synthesizer.h:36)
3. **Wraparound**: Explicit modulo in write (ring_buffer.h:44) and read (ring_buffer.h:111)
4. **Size calculation**: Handles wrapped case explicitly (ring_buffer.h:131-134)

### Why it works (no dropouts):
- **Audio thread** (producer) fills up to 2000 samples, then waits
- **Main thread** (consumer) reads continuously, even if buffer underruns (returns silence)
- **No blocking** on consumer side - if buffer is empty, it returns zeros
- **Producer throttling** prevents overflow by waiting when buffer > 2000 samples

### Key insight:
The GUI achieves zero delay NOT through a "larger buffer" but through:
1. **Circular buffer** with fixed capacity (44,100 samples)
2. **Non-blocking consumer** that returns zeros on underrun
3. **Throttled producer** that waits when buffer is full enough

This is different from CLI's intermediate buffer strategy (48,000 samples, 100ms target lead), but BOTH use circular buffers.

---

## Summary

**GUI Strategy**: Circular buffer (RingBuffer<int16_t>, 44,100 samples)

**CLI Strategy**: Circular buffer (AudioBuffer with std::vector, 192,000 samples for intermediate buffer)

**Both use circular buffers with modulo arithmetic.**

The difference is NOT the buffer type, but:
- **Capacity**: GUI 44,100 vs CLI intermediate 192,000
- **Throttling**: GUI waits at 2000 samples, CLI targets 100ms lead
- **Consumer behavior**: GUI returns zeros on underrun, CLI blocks/returns partial

---

## Conclusion

**The GUI uses a circular buffer (RingBuffer) with modulo arithmetic, NOT a larger fixed buffer.**

The misconception that GUI uses a "larger buffer" is incorrect. Both GUI and CLI use circular buffers. The GUI's success comes from:
1. Non-blocking consumer (returns zeros, doesn't wait)
2. Throttled producer (waits when buffer has enough data)
3. Proper wraparound handling in the RingBuffer implementation
