# Mock Engine-Sine Architecture

**Status:** v2.0 Implementation Complete
**Last Updated:** 2026-02-09
**Implementation:** implementation-lead

## Overview

The "engine-sine" mock implementation is a behavioral replica of engine-sim that outputs sine waves instead of engine physics audio. It replicates ALL engine-sim behaviors (threading, buffers, update cycle, synchronization) through the bridge API.

## Purpose

Test the entire infrastructure (bridge, threading, buffers, synchronization) without engine physics variability. If mock works perfectly, real engine-sim should work when integrated.

## Architecture Status

### Completed (All 5 Phases)
1. **Phase 1** - Core infrastructure (MockRingBuffer<T>, MockSynthesizer class)
2. **Phase 2** - Threading replication (cv0.wait() with exact predicate)
3. **Phase 3** - Update cycle (startFrame/simulateStep/endFrame)
4. **Phase 4** - Sine generation (phase-continuous, RPM-linked)
5. **Phase 5** - Testing & validation (build + runtime verified)

---

## Implementation Details

### Component Architecture

```
CLI (engine_sim_cli.cpp)
  │
  ├─ EngineSimUpdate(handle, dt)
  │   └─ MockEngineSimContext::startFrame(dt)
  │       ├─ Calculate simulation steps (latency-adjusted)
  │       └─ simulateStep() loop:
  │           ├─ updateEngineState(timestep)     [RPM physics]
  │           └─ writeToSynthesizer()            [sine → synthesizer input]
  │       endFrame()
  │           └─ synthesizer.endInputBlock()     [signals cv0]
  │
  ├─ EngineSimReadAudioBuffer(handle, buf, frames)
  │   └─ synthesizer.readAudioOutput(frames, int16_buf)
  │       └─ Reads from m_audioBuffer under m_lock0
  │       Convert int16 mono → float32 stereo
  │
  └─ [Audio Thread] (started by EngineSimStartAudioThread)
      └─ MockSynthesizer::audioRenderingThread()
          └─ renderAudio() loop:
              ├─ m_cv0.wait(predicate)           [EXACT real pattern]
              ├─ Read from m_inputChannel
              ├─ Set m_processed = true
              ├─ Unlock m_lock0
              ├─ Convert float→int16, write to m_audioBuffer
              └─ m_cv0.notify_one()
```

### Phase 1: Core Infrastructure

#### MockRingBuffer<T>
Replicates engine-sim's `RingBuffer<T>` with matching API:
- `write(T value)` - Write single item
- `read(int n, T* dest)` - Read n items without removing (peek)
- `readAndRemove(int n, T* dest)` - Read and consume n items
- `removeBeginning(int n)` - Remove n items from head
- `size()` / `capacity()` / `writeIndex()`

Two instances used:
- `MockRingBuffer<float>` - Input channel (simulation → audio thread)
- `MockRingBuffer<int16_t>` - Audio output buffer (audio thread → CLI)

#### MockSynthesizer
Class replicating `Synthesizer` from `synthesizer.h/cpp`:
- Same threading primitives (`m_lock0`, `m_inputLock`, `m_cv0`)
- Same flags (`m_processed`, `m_run`)
- Same methods (`startAudioRenderingThread`, `endAudioRenderingThread`, `endInputBlock`, `waitProcessed`, `readAudioOutput`)

**Critical C++ detail:** MockRingBuffer and MockSynthesizer are defined OUTSIDE `extern "C"` because templates require C++ linkage. Only the C API functions are inside `extern "C"`.

### Phase 2: Threading Model

The threading model is an EXACT replica of `synthesizer.cpp:229-270`:

```cpp
// Real engine-sim (synthesizer.cpp:239-244):
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannels[0].data.size() > 0
        && m_audioBuffer.size() < 2000;
    return !m_run || (inputAvailable && !m_processed);
});

// Mock (identical predicate):
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannel.size() > 0
        && m_audioBuffer.size() < 2000;
    return !m_run || (inputAvailable && !m_processed);
});
```

**Key threading flow:**
1. Audio thread calls `renderAudio()`, acquires `m_lock0`, waits on `m_cv0`
2. Main thread runs simulation steps, feeds input via `writeInput()`
3. At end of frame, `endInputBlock()` sets `m_processed = false`, notifies `m_cv0`
4. Audio thread wakes up, reads input, sets `m_processed = true`, unlocks
5. Audio thread processes samples (float→int16), writes to `m_audioBuffer`
6. Audio thread notifies `m_cv0` again, re-enters wait

**Differences from v1.0 (old mock):**
- v1.0 used `std::timed_mutex` with `try_lock_for()` - wrong pattern
- v1.0 had separate `audioFrameReadyCond` - not in real engine-sim
- v2.0 uses `std::mutex` + `std::condition_variable` - exact match

### Phase 3: Update Cycle

Replicates `Simulator::startFrame/simulateStep/endFrame`:

#### startFrame(dt)
```
1. Record simulation start time
2. Set input sample rate: simulationFrequency × simulationSpeed
3. Calculate steps: round((dt × speed) / timestep)
4. Latency-based adjustment:
   - If latency < target: steps = (steps + 1) × 1.1
   - If latency > target: steps = (steps - 1) × 0.9
```

#### simulateStep()
```
1. If currentIteration >= steps: return false (frame done)
2. updateEngineState(timestep)  [RPM physics at 10kHz]
3. writeToSynthesizer()         [generate sine, feed to input]
4. ++currentIteration
5. return true
```

#### endFrame()
```
1. synthesizer.endInputBlock()  [signals audio thread via cv0]
```

