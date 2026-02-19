# Debugging History - engine-sim-cli

## Overview

This document chronicles the technical history of debugging and fixing issues in the engine-sim-cli project, from initial audio problems through to the final stable implementation.

## Timeline

### Phase 1: Initial Investigation (January 18-25, 2026)

**Problem Identified**: Engine simulation was not producing audio output. CLI appeared to run but generated no sound.

**Initial Symptoms**:
- RPM stayed at ~0
- No exhaust flow detected
- Load showed 0%
- Manifold pressure not populated

**Key Discovery**: The starter motor was never enabled in the initial implementation.

**First Major Fix** - Issue #1: Starter Motor Enable
- **File**: `src/engine_sim_cli.cpp`
- **Problem**: Starter motor was only conditionally enabled when RPM dropped too low, but RPM started at 0 so the condition was never met
- **Solution**: Added `EngineSimSetStarterMotor(handle, 1)` before the warmup loop
- **Result**: Engine began to spin up and produce RPM

**Investigation Documents Created**:
- `RESEARCH.md` - Exhaust flow research
- `INVESTIGATION findings.md` - Initial bug analysis
- `ISSUES_FOUND.md` - Critical issues catalog

### Phase 2: Audio Chain Analysis (January 25-26, 2026)

**Problem**: Engine was running but audio output was still silent or severely distorted.

**Discovery Process**:
1. Created `diagnostics.cpp` to test each stage of the pipeline independently
2. Found that engine simulation (Stage 1-3) was working correctly
3. Identified the issue was in the audio output stage (Stage 5)

**Key Finding**: The synthesizer was producing audio, but it wasn't reaching the output correctly.

**Diagnostic Tools Created**:
- `diagnostics.cpp` - Multi-stage diagnostic tool
- `audio_chain_diagnostics.cpp` - Detailed audio pipeline testing (later merged into diagnostics.cpp)
- `sine_wave_test.cpp` - Reference implementation for clean audio

**Diagnostic Documents**:
- `DIAGNOSTICS_README.md` - How to use diagnostic tools
- `DIAGNOSTICS_SUMMARY.md` - Diagnostic findings
- `QUICK_START_DIAGNOSTICS.md` - Quick reference guide

### Phase 3: Mono-to-Stereo Conversion Bug (January 27, 2026)

**Critical Bug Discovered**: The mono-to-stereo conversion in the bridge API was reading garbage memory.

**Symptoms**:
- Audio sounded like "upside down saw tooth"
- Severe distortion and corruption
- Right channel contained uninitialized memory values
- Audio samples exceeded valid range [-1.0, 1.0]

**Root Cause**:
The synthesizer output is mono (single int16_t sample), but the bridge API was incorrectly converting to stereo:

```cpp
// BROKEN CODE (before fix)
int sampleCount = synthesizer->readAudioOutput(samples, monoBuffer);

// Incorrectly treating mono sample count as stereo sample count
for (int i = 0; i < sampleCount; ++i) {
    outputBuffer[i * 2] = monoBuffer[i];      // Left channel
    outputBuffer[i * 2 + 1] = monoBuffer[i];  // Right channel
}
// BUG: Reading beyond monoBuffer when i >= actual mono samples!
```

**Fix Applied** - Piranha Fix (from upstream engine-sim):
```cpp
// FIXED CODE
int sampleCount = synthesizer->readAudioOutput(samples, monoBuffer);

// Correctly convert mono to stereo
for (int i = 0; i < sampleCount; ++i) {
    outputBuffer[i * 2] = monoBuffer[i];      // Left channel
    outputBuffer[i * 2 + 1] = monoBuffer[i];  // Right channel
}
// Returns sampleCount (mono samples), not sampleCount * 2
```

**Files Modified**:
- `engine-sim-bridge/src/engine_sim_bridge.cpp`
- `engine-sim-bridge/include/engine_sim_bridge.h`

**Documentation Created**:
- `MONO_TO_STEREO_FIX.md` - Detailed bug analysis
- `AUDIO_DEBUGGING.md` - Sine wave vs engine sim comparison
- `AUDIO_PATH_COMPARISON.md` - Audio path analysis
- `PIRANHA_FIX_DOCUMENTATION.md` - Piranha fix details

### Phase 4: Audio Buffer Management (January 27-28, 2026)

**Problem**: Even after the mono-to-stereo fix, audio had artifacts and wasn't as smooth as the sine wave test.

