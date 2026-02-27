# Engine Sim CLI - NEW_ARCHITECTURE_APPROACH

## Project Status
**Date:** 2025-02-25
**State:** Needs team investigation - audio quality issues with real engine mode

## Architecture Overview

```
Main Loop (60Hz)               Async Thread               CoreAudio Callback (~100Hz)
     |                               |                            |
     v                               v                            v
writeInput()  ------------->  renderAudio()  ---------->  ReadAudioBuffer()
(1024 samples)                  (targetBufferLevel)                 (reads from m_audioBuffer)
     |                               |                            |
     +----------------------------->  |  <-------------------------+
    input ring buffer          m_audioBuffer                  (stereo float)
       (1024 samples)         (22050 samples)
                                  |
```

## Changes Made (2025-02-25)

### 1. Removed CLI Circular Buffer
**Before:** AudioUnitContext had `circularBuffer`, `writePointer`, `readPointer`
- **After:** Direct callback reads from synthesizer's `m_audioBuffer` via `api.ReadAudioBuffer()`

### 2. Removed 2000-Sample Cap
**Before:** `renderAudio()` had `&& m_audioBuffer.size() < 2000` condition
- **After:** Use full buffer capacity, no cap
- **Result:** Underruns reduced from 2000+ to 1-3

### 3. Fixed Async Thread Deadlock
**Problem:** `!m_processed` check prevented thread from waking when input available
- **Fix:** Removed `!m_processed` from wait condition in `m_cv0.wait()`
- **Result:** Thread wakes whenever input is available

### 4. Dynamic Target Buffer Level
**Single exhaust:** 5000 samples (~113ms latency)
- **Dual exhaust (ferrari):** 10000 samples (~227ms latency)
- **Formula:** `5000 + (channelCount - 1) * 5000`
- **Purpose:** Prevent overflow, reduce latency vs. prevent "jumping tracks"

### 5. Added Ring Buffer Overflow Protection
**Problem:** `write()` method wraps buffer without checking for overflow
- **Solution:** `writeSafe()` returns `false` if buffer would overflow
- **Implementation:**
```cpp
// Before:
m_audioBuffer.write(renderAudio(i));

// After:
for (int i = 0; i < n; ++i) {
    // Write to buffer and increment, wrap if needed
    if (!m_audioBuffer.writeSafe(renderAudio(i))) {
        // Buffer full - stop writing to prevent overflow
        n = i;  // Update to reflect actual samples written
        break;
    }
}
```

## Current Test Results

| Mode | Underruns | RPM Tracking | Audio Quality |
|-------|---------|-------------------|-----------|
| Sine | 0 | Smooth | Perfect | Pure math |
| Engine | 1-3 | Delayed/Choppy | Crackly/Jumpy | Complex pipeline |

## Known Issues (Require Team Investigation)

### 1. Real Engine Audio Quality - Crackling/Jumping
**Symptoms:**
- Audio sounds like "dirty old vinyl record" or "skipping tracks on CD"
- RPM display shows values correctly for 1-2 seconds, then jumps to 192-284 RPM
- Console output stops updating after iteration 50
- Underruns: 0 (but user reports crackling)

**Possible Causes:**
1. **Filter state corruption** - Convolution/leveling filters may have state issues when RPM changes rapidly
2. **Sample rate conversion** - 10kHz → 44.1kHz resampling with phase interpolation may have discontinuities
3. **Thread race conditions** - Though locks are in place, timing issues may occur
4. **Buffer latency** - 500ms (single) or 227ms (dual) buffer delay may cause RPM lag

### 2. Console Output Not Updating
**Symptoms:**
- RPM display shows: "[   1 RPM] [Throttle: 70%] [Flow: 0.00 m3/s]" but stays same after iteration 50
- Progress output stops after iteration 50
- Early diagnostic: "[iter 1] dt=0.100ms underruns=0" continues but no visual feedback

