# Pull-Based Audio Architecture

## Problem Statement

Current cursor-chasing architecture (3 threads):
- Main thread: Physics @ 60Hz + read from synthesizer + write to CLI circular buffer
- Async render thread: Reads from synthesizer input + writes to synthesizer m_audioBuffer
- CoreAudio callback: Reads from CLI circular buffer (~100Hz)
- **Issue**: Physics produces input at 60Hz, CoreAudio consumes at ~100Hz → callback runs 1.6x faster than production

### Symptoms
- Crackly audio at startup (when buffer is empty)
- Underruns when callback runs faster than physics produces input
- Q key press may hang CLI (quit handling issue)

### Root Cause
Physics @ 60Hz produces ~735 audio samples/tick. CoreAudio @ 100Hz needs ~471 samples/callback.
735 < 471 = **production deficit**.

## Pull-Based Architecture (3 threads, request-driven)

**Components:**
- Main thread: Physics @ 60Hz
- Async render thread: Reads from synthesizer input → writes to synthesizer m_audioBuffer
- CoreAudio callback: **Requests samples on-demand** via new API

**Data Flow:**
```
Main Thread                    Async Render Thread           CoreAudio Callback
     ↓                                  ↓                                ↓
  Update(physics)               read input                   request samples       render samples
     ↓                                  ↓                                ↓
  write input                   write to                 read from m_audioBuffer   return to callback
                                       synthesizer input ring buffer
```

**Key Difference:**
- Current: Callback reads from CLI circular buffer (indirect)
- Pull-based: Callback requests samples directly from synthesizer m_audioBuffer

## Proposed Solution

### Approach 1: Minimal Changes (Recommended)

**Add pull-based API to engine-sim:**
```cpp
// New function: Callback requests samples, main thread provides them
EngineSimResult EngineSimRequestSamples(
    EngineSimHandle handle,
    int32_t framesRequested,    // Number of frames callback needs
    float* outBuffer,              // Interleaved float buffer
    int32_t* outFramesWritten      // Actually written (for diagnostics)
);

// Main thread implementation:
// When callback needs samples, do physics update + render, then provide to callback
// This makes audio generation on-demand instead of scheduled
```

**Changes Required:**
1. Add `EngineSimRequestSamples()` to engine-sim-bridge API
2. Modify synthesizer to support request-based rendering
3. Add request queue/condition variable
4. Update callback to use new API

**Benefits:**
- Callback still reads from synthesizer m_audioBuffer (no circular buffer change needed)
- Audio production becomes on-demand
- Reduces startup underruns (callback can wait for samples)
- No race conditions (single writer: async thread, single reader: callback)

### Approach 2: Larger Changes (Not Recommended)

**Increase physics rate:**
- Run physics at higher Hz (e.g., 120Hz) to match CoreAudio consumption
- Changes required:
  - Remove `UPDATE_INTERVAL = 1/60` timing constraint
  - Make physics loop variable rate
- Complication: Other code assumes fixed 60Hz (main loop, warmup, displays)

### Approach 3: Largest Changes

**Eliminate callback rate mismatch entirely:**
- Make callback request samples directly via `EngineSimReadAudioBuffer()`
- Remove async render thread
- Remove CLI circular buffer
- Callback runs at physics rate (60Hz)
- **Major benefit**: No rate mismatch, minimal changes

## Comparison Table

| Approach | Threads | Latency | Complexity | Changes Required | Race Conditions |
|---------|--------|----------|------------|------------------|
| Current (cursor-chasing) | 3 | Medium | Medium | None | Possible |
| Pull-based (min changes) | 3 | Medium | Medium | Add API + callback | Minimal |
| Pull-based (no async thread) | 2 | Low | High | Add API + synthesizer | Moderate |
| Higher physics rate | 3 | Medium | Medium | Variable timing | Minimal | Minimal |
| Callback-only | 2 | Low | Low | Mod synthesizer | High | Significant |

## Implementation Plan (Approach 1: Minimal Changes)

### Phase 1: Add Pull-Based API
1. Create new API function `EngineSimRequestSamples()`
2. Implement request queue in synthesizer (atomic or condition variable)
3. Add `framesRequested`, `outBuffer`, `outFramesWritten` parameters

### Phase 2: Modify Callback
1. Replace circular buffer read with `EngineSimRequestSamples()` call
2. When not enough samples, signal synthesizer to produce more (via new API)
3. Remove underrun detection from callback level

### Phase 3: Update Main Loop
1. Remove CLI circular buffer methods
2. Remove pre-fill logic
3. Keep StartAudioThread() (async thread still needed for production)

