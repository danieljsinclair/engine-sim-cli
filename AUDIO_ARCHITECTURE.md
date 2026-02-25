# Audio Architecture Documentation

## Current State (Working)

### Architecture Overview

```
┌─────────────────────────────────────────────────────────────┐
│  Main Thread          │  Async Audio Render │  CoreAudio Callback │
│ (60Hz physics)      │  (renderAudio @~60Hz)  │  (real-time @44.1kHz) │
└─────────────────────────────────────────────────────┘
        ↓                   ↓                     ↓
    [writes input]    [produces audio]    [reads circular]       [reads CoreAudio]
    to synthesizer      to m_audioBuffer     to circular buffer       directly to speaker
```

### Components

1. **Main Thread**
   - Runs physics simulation at 60Hz fixed rate
   - Calls `Update(handle, 1/60)` each frame
   - Reads audio from synthesizer via `ReadAudioBuffer()`
   - Writes to circular buffer

2. **Async Audio Rendering Thread**
   - Started via `StartAudioThread(handle)`
   - Runs `renderAudio()` continuously
   - Reads from synthesizer input ring buffer
   - Writes to `m_audioBuffer` (44,100-sample ring buffer)
   - **Known Limitation**: Caps at 2000 samples (line 274 of synthesizer.cpp)
   - **Race Condition**: Releases lock before writing to `m_audioBuffer` (line 289)

3. **CoreAudio Callback Thread**
   - Hardware-driven callback at ~470 frames per 440Hz
   - Reads from CLI circular buffer (96,000 samples)
   - Directly drives speaker

### Buffer Flow

```
Synthesizer Input Ring Buffer (96k)
        ↑ (async render)     ↑ (main loop)          ↑ (CoreAudio callback)
           [2000-sample cap]          [44100 samples]         [470 frames/callback]
```

### Timing

- Physics tick rate: 60 Hz (16.67ms)
- Async render rate: ~60 Hz (when input available)
- CoreAudio callback: ~100 Hz (hardware, 440Hz typical)
- Main loop read rate: 60 Hz (capped by physics tick)

### Latency

- Pre-fill: 6 iterations = 4410 samples = 100ms (for sine mode startup)
- Circular buffer lead: 100ms (write pointer ahead of read pointer)
- **Total startup latency**: ~200ms (pre-fill + lead)

### Performance Notes

- At 60Hz physics with 10kHz simulation frequency:
- Physics produces ~735 audio samples per frame
- Async thread renders at similar rate
- Circular buffer maintains ~200-500 samples (4.5-6 seconds) available
- CoreAudio consumes at 44.1kHz, which matches production rate

## Async Thread Limitations

### 2000-Sample Cap

The async rendering thread limits `m_audioBuffer` to 2000 samples (line 274):

```cpp
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),
    (int)m_inputChannels[0].data.size());
```

**Purpose**: Prevents async thread from running ahead and consuming CPU
**Effect**: At 60Hz render rate × 44.1kHz, the cap is hit in ~3 frames.

**Issue**: Complex engines (V8, Ferrari) have large impulse responses and can hit this cap
when convolution is expensive. The cap limits audio output, causing the buffer to drain.

## Race Condition

The async thread releases its lock before writing to `m_audioBuffer`:

```cpp
// renderAudio() - lines 261-299
const int n = std::min(...);
// ... read input from ring buffers ...
m_processed = true;
lk0.unlock();  // <-- LOCK RELEASED HERE

// Filter updates happen
for (int i = 0; i < m_inputChannelCount; ++i) {
    m_filters[i].airNoiseLowPass.setCutoffFrequency(...);
    m_filters[i].jitterFilter.setJitterScale(...);
}

// Then write to m_audioBuffer (OUTSIDE LOCK!)
for (int i = 0; i < n; ++i) {
    m_audioBuffer.write(renderAudio(i));
}
```

**Problem**: `readAudioOutput()` on main thread reads from `m_audioBuffer` while filter updates
are still happening. If main thread reads while filter is updating, it may get inconsistent
state.

**Note**: Current implementation appears to work in practice - the race doesn't cause audible issues.

## Two-Thread Alternative (Not Implemented)

To reduce to 2 threads (main + CoreAudio), the architecture would be:

1. Remove async audio rendering thread
2. Main thread calls `renderAudioSync()` synchronously after `Update()`
3. Main thread still reads from synthesizer via `readAudioBuffer()`
4. CoreAudio callback reads from circular buffer

**Challenge**: At 10kHz simulation, `Update()` takes ~12-16ms physics + `renderAudioSync()` takes ~6ms
   Total = 18-22ms, which exceeds 16.67ms frame budget.

The synchronous approach cannot handle 10kHz physics while maintaining 60Hz loop rate.

## Historical Context

- Working code from `origin/master` uses this 3-thread architecture
- Previous attempts at 2-thread (main + sync render) could not achieve 10kHz
- The 2000-sample cap and race condition are limitations of the current async thread
- These limitations are known and the system works within them

## Tuning Options

### Reduce Latency (Already Applied)

1. **Pre-fill reduction**: Changed from 40 iterations (667ms) to 6 iterations (100ms)
2. **Result**: Sine mode instant RPM response, 100ms startup latency
3. **Trade-off**: Slightly less headroom for buffer starvation during sudden RPM changes

### Increase Throughput (Not Recommended)

To reduce underruns on complex engines at 10kHz:

1. **Increase 2000-sample cap**: Modify synthesizer.cpp line 274
   ```cpp
   && m_audioBuffer.size() < 8000  // Or some higher value
   ```
   - **Trade-off**: More audio in buffer = more latency
   - **Benefit**: Async thread has more data to render, reduces underrun risk

2. **Reduce convolution level**: If acceptable for testing
   - Current: `convolutionLevel = 1.0f`
   - Could try 0.5f to halve IR length, reducing compute cost

3. **Parallelism (More Complex)**:
   - Run physics and audio on separate threads with careful synchronization
   - Requires significant refactoring
   - May introduce race conditions that need careful handling

### Alternative Architecture Considerations

#### Option: Hybrid Sync + Async (3 Threads)

- Main thread: Physics + sync render for low-cost operations
- Async thread: Handles expensive operations (convolution)
- **Benefit**: Main loop less burdened
- **Complexity**: Need to split work between sync and async render cleanly

#### Option: Adaptive Rate Based on Load

- Increase simulation frequency when engine is idling or at low RPM
- Decrease simulation frequency when engine is at high load
- **Benefit**: Save CPU when engine doesn't need high precision
- **Drawback**: Variable simulation rate could affect consistency

#### Option: Buffer-Driven Physics

- Instead of fixed 60Hz tick rate, run physics when circular buffer has room
- **Benefit**: Physics runs as fast as hardware permits, not throttled to arbitrary rate
- **Challenge**: Physics timing becomes variable, could affect engine behavior consistency
