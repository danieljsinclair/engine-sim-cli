# Audio Investigation Handover Document

**Date:** 2025-02-03
**Platform:** macOS M4 Pro (Apple Silicon)
**Project:** engine-sim-cli - Command-line interface for engine-sim audio generation
**Status:** Investigation in progress - root cause not yet identified

## Executive Summary

The CLI implementation exhibits **periodic audio crackling** and **~100ms latency** that do not occur in the Windows GUI version. Extensive investigation has ruled out several theories, but the root cause remains unknown. Position tracking has been verified as accurate through hardware position feedback diagnostics.

## Current State

### What Works
- AudioUnit real-time streaming implemented and functional
- Circular buffer architecture matching GUI design
- 60Hz update rate matching GUI
- Position tracking verified accurate (hardware position matches manual tracking)
- Zero underruns after initialization phase
- Comprehensive diagnostics implemented

### What Doesn't Work
- **Periodic crackling** in audio output (both sine and engine modes)
- **~100ms latency** between parameter changes and audio output
- Startup underruns (2-3 during first second, expected without pre-fill)

### What Has Been Ruled Out
✅ Position tracking errors (hardware position confirms manual tracking is accurate)
✅ Update rate differences (CLI already at 60Hz matching GUI)
✅ Audio library choice (AudioUnit is correct approach for macOS)
✅ Double buffer consumption (fixed - no longer occurs)
✅ Underruns as primary cause (crackles occur without underruns)

### What Remains Unknown
❓ Exact source of synthesizer output discontinuities
❓ Why GUI doesn't exhibit same crackles with same synthesizer
❓ Whether buffer lead target (10%) is optimal for pull model
❓ Root cause of periodic crackling pattern

## Architecture

### Audio Flow

```
Main Loop (60Hz)
  ├── Generates/reads audio samples
  ├── Writes to circular buffer (44100 samples)
  └── Calculates buffer lead (10% target = 4410 samples)

AudioUnit Callback (~94Hz, real-time)
  ├── Receives hardware position feedback (mSampleTime)
  ├── Reads from circular buffer
  ├── Detects underruns and discontinuities
  └── Streams to audio hardware
```

### Key Files

**Primary implementation:**
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
  - Lines 71-95: AudioUnitContext structure
  - Lines 192-205: Circular buffer initialization
  - Lines 320-420: Write discontinuity detection
  - Lines 400-750: AudioUnit callback with diagnostics
  - Lines 1100-1200: Main loop audio generation

**Documentation:**
- `/Users/danielsinclair/vscode/engine-sim-cli/AUDIO_THEORIES_TRACKING.md` - Complete investigation history
- `/Users/danielsinclair/vscode/engine-sim-cli/AUDIO_DIAGNOSTIC_REPORT.md` - Diagnostic analysis
- `/Users/danielsinclair/vscode/engine-sim-cli/AUDIO_FIX_IMPLEMENTATION_GUIDE.md` - Proposed fixes

**Reference implementations:**
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` - GUI reference
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp` - Audio thread

## Configuration

| Parameter | Value | Purpose |
|-----------|-------|---------|
| Sample Rate | 44100 Hz | Audio sample rate |
| Buffer Size | 44100 samples | 1 second @ 44.1kHz |
| Buffer Lead Target | 10% (4410 samples) | 100ms lead time |
| Update Rate | 60 Hz | Physics/audio update |
| Callback Rate | ~94 Hz | Real-time streaming |
| Discontinuity Threshold | 0.2 (20%) | False positive filter |

## Diagnostic Output Examples

### Startup Sequence
```
[Audio] AudioUnit initialized at 44100 Hz (stereo float32)
[Audio] Real-time streaming mode (no queuing latency)
[POSITION DIAGNOSTIC #0 at 0ms] HW:0 (mod:0) Manual:0 Diff:0
[UNDERRUN #1 at 95ms] Req: 470, Got: 176, Avail: 176, WPtr: 4410, RPtr: 4234
  [POSITION AT UNDERRUN] HW:4234 (mod:4234) Manual:4234 Diff:0
```

### Steady State
```
[DIAGNOSTIC #100] Time: 1055ms, HW:46570 (mod:2470) Manual:2940 Diff:-470,
WPtr: 7700, Avail: 4760 (108.0ms), Underruns: 2, ReadDiscontinuities: 0,
Wraps: 1, PosMismatches: 0
```

### Key Finding
```
Diff:0 - Hardware position matches manual tracking exactly
```

This confirms position tracking is NOT the problem.

## Testing Commands

### Build
```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
make clean
make
```

### Sine Mode Test
```bash
./build/engine-sim-cli --sine --rpm 2000 --play
```

### Engine Mode Test
```bash
./build/engine-sim-cli --default-engine --rpm 2000 --play
```

## Investigation History

### Phase 1: Position Tracking Verification (2025-02-03)
**Status:** COMPLETE - HYPOTHESIS DISPROVEN

**Hypothesis:** Manual `readPointer` tracking was inaccurate, causing buffer lead miscalculations.

**Test:** Added diagnostics to compare hardware position (`timeStamp->mSampleTime`) with manual tracking.

**Result:** Hardware position matches manual tracking exactly (`Diff:0`).

**Conclusion:** Position tracking is accurate. Problem is elsewhere.

### Previous Sessions Summary

