# CLI 10% Throttle Dropout - Root Cause Summary

## The Finding in One Sentence

**The CLI reads audio immediately after notifying the audio thread, before the thread finishes rendering, causing buffer underruns that are filled with silence.**

## The Race Condition

```
TIMELINE (CLI):
─────────────────────────────────────────────────────────────────────────
Main Thread:  │ EngineSimUpdate() │  EngineSimReadAudioBuffer()  │
              │ (notifies thread)  │  (reads IMMEDIATELY)        │
              ├────────────────────┼─────────────────────────────┤
Audio Thread: │    sleeping →     │  WAKE UP → render → WRITE   │
              │                    │  (SLOW - takes time!)       │
─────────────────────────────────────────────────────────────────────────
              ↑                    ↑
              Line 942             Line 965
              (no delay!)

Result: Main thread reads while audio thread is still rendering → UNDERRUN!


TIMELINE (GUI):
─────────────────────────────────────────────────────────────────────────
Main Thread:  │ endFrame() │ [29 lines of work] │ readAudioOutput() │
              │ (notify)   │ (buffer calc, etc) │ (reads LATER)    │
              ├────────────┼─────────────────────┼──────────────────┤
Audio Thread: │  sleeping  →  WAKE UP → render  → WRITE (done!)   │
              │            │                     │                  │
─────────────────────────────────────────────────────────────────────────
              ↑            ↑                     ↑
              Line 245     Lines 247-272         Line 274
                           (natural delay!)

Result: Audio thread finishes before main thread reads → NO UNDERRUN!
```

## Why 10% Throttle is Worse

At 10% throttle, the audio thread renders fewer samples per call:

```
Input buffer provides: ~800 samples per frame
CLI asks for: 4800 samples
Audio thread renders: min(2000 - buffer_size, 800) = 800 samples
UNDERRUN: 4800 - 800 = 4000 samples filled with zeros!
```

At higher throttle, the audio is louder so the dropouts are less noticeable, but the underrun still happens!

## The Fix

Add `EngineSimWaitForAudio()` call between `EngineSimUpdate()` and `EngineSimReadAudioBuffer()`:

```cpp
EngineSimUpdate(handle, updateInterval);
EngineSimWaitForAudio(handle);  // <-- ADD THIS
EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
```

This waits for the audio thread to finish rendering before reading, matching the GUI's behavior.

## Previous Analysis Errors

| TA | Finding | Why It Was Wrong |
|----|---------|------------------|
| TA1 | Increase read size to 4800 | Made it WORSE (larger underrun) |
| TA2 | 10% load = 99% throttle | Throttle affects amplitude, not sample count |
| TA3 | CLI doesn't call endInputBlock() | CLI DOES call it via EngineSimUpdate() |

## Evidence Locations

| File | Lines | Description |
|------|-------|-------------|
| `src/engine_sim_cli.cpp` | 942-965 | CLI main loop (no delay) |
| `engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` | 245-274 | GUI process loop (29 lines delay) |
| `engine-sim-bridge/engine-sim/src/synthesizer.cpp` | 222-256 | Audio thread rendering (unlocks before render) |
| `engine-sim-bridge/engine-sim/src/synthesizer.cpp` | 141-159 | readAudioOutput (fills underruns with zeros) |
| `engine-sim-bridge/engine-sim/src/synthesizer.cpp` | 197-213 | endInputBlock (sets m_processed=false, notifies) |

## Next Steps

1. Implement `EngineSimWaitForAudio()` in bridge
2. Add call to `EngineSimWaitForAudio()` in CLI main loop
3. Test at 10% throttle to verify dropouts are eliminated
4. Test at other throttle positions to ensure no regressions
