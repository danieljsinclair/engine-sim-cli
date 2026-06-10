# HARDWARE SYNC FAILURE REPORT

**Date:** 2026-02-04
**Status:** FAILED - Audio still has crackles and dropouts

## CRITICAL LESSON LEARNED

I incorrectly claimed the audio crackle issue was RESOLVED when it clearly is not. The user reports:
- Regular dropouts and crackles/static in BOTH sine and engine modes
- The discontinuities are real audio artifacts, not just diagnostics
- More correlation with DIAGNOSTIC and POSITION messages than READ DISCONTINUITY messages
- Audio quality is poor with almost continual crackles

## FAILED ATTEMPT: Hardware Synchronization

### What Was Tried
- Attempted to replicate GUI's GetCurrentWritePosition() behavior in CLI
- Used AudioUnit's hardwareSampleTimeMod for position feedback
- Implemented commitBlock() style behavior
- Removed immediate read pointer advancement in callback

### Why It Failed
1. **Fundamental Model Difference**: DirectSound (GUI) is push model, AudioUnit (CLI) is pull model
   - GUI: DirectSound pushes audio, can query write position
   - CLI: AudioUnit pulls audio via callback, different timing model

2. **Emulation Not Real Hardware**: AudioUnit's timestamp is not equivalent to DirectWritePosition
   - Cannot truly emulate hardware feedback in pull model
   - Timing relationship is fundamentally different

3. **Diagnosis Mismatch**: I claimed "no underruns" when user clearly hears crackles
   - Discontinuities ARE audible artifacts
   - Zero underruns doesn't mean no quality issues

## EVIDENCE FROM USER

### Sine Mode Debug Output
```
[READ DISCONTINUITY #399 at 21151ms] Frame:0 Ch:0 Delta:0.6864 Last:0.4080 Curr:-0.2784
[READ DISCONTINUITY #400 at 21151ms] Frame:0 Ch:1 Delta:0.6864 Last:0.4080 Curr:-0.2784
[DIAGNOSTIC #2000] Time: 21322ms, HW:940330 (mod:14230) Manual:13760 Diff:470, WPtr: 23360, Avail: 9600 (217.7ms), Underruns: 0, ReadDiscontinuities: 402, Wraps: 0, PosMismatches: 1
[POSITION DIAGNOSTIC #2000 at 21333ms] HW:940800 (mod:14700) Manual:13760 Diff:940
```

### User Observations
- "Regular drop outs and crackles/static with both sine and engine"
- "Little continuity between READ DISCONTINUITY and audio artefacts"
- "More correlation with the DIAGNOSTIC and POSITION messages"
- "Pretty much drops out and crackles almost continually"
- "How can you effectively emulate the hardware feedback in DirectSound"

## CRITICAL INSIGHT

The problem is deeper than just timing drift. We're trying to emulate a push model (DirectSound) in a pull model (AudioUnit), which fundamentally doesn't work the same way.

## NEXT STEPS

1. **Two Architecture Review Needed**:
   - Option 1: How to handle pull model vs push model fundamental difference
   - Option 2: How to properly emulate DirectSound's GetWritePosition in AudioUnit

2. **Better Diagnostics**: Need diagnostics that actually correlate with audio quality
   - Current discontinuity detection may not be accurate
   - Need to detect when audio actually crackles vs when it's smooth

3. **Accept Model Differences**: May need completely different approach for AudioUnit

## WARNING

Previous attempts to claim "problem solved" when it wasn't were wrong. Must:
- Listen to user's actual experience
- Verify audio quality, not just metrics
- Acknowledge when approaches don't work
- Document failures to avoid repeating them

The crackle issue is NOT RESOLVED. Need fundamental architectural rethink.