**EngineSimUpdate() calls these in sequence - matches real bridge exactly:**
```cpp
ctx->startFrame(deltaTime);
while (ctx->simulateStep()) { }
ctx->endFrame();
```

### Phase 4: Sine Generation

#### writeToSynthesizer()
Mock equivalent of `PistonEngineSimulator::writeToSynthesizer()`:
- Maps RPM to frequency: `(RPM / 600) × 100 Hz`
  - 600 RPM → 100 Hz
  - 3000 RPM → 500 Hz
  - 6000 RPM → 1000 Hz
- Phase increment: `2π × frequency / simulationFrequency`
- Phase-continuous: phase accumulates across calls, wraps at 2π
- Output scaled by `config.volume`

**Bugs fixed from v1.0:**
- v1.0 had double phase advance (phase incremented twice per sample)
- v1.0 called `updateEngineState()` in both audio thread AND `EngineSimUpdate()` (double update)
- v2.0 only updates engine state inside `simulateStep()` - no double updates

#### Data Flow
```
writeToSynthesizer() → float sample → synthesizer.writeInput(sample)
                                         ↓
                                    [input ring buffer]
                                         ↓
                         cv0 signal → audio thread reads
                                         ↓
                                    float→int16 conversion
                                         ↓
                                    [audio ring buffer]
                                         ↓
                   readAudioOutput() → int16→float, mono→stereo
                                         ↓
                                    CLI gets stereo float32
```

### Phase 5: Testing Results

**Build:** `cmake .. -DUSE_MOCK_ENGINE_SIM=ON` compiles cleanly
**WAV mode:** `./engine-sim-cli --sine --duration 2` runs successfully
**Audio mode:** `./engine-sim-cli --sine --play --duration 3` produces audio

**Verified:**
- RPM ramp: 0 → 192 → 560 → ... → 4440 RPM (warmup), then ramp phase
- Frequency mapping: 100Hz at 600RPM, scales linearly
- No buffer starvation messages
- Clean audio output

---

## Behavioral Equivalence with Real Engine-Sim

| Aspect | Real Engine-Sim | Mock Engine-Sine v2.0 |
|--------|----------------|----------------------|
| Threading | cv0.wait() with processed flag | Identical |
| Input buffer | RingBuffer<float> | MockRingBuffer<float> |
| Output buffer | RingBuffer<int16_t> | MockRingBuffer<int16_t> |
| Update cycle | startFrame/simulateStep/endFrame | Identical |
| Bridge conversion | int16 mono → float32 stereo | Identical |
| readAudioOutput | Locks m_lock0, reads from buffer | Identical |
| endInputBlock | Removes processed, sets !processed, notifies cv0 | Identical |
| Audio content | Engine exhaust convolution | Sine wave (RPM-linked) |

---

## NO SLEEP Core Directive (2026-02-17)

### Synchronization: Condition Variables, Not Sleep

This mock implementation **strictly follows the NO SLEEP core directive**:

**Proper synchronization (from synthesizer.cpp:239-244):**
```cpp
m_cv0.wait(lk0, [this] {
    const bool inputAvailable =
        m_inputChannel.size() > 0
        && m_audioBuffer.size() < 2000;
    return !m_run || (inputAvailable && !m_processed);
});
```

**Why this is correct:**
- Predicate-based: Thread only wakes when actual conditions are met
- No wasted sleep: Immediate wake when `notify_one()` is called
- Deadlock-safe: Predicate is checked after wake (spurious wakeups handled)
- Deterministic: Behavior depends on state, not wall-clock time

**Anti-pattern we never use:**
```cpp
// WRONG - Anti-pattern, violates core directive
while (m_inputChannel.size() == 0) {
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
}
```

### NO SLEEP in Mock Architecture

The mock architecture uses **NO sleep()** for synchronization:

1. **Audio thread waiting:** Uses `cv0.wait()` with predicate, not sleep polling
2. **Main thread signaling:** Uses `cv0.notify_one()`, not sleep delays
3. **Buffer access:** Uses mutex locks, not sleep-based "race condition fixes"
4. **Update coordination:** Uses `endInputBlock()` notification, not sleep timing

This matches real engine-sim's threading pattern exactly - **no sleep-based synchronization**.

### Why Mock Proves Proper Sync Works

The mock implementation demonstrates that:
1. **cv0.wait() pattern** works reliably for audio thread synchronization
2. **Producer-consumer** coordination works without sleep
3. **Buffer management** works with condition variables, not timing hacks
4. **Zero latency** is possible without sleep-based synchronization

If mock uses sleep(), it would work on some systems and fail on others - making it an **invalid test bed** for real engine-sim.

### Core Directive Summary

**NO SLEEP ANYWHERE - ABSOLUTE CORE DIRECTIVE**

For all production code (mock, real, bridge, CLI):
- Use `cv0.wait()` for audio thread waiting
- Use `cv0.notify_one()` for signaling
- Use mutexes for buffer access
- Use hardware callbacks for audio playback
- Use cursor-chasing for real-time streaming
- Use adaptive timing for rate control

See MEMORY.md "NO SLEEP Core Directive" section for comprehensive guidance.

---

## Reference Implementation

**Based on:**
- `engine-sim-bridge/engine-sim/src/synthesizer.cpp` (threading, cv0.wait)
- `engine-sim-bridge/engine-sim/src/simulator.cpp` (startFrame/simulateStep/endFrame)
- `engine-sim-bridge/engine-sim/src/piston_engine_simulator.cpp` (writeToSynthesizer)
- `engine-sim-bridge/src/engine_sim_bridge.cpp` (C API, int16→float conversion)

---

*Implementation completed: 2026-02-09*
*Version: mock-engine-sim/2.0.0*