1. **Audio library selection:** Tried OpenAL, AudioQueue, settled on AudioUnit (correct choice)
2. **Double buffer consumption:** Fixed race condition where main thread and callback both read from buffer
3. **Buffer sizing:** Reduced from 96000 to 44100 to match GUI
4. **Update rate:** Verified CLI at 60Hz matching GUI
5. **Architecture:** Implemented circular buffer matching GUI design
6. **Diagnostics:** Added comprehensive position tracking, underrun detection, discontinuity detection

## Comparison: CLI vs GUI

| Aspect | CLI (macOS) | GUI (Windows) | Status |
|--------|-------------|---------------|--------|
| Buffer architecture | Circular buffer (44100) | Circular buffer (44100) | ✅ Match |
| Buffer lead target | 10% (4410 samples) | 10% (4410 samples) | ✅ Match |
| Update rate | 60 Hz | ~60 Hz | ✅ Match |
| Position feedback | Hardware mSampleTime | GetCurrentPosition() | ✅ Equivalent |
| Audio model | Pull (AudioUnit) | Push (DirectSound) | ⚠️ Different |
| Conditional writes | Yes | Yes | ✅ Match |
| Underruns | Startup only | Startup only | ✅ Match |
| Crackles | YES (audible) | NONE | ❌ Problem |

**Critical Question:** Why does GUI work perfectly with same synthesizer and buffer architecture?

## Proposed Next Steps

### Immediate Actions

1. **Profile GUI vs CLI timing**
   - Measure actual callback intervals with high-resolution timers
   - Compare buffer fill patterns over time
   - Check for timing jitter or scheduling differences

2. **Test different buffer lead targets**
   - Try 5% (50ms) - reduces latency
   - Try 20% (200ms) - increases safety margin
   - Measure effect on crackles

3. **Investigate AudioUnit-specific behavior**
   - Research if AudioUnit has different buffering than DirectSound
   - Check if pull model requires different synchronization than push model
   - Consider implementing buffer pre-fill to eliminate startup underruns

### Medium-term Investigations

1. **Add synthesizer output diagnostics**
   - Check if synthesizer produces discontinuous samples
   - Compare GUI vs CLI synthesizer output directly
   - Verify filter states are identical

2. **Cross-platform comparison**
   - Test CLI on Windows with DirectSound
   - Test GUI on macOS (if possible)
   - Isolate platform vs implementation differences

3. **Audio API deep dive**
   - Research AudioUnit callback timing guarantees
   - Compare with DirectSound streaming behavior
   - Consider alternative: miniaudio for unified API

### Long-term Considerations

1. **Implement pre-fill buffer**
   - Fill buffer before starting playback
   - Eliminates startup underruns
   - May affect crackling behavior

2. **Dynamic buffer lead management**
   - Adjust lead based on measured latency
   - Respond to buffer underruns/overruns
   - Match GUI's adaptive behavior

3. **Consider miniaudio library**
   - Single-header C library
   - Supports macOS, Windows, Linux, ESP32
   - Unified API abstracting push/pull differences

## Code Locations Reference

### CLI Implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
  - Line 71-95: AudioUnitContext structure
  - Line 192-205: Circular buffer initialization
  - Line 320-420: addToCircularBuffer() with discontinuity detection
  - Line 400-750: AudioUnit callback with comprehensive diagnostics
  - Line 1100-1200: Main loop audio generation (sine mode)
  - Line 1600-1645: Main loop audio generation (engine mode)

### GUI Reference
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
  - Line 169-170: Buffer initialization with 10% lead
  - Line 253-274: Main loop buffer management
  - Line 179: Audio source creation (DirectSound)

### Synthesizer
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
  - Line 228: Audio thread throttle (2000 sample buffer)
  - Line 232-234: Burst rendering

## Important Reminders

### What NOT to Do

❌ **Don't change position tracking** - It's already verified as accurate
❌ **Don't change update rate** - Already matches GUI at 60Hz
❌ **Don't change buffer architecture** - Already matches GUI design
❌ **Don't speculate without evidence** - Use diagnostics to verify theories

### What to Do

✅ **Use diagnostics** - Extensive diagnostic infrastructure already in place
✅ **Measure, don't guess** - Every theory should be tested with evidence
✅ **Compare with GUI** - GUI works perfectly, understand why
✅ **Document everything** - Update AUDIO_THEORIES_TRACKING.md after every change

### Critical Principle

**NO SPECULATION - ONLY EVIDENCE**

Every theory must be tested with diagnostics. Every result must be documented. Failed theories are valuable - they rule out possibilities and narrow the search.

## Documentation Files

1. **AUDIO_THEORIES_TRACKING.md** - Complete investigation history with evidence
2. **AUDIO_DIAGNOSTIC_REPORT.md** - Analysis of CLI vs GUI differences
3. **AUDIO_FIX_IMPLEMENTATION_GUIDE.md** - Proposed fixes (not yet implemented)
4. **This handover document** - Current state summary

## Git Status

**Current branch:** master
**Uncommitted changes:** `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Recent commits:**
- `0f54807` feat: Real-time AudioUnit streaming with diagnostics
- `19ac635` feat: Implement AudioUnit real-time streaming audio
- `0ae089e` refactor: Reorganize project structure and revert failed audio fixes

## Contact Context

This handover is for:
- Continuing the audio investigation
- Understanding what has been tried
- Avoiding repeating failed approaches
- Building on existing diagnostics

**Remember:** The diagnostics infrastructure is comprehensive. Use it to test theories before implementing changes.

---

**End of Handover Document**

**Next person:** Read AUDIO_THEORIES_TRACKING.md for full investigation history. Use existing diagnostics to test new theories before implementing.