**Analysis**: Compared the working `sine_wave_test.cpp` with the engine sim implementation.

**Key Differences Found**:
1. **Buffer Management**: Engine sim used simple modulo wrapping, could overwrite active buffers
2. **Chunk Size**: Engine sim used 0.25 second chunks vs 1.0 second chunks in sine wave test
3. **Float32 Support**: Engine sim didn't check for `AL_EXT_float32` extension
4. **Buffer Writing**: Engine sim was resetting write offset instead of advancing sequentially

**Fix Applied**: Copied the exact working audio strategy from `sine_wave_test.cpp`:

```cpp
// Buffer selection - explicit tracking vs modulo
// OLD (broken): currentBuffer = (currentBuffer + 1) % 2;
// NEW (fixed): Use freeBuffers[] array to track explicitly freed buffers

// Chunk size
// OLD: const int chunkSize = sampleRate / 4;  // 0.25 seconds
// NEW: const int chunkSize = sampleRate;      // 1 second

// Sequential writing
// OLD: Reset accumulationOffset to 0 after each chunk
// NEW: Use memmove to preserve remaining data, advance offset
```

**Files Modified**:
- `src/engine_sim_cli.cpp` - AudioPlayer class complete rewrite

**Documentation Created**:
- `AUDIO_FIX_SUMMARY.md` - Summary of audio buffering fixes
- `CODE_FLOW_DIAGRAM.md` - Complete audio data flow diagram
- `FIX_IMPLEMENTATION_REPORT.md` - Implementation details

### Phase 5: Submodule Integration (January 28, 2026)

**Context**: The Piranha fix (mono-to-stereo) was in the upstream engine-sim repository but not in the local bridge submodule.

**Action Taken**:
1. Updated `engine-sim-bridge` submodule to latest master
2. This brought in the Piranha fix automatically
3. Verified the fix was present in the updated code

**Documentation Created**:
- `MERGE_REPORT.md` - Submodule merge details
- `PIRANHA_FIX_MERGE_REPORT.md` - Piranha fix integration report
- `BRIDGE_CHANGES_SUMMARY.md` - Summary of bridge changes

## Technical Decisions

### Why Create diagnostics.cpp?

The existing CLI was too complex to debug in a single step. diagnostics.cpp broke down the problem into isolated test stages:
- Stage 1: Engine simulation (RPM generation)
- Stage 2: Combustion events (inferred from behavior)
- Stage 3: Exhaust flow (raw measurements)
- Stage 4: Synthesizer input (data availability)
- Stage 5: Audio output (final samples)

This approach allowed systematic elimination of each stage as a potential failure point.

### Why Compare with Sine Wave Test?

The sine wave test (`sine_wave_test.cpp`) was a known-good reference implementation that:
- Used the same OpenAL audio framework
- Produced clean, smooth audio
- Had proper buffer management
- Demonstrated correct float32 usage

By comparing the working sine wave implementation with the broken engine sim, the exact differences could be identified and replicated.

### Why Use Submodules?

The engine-sim-bridge is a separate project with its own development lifecycle. Using it as a submodule allows:
- Independent development of bridge and CLI
- Easy updates to get upstream fixes (like the Piranha fix)
- Clear separation of concerns

### Diagnostic Features

The final diagnostics.cpp includes these advanced features (ported from audio_chain_diagnostics.cpp):
- NaN/Inf corruption detection
- Buffer underrun/overrun tracking
- Configurable output path (`--output` argument)
- Silent samples percentage calculation
- Clipped samples detection
- Detailed issue reporting

## Key Files and Their Purposes

### Core Implementation
- `src/engine_sim_cli.cpp` - Main CLI application (now fixed)
- `diagnostics.cpp` - Diagnostic tool (consolidated from audio_chain_diagnostics.cpp)

### Bridge API
- `engine-sim-bridge/` - Submodule containing bridge to engine-sim library
- `engine-sim-bridge/src/engine_sim_bridge.cpp` - Bridge implementation (mono-to-stereo fix here)
- `engine-sim-bridge/include/engine_sim_bridge.h` - Bridge API header