**Possible Cause:**
- `displayProgress()` only updates every 10% progress OR after iteration 50
- Interactive mode might have different update frequency

### 3. Submodule Build System
**Status:** Submodule needs manual initialization before clean build
**Error:** `engine-sim/` directory empty, synthesizer source files not being built

### Files Modified (2025-02-25)
- `engine-sim-bridge/engine-sim/src/synthesizer.cpp`: Removed 2000 cap, added `targetBufferLevel`, added `writeSafe()` calls
- `engine-sim-bridge/engine-sim/src/synthesizer.h`: Added `writeSafe()` method
- `src/engine_sim_cli.cpp`: Audio buffer reduced from 96000 to 22050 samples
- `engine-sim-bridge/include/ring_buffer.h`: Added `writeSafe()` method

### Current Data Flow
```
Input Ring Buffer (1024 samples)
     ↓ [Main loop writes 1000 samples at 60Hz]
     ↓ [Async thread reads from input buffer at 60Hz]
     ↓ [Async thread writes to m_audioBuffer (no cap)]
     ↓ [Callback reads from m_audioBuffer at ~100Hz]
```

## Why Sine Mode Works But Real Engine Doesn't

### Key Difference

**Sine Mode:**
```cpp
// generateAudio() - pure math
double frequency = (directRPM / 600.0) * 100.0;
double phaseIncrement = (2.0 * M_PI * frequency) / AudioLoopConfig::SAMPLE_RATE;
for (int i = 0; i < frames; i++) {
    float sample = static_cast<float>(std::sin(currentPhase) * 0.9);
    buffer[i * 2] = sample;
    buffer[i * 2 + 1] = sample;
    currentPhase += phaseIncrement;
}
```

**Real Engine Mode:**
```cpp
// generateAudio() - reads from synthesizer buffer
int totalRead = 0;
api.ReadAudioBuffer(handle, buffer.data(), frames, &totalRead);
return totalRead;
```

The difference is:
- **Sine mode:** Bypasses synthesizer entirely - generates samples directly
- **Engine mode:** Goes through full synthesizer pipeline with buffering, filtering, etc.

## Team Investigation Tasks

### Priority 1: Fix Console Output Display
- Ensure RPM display updates properly in interactive mode
- Remove early iteration diagnostic limit (stops at iteration 50)

### Priority 2: Debug Audio Quality Issues
- Add audio sample logging (to diagnose crackling/jumping)
- Add buffer state logging (to diagnose overflow/underflow)
- Compare sine vs engine mode behavior in real-time

### Priority 3: Verify Buffer Management
- Confirm no samples are being dropped (writeSafe() prevents this)
- Monitor buffer size during playback (target vs current)

## Commands for Testing

```bash
# Sine mode test
./build/engine-sim-cli --sine --play

# Real engine at specific RPM
./build/engine-sim-cli --default-engine --rpm 1500 --play

# Interactive with custom engine
./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr

# Compare modes
./build/engine-sim-cli --sine --play  &
./build/engine-sim-cli --default-engine --rpm 1500 --play
```

## Questions for Team

1. Why does real engine have 1-3 second delay between keypress and audio?
   - Is this normal behavior or a symptom of the underlying issue?

2. Why does RPM display stop updating after iteration 50?
   - Is this by design or a bug?

3. Are filters causing the crackling/jumping when RPM changes?
   - Is the 10kHz → 44.1kHz resampling introducing artifacts?

4. Is the 500ms buffer size causing latency that makes RPM tracking feel unresponsive?

## Current Implementation Status

- **Architecture:** Pull-based callback (no CLI intermediate buffer)
- **Async thread:** Running, no buffer cap, dynamic target level
- **Underruns:** Sine=0, Engine=1-3
- **RPM tracking:** Sine=smooth, Engine=delayed/choppy

- **Audio Quality:** Sine=perfect, Engine=crackly/jumpy (but 0 underruns)
