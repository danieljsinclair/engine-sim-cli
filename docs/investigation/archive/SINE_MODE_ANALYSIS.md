# Sine Mode Pitch Jump Analysis

## Investigation Summary

This analysis investigates variable pitch jumps reported in sine mode at specific RPMs (3295, 3952, 5350).

## Key Findings

### 1. **Sine Mode Implementation Issues**

The sine mode has several fundamental problems:

**Problem 1: Engine Warmup Failure**
- Sine mode relies on engine simulation to generate RPM values
- The engine warmup phase is not working properly
- RPM values are essentially zero (e.g., 8.76411e-18 RPM)
- This results in inaudible frequencies (e.g., 1.46068e-18 Hz)

**Problem 2: Buffer Underruns**
- Constant buffer underruns (hundreds detected)
- Circular buffer is consistently empty
- Audio callback receives no data, resulting in silence

### 2. **Hybrid Mode (Engine Simulation) Behavior**

When using actual engine simulation:

**Buffer Management**
- Buffer underruns still occur but are less severe
- The circular buffer has synchronization issues between write and read pointers

**RPM Control Oscillation**
- RPM oscillates around target values
- Throttle spikes cause RPM to overshoot/undershoot
- Example: Target 3295 RPM, actual RPM varies from 2800-4200

**No Pitch Jumps Observed**
- In engine simulation mode, no pitch jumps were detected
- Audio generation is continuous and stable

### 3. **Root Cause Analysis**

**Primary Issue: Sine Mode Engine Simulation**
- The sine mode attempts to run engine simulation but fails to start the engine properly
- Zero RPM values lead to zero frequency sine waves
- No audible audio is generated

**Secondary Issue: Buffer Synchronization**
- The circular buffer implementation has race conditions
- Write and read pointers can get out of sync
- Buffer underruns occur when read pointer outpaces write pointer

**Timing Issues**
- Physics updates take longer than target (10-16ms vs 16.67ms target)
- This causes inconsistent audio generation timing

## Code Analysis

### Sine Mode (Lines 798-1045)

```cpp
// Problem: Engine not starting properly
EngineSimStats stats = {};
EngineSimGetStats(handle, &stats);
double frequency = (stats.currentRPM / 600.0) * 100.0;  // Results in ~0 Hz
```

### Circular Buffer Implementation (Lines 295-329)

```cpp
// Write operation
int newWritePtr = (writePtr + frameCount) % bufferSize;
context->writePointer.store(newWritePtr);

// Read operation (in callback)
int newReadPtr = (readPtr + framesToRead) % bufferSize;
ctx->readPointer.store(newReadPtr);
```

The issue is that the read pointer advances only the frames actually read, but if there's an underrun, the read pointer doesn't advance enough, creating a gap that the next write can't fill.

### Audio Callback (Lines 385-483)

```cpp
// Underrun handling
if (framesToRead < static_cast<int>(framesToWrite)) {
    ctx->underrunCount.fetch_add(1);
    // Fill with silence
    std::memset(data + framesToRead * 2, 0, silenceFrames * 2 * sizeof(float));
}
```

The silence filling creates discontinuities in the audio stream.

## Recommendations

### Immediate Fixes

1. **Fix Sine Mode Engine Startup**
   - Ensure engine starts properly in sine mode
   - Add fallback frequency if RPM is zero
   - Implement proper warmup sequence

2. **Buffer Management Improvements**
   - Implement proper buffer synchronization
   - Add buffer pre-filling
   - Use mutex protection for thread safety

3. **Timing Optimization**
   - Reduce physics update duration
   - Implement proper frame timing
   - Add jitter compensation

### Long-term Solutions

1. **Audio Architecture Redesign**
   - Implement double-buffering for audio
   - Add proper queue management
   - Implement real-time prioritization

2. **Sine Mode Separation**
   - Create independent sine wave generator
   - Remove dependency on engine simulation
   - Implement proper frequency ramping

### Testing Strategy

1. **Unit Tests for Audio System**
   - Test circular buffer synchronization
   - Test underrun scenarios
   - Test timing accuracy

2. **Integration Tests**
   - Test sine mode at various RPMs
   - Test buffer management under load
   - Test timing accuracy

## Conclusion

The variable pitch jumps reported in sine mode are likely caused by:
1. Engine simulation failure resulting in zero RPM values
2. Buffer underruns causing audio discontinuities
3. Timing inconsistencies in the audio generation pipeline

The primary issue is that sine mode is not functioning as intended due to engine simulation problems, rather than actual pitch jumps in a properly functioning system.