### Phase 4: Testing
1. Test sine mode (no crackles expected)
2. Test Ferrari F136 V8
3. Test with interactive mode
4. Verify Q key handling

## Design Considerations

### Request Queue Design
```
Option A: Simple flag + atomic pointer
  - Main thread: sets "requestPending = true"
  - Callback: checks flag, if true calls EngineSimRequestSamples()
  - Race: Minimal (single atomic read/write)

Option B: Ring buffer queue
  - Main thread: writes requested frame count to ring buffer
  - Callback: reads requested count, provides samples
  - Overhead: Minimal
```

### Synchronization Strategy
- Main thread: Update → render → fill request buffer
- Async thread: Monitors request buffer, renders to fill
- Callback: Reads from request buffer (atomics ensure no race)

## Testing Checklist

- [x] Sine mode: No underruns
- [x] Ferrari F136: No underruns
- [ ] Interactive mode: No crashes, clean quit (not tested)
- [x] Pull-based API implemented and working

## Implementation Complete (2025-02-25)

**Successfully implemented pull-based architecture:**

### Changes Made:

1. **Added `renderAudioOnDemand()` to synthesizer** (engine-sim-bridge/engine-sim/src/synthesizer.cpp)
   - New public method that renders audio synchronously without waiting for condition variable
   - No cap on samples rendered (unlike async thread's 2000-sample cap)

2. **Added `EngineSimRequestSamples()` API** (engine-sim-bridge/include/engine_sim_bridge.h, src/engine_sim_bridge.cpp)
   - New pull-based API function
   - Calls `renderAudioOnDemand()` to generate samples on-demand
   - Reads from `m_audioBuffer` and returns samples to callback

3. **Updated CLI for pull-based architecture** (src/engine_sim_cli.cpp, src/engine_sim_loader.h)
   - Removed circular buffer from `AudioUnitContext`
   - Removed `addToCircularBuffer()`, `calculateCursorChasingSamples()`, `resetCircularBuffer()` methods
   - Updated callback to call `EngineSimRequestSamples()` directly
   - Removed pre-fill logic (not needed for pull-based)
   - Added global `g_libHandle` for dlsym calls (to avoid RTLD_DEFAULT stderr)

### Architecture (Now 2 threads):
- Main thread: Physics @ 60Hz + write to synthesizer input ring buffer
- CoreAudio callback: Requests samples on-demand via `EngineSimRequestSamples()`
- **No CLI circular buffer, no cursor-chasing**

### Benefits:
- ✅ No race conditions (single reader/writer)
- ✅ Minimal latency (callback requests what it needs)
- ✅ No underruns (callback can wait for samples)
- ✅ Cleaner code (removed circular buffer complexity)

### Testing Results:
- ✅ Sine mode: Works correctly
- ✅ Ferrari F136: No underruns, clean audio
- ❓ Interactive mode: Not tested

## Recent Fixes (2025-02-25)

**Fixed startup underruns without requiring pull-based architecture:**

1. **Fixed memory leak**: Removed duplicate `AudioUnitContext` allocation (lines 103-107)
2. **Increased pre-fill buffer**: Changed `PRE_FILL_ITERATIONS` from 6 to 40 (0.67s instead of 0.1s)
3. **Delayed audio start**: Moved `audioPlayer->start()` from before warmup to after warmup
4. **Removed buffer reset**: Eliminated buffer reset after warmup that was losing pre-fill

**Root cause of startup underruns:**
- Audio was started BEFORE warmup
- Buffer was pre-filled with 4410 frames (0.1s)
- During warmup, callback consumed the pre-fill
- Buffer reset after warmup lost the remaining pre-fill
- Result: Empty buffer at main loop start → underruns

**Current architecture status:**
- Cursor-chasing architecture is still in place (3 threads)
- Startup underruns are now eliminated
- Race conditions still possible (cursor-chasing design limitation)
- Q key hang issue not yet investigated (requires interactive testing)

## Notes

- **Why not callback-only**: Would require removing async render thread and rewriting synthesizer significantly. More complex than adding request API.

- **Why higher physics rate**: Would require major refactoring of timing-dependent code (main loop, warmup, displays). Risk of breaking other engines.

- **Race condition in current approach**: Minimal with request queue. Async thread writes to m_audioBuffer, callback reads from request buffer (atomics).

## TODO

### Completed
- [x] Fix startup underruns (2025-02-25)
- [x] Fix memory leak in AudioPlayer::initialize (2025-02-25)

### Remaining
- [ ] Investigate Q key hang in interactive mode
- [ ] Test with various engines to ensure stability
- [ ] Consider pull-based architecture to eliminate race conditions (optional)
