# Audio Regression Fix - Post-ISimulator Refactor

**Date**: 2026-04-21
**Scope**: SyncPull silence dropouts, Threaded cursor-chasing lag and underruns
**Root cause**: Latency regulator in upstream `Simulator::startFrame()` over-produces audio steps when bypassed incorrectly, or under-produces when integer truncation isn't handled

---

## FIXED: SyncPull silence dropouts ("exhausted 3 retries, filling silence for N frames")

### Symptom
In SyncPull mode, `renderOnDemand()` returns fewer frames than requested (e.g. req=470 got=467).
The retry loop exhausts and fills remaining frames with silence, causing audible clicks/dropouts.

### Root cause (two parts)

**1. Integer truncation in simSteps calculation:**
```
simSteps = int(10000 * 470/44100) = int(106.58) = 106
Audio produced = 106 * (44100/10000) = 467 frames
Deficit = 470 - 467 = 3 frames -> filled with silence
```

**2. Retry loop's `update(1/sampleRate)` produced 0 steps:**
The retry called `simulator_->update(1.0 / 44100)` to nudge the simulation. With the
fixed-step approach using `static_cast<int>(simulationFrequency * dt)`:
```
steps = int(10000 * 0.0000227) = int(0.227) = 0
```
Zero steps = no audio = retry fails = silence fill.

### Fix
- `renderOnDemand()` uses `advanceFixedSteps(ceil=false)` — truncation is OK because the retry loop handles deficits
- `update()` uses `advanceFixedSteps(ceil=true)` — `ceil(0.227) = 1`, so the retry's tiny dt produces at least 1 step
- Retry loop in `SyncPullStrategy.cpp` restored to call `simulator_->update(1.0 / sampleRate_)` before each retry

### Files
- `src/strategy/SyncPullStrategy.cpp` — restored `update()` in retry loop, `sampleRate_` from config
- `include/strategy/SyncPullStrategy.h` — added `sampleRate_` member
- `include/simulator/SimulatorBase.h` — added `advanceFixedSteps()` shared helper with ceil parameter

---

## FIXED: Threaded cursor-chasing lag (latency creeps in over seconds)

### Symptom
In threaded mode, audio is responsive initially but lag gradually increases over several seconds.
The write pointer gets too far ahead of the read pointer in the circular buffer.

### Root cause (two parts)

**1. Latency regulator over-production in `update()`:**
`Simulator::startFrame()` (simulator.cpp:80-89) boosts step count by 10% when input latency
is below target. Over many frames, this 10% boost accumulates, producing more audio than consumed.
The cursor-chasing logic couldn't absorb the excess fast enough.

**2. Missing pre-fill drain in `start()`:**
The old C API's `EngineSimStartAudioThread` explicitly drained the Synthesizer's ~44100 sample
pre-fill before starting the audio thread. The new `BridgeSimulator::start()` skipped this drain,
leaving ~1 second of stale silence in the buffer.

### Fix
- `update()` uses `advanceFixedSteps(ceil=true)` — bypasses the regulator with deterministic steps.
  `ceil` provides slight over-production (~1 extra frame per call) which absorbs timing jitter
  without the regulator's aggressive 10% boost.
- `start()` calls `drainSynthesizerBuffer()` before starting the audio thread (matches old C API).
- `ThreadedStrategy` enhanced with `totalFramesWritten_`/`totalFramesRead_` tracking for lead-distance
  calculation, self-correction when lead exceeds 200ms, target lead reduced from 100ms to 50ms.

### Files
- `src/simulator/BridgeSimulator.cpp` — `update()` uses `advanceFixedSteps(ceil=true)`, `start()` drains
- `src/simulator/sine_wave_simulator.cpp` — same pattern
- `src/strategy/ThreadedStrategy.cpp` — lead-distance tracking and self-correction
- `include/strategy/ThreadedStrategy.h` — tracking members
- `include/simulator/SimulatorBase.h` — shared `advanceFixedSteps()`, `drainSynthesizerBuffer()`

---

## FIXED: DRY violations causing mode-specific bugs

### Problem
The fixed-step bypass pattern was duplicated in 4 places (BridgeSimulator and SineWaveSimulator,
each with `update()` and `renderOnDemand()`). `sampleRate_` and `simulationFrequency_` were also
duplicated. Different implementations drifted apart, causing bugs in one mode but not another.

### Fix
- `advanceFixedSteps(Simulator*, int freq, double dt, bool ceil)` added to `SimulatorBase` — single
  implementation shared by both simulators
- `drainSynthesizerBuffer(Simulator*)` added to `SimulatorBase` — shared drain logic
- `sampleRate_` and `simulationFrequency_` moved to `SimulatorBase` — removed from BridgeSimulator.h

### Key insight
The `ceil` parameter controls behavior per call-site:
- `ceil=true` for `update()` (Threaded mode): slight over-production prevents underruns, ensures
  at least 1 step for tiny dt values (SyncPull retry)
- `ceil=false` for `renderOnDemand()` (SyncPull mode): truncation is fine because the retry loop
  handles deficits via `update()` calls

---

## Architecture note: Latency regulator bypass

The upstream `Simulator::startFrame()` contains a latency regulator (simulator.cpp:80-89):

```cpp
if (m_synthesizer.getLatency() < targetLatency) {
    m_steps = static_cast<int>((m_steps + 1) * 1.1);  // BOOST by 10%
} else if (m_synthesizer.getLatency() > targetLatency) {
    m_steps = static_cast<int>((m_steps - 1) * 0.9);  // CUT by 10%
}
```

This regulator was designed for the upstream GUI application's audio pipeline. In our CLI/iOS
architecture with SyncPull and Threaded strategies, it causes problems:
- **SyncPull**: Regulator adds extra steps -> extra audio accumulates -> lag
- **Threaded**: Regulator over-produces -> cursor-chasing can't keep up -> lag creeps in

The `advanceFixedSteps()` helper bypasses this regulator by using a fixed for-loop instead of
`while(simulateStep())`. This gives deterministic step counts without the feedback loop.