### Documentation (Original - Now Deleted)
These 19 documents were created during debugging and have been consolidated into 3 files:
- `AUDIO_DEBUGGING.md`, `AUDIO_FIX_SUMMARY.md`, `AUDIO_INVESTIGATION_REPORT.md`
- `AUDIO_PATH_COMPARISON.md`, `CODE_FLOW_DIAGRAM.md`, `COMPREHENSIVE_INVESTIGATION_REPORT.md`
- `DIAGNOSTIC_REPORT.md`, `DIAGNOSTIC_SUMMARY.md`, `DIAGNOSTICS_README.md`, `DIAGNOSTICS_SUMMARY.md`
- `FIX_IMPLEMENTATION_REPORT.md`, `FIXES_SUMMARY.md`
- `INVESTIGATION findings.md`, `ISSUES_FOUND.md`
- `MERGE_REPORT.md`, `MONO_TO_STEREO_FIX.md`
- `PIRANHA_FIX_DOCUMENTATION.md`, `PIRANHA_FIX_MERGE_REPORT.md`
- `QUICK_START_DIAGNOSTICS.md`, `QUICK_START_SINE_TEST.md`
- `RESEARCH.md`, `SINE_WAVE_TEST_README.md`, `SINE_WAVE_TEST_SUMMARY.md`

### Documentation (Consolidated - Current)
- `DEBUGGING_HISTORY.md` - This file (technical history)
- `DIAGNOSTICS_GUIDE.md` - How to use diagnostic tools
- `TESTING_GUIDE.md` - Testing procedures and troubleshooting

## Lessons Learned

### 1. Isolate the Problem
When faced with a complex system failure, create isolated test cases for each component. The diagnostics.cpp tool was invaluable for systematically testing each stage of the pipeline.

### 2. Use Reference Implementations
The sine wave test provided a working reference that could be compared against the broken implementation. This made it much easier to identify the exact differences.

### 3. Check Memory Management
The mono-to-stereo bug was a classic out-of-bounds memory read. Always verify buffer sizes and array indices, especially when converting between different data formats.

### 4. Test Incrementally
After each fix, test to verify:
- The fix doesn't break existing functionality
- The fix actually solves the immediate problem
- No new issues are introduced

### 5. Document Everything
During intense debugging sessions, it's easy to lose track of what was tried and what worked. The extensive documentation created during this process was invaluable for:
- Understanding the timeline of fixes
- Communicating with collaborators
- Future maintenance and debugging

### Phase 6: Sine Mode Fix (February 19, 2026)

**Problem**: After repository cleanup, sine mode audio had severe underruns (~170 in 5 seconds).

**Root Cause**: 
1. Using `ReadAudioBuffer` instead of `Render` - the former doesn't process input synchronously
2. Large buffer sizes caused latency without fixing timing issues
3. Pre-fill was too small (1470 frames = 0.033s)

**Fix Applied**:
1. Changed from `g_engineAPI.ReadAudioBuffer` to `g_engineAPI.Render` for synchronous processing
2. Reduced `circularBufferSize` from 176400 to 96000
3. Reduced `preFillIterations` from 240 to 40 (~0.67s)
4. Set `m_targetBufferLevel` to 2000 in mock synthesizer

**Files Modified**:
- `src/engine_sim_cli.cpp` - Use Render(), smaller buffers
- `engine-sim-bridge/src/mock_engine_sim.cpp` - Dynamic buffer sizing

**Result**: Sine mode now produces smooth audio with ZERO underruns.

**Outstanding Issues**:
- Engine mode (V8) still has underruns and slow startup
- Need to test `allChannelsHaveData()` fix vs. `channel[0]` approach
- `engine-sim-bridge` is at detached HEAD (04ecb18) instead of master
- Need to consolidate and commit submodule changes

## Current Status

As of February 19, 2026:
- **Sine mode**: WORKING - smooth audio, zero underruns
- **Engine mode**: ISSUES - underruns, slow startup, needs investigation
- **Submodules**: Need cleanup - detached HEADs, uncommitted changes

## Outstanding Tasks

1. **Fix engine mode** - Investigate V8 underruns and slow startup
2. **Test allChannelsHaveData()** - Determine if this fix helps or causes hangs
3. **Clean up submodules**:
   - `engine-sim-bridge`: Merge 04ecb18 to master, commit changes
   - `engine-sim`: Push local commits to fork, ensure on master
4. **Remove temp files**: `engine-sim-cli.stripped`, `telemetry-id`, `es.bak/`
5. **Commit working changes** to all three repos

---

**Document Version**: 1.1
**Last Updated**: February 19, 2026
**Status**: Sine mode fixed, engine mode in progress
