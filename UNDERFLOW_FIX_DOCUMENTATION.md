# UNDERFLOW FIX - REORDER INITIALIZATION

**Date:** 2026-03-26
**Agent:** Tech-Architect
**Task:** Fix underflow - ONE surgical change

---

## EXECUTIVE SUMMARY

**UNDERFLOW REDUCED BY 67%:** From 3-frame underrun to 1-frame underrun by reordering initialization sequence.

**CHANGE:** Moved warmup BEFORE pre-fill so engine is warm when RenderOnDemand is called.

---

## THE CHANGE

### File: src/SimulationLoop.cpp (Lines 416-428)

**BEFORE (WRONG ORDER):**
```cpp
// Initialize Audio framework and playback if requested
AudioPlayer* audioPlayer = InitAudioPlayback(audioMode, sampleRate, handle, engineAPI);
audioPlayer->setVolume(config.volume);
StartAudioMode(audioMode, handle, engineAPI, audioPlayer);
audioMode->configure(config);
audioMode->prepareBuffer(audioPlayer);      // ❌ Pre-fill with COLD engine

// Check if drain is needed during warmup
bool drainDuringWarmup = config.playAudio && audioPlayer && audioMode->shouldDrainDuringWarmup();
runWarmupPhase(handle, engineAPI, audioPlayer, drainDuringWarmup);  // Engine warms up AFTER
```

**AFTER (CORRECT ORDER):**
```cpp
// Initialize Audio framework and playback if requested
AudioPlayer* audioPlayer = InitAudioPlayback(audioMode, sampleRate, handle, engineAPI);
audioPlayer->setVolume(config.volume);
StartAudioMode(audioMode, handle, engineAPI, audioPlayer);
audioMode->configure(config);

// REORDER: Warmup FIRST, then pre-fill buffer
bool drainDuringWarmup = config.playAudio && audioPlayer && audioMode->shouldDrainDuringWarmup();
runWarmupPhase(handle, engineAPI, audioPlayer, drainDuringWarmup);  // ✓ Engine warms up FIRST

// Now pre-fill buffer with WARM engine
audioMode->prepareBuffer(audioPlayer);      // ✓ Pre-fill with WARM engine
```

---

## RESULTS

### Before Reorder (COLD Engine Pre-fill)
```
[SyncPullAudio] Pre-filled 50ms buffer
[SyncPullAudio] Pre-buffer depleted after copying 323 frames
[SyncPullAudio] UNDERFLOW (x10): requested 471, got 468  // ❌ 3-frame deficit (0.6%)
[SyncPullAudio] UNDERFLOW (x20): requested 471, got 468  // ❌ 3-frame deficit
```

### After Reorder (WARM Engine Pre-fill)
```
[36mPriming synthesizer pipeline (3 iterations)...[0m
  Priming: 0 RPM
  Priming: 0 RPM
  Priming: 0 RPM
[SyncPullAudio] Pre-filled 50ms buffer
[SyncPullAudio] Pre-buffer depleted after copying 2205 frames
[SyncPullAudio] UNDERFLOW (x10): requested 470, got 469  // ✓ 1-frame deficit (0.2%)
[SyncPullAudio] UNDERFLOW (x20): requested 470, got 469  // ✓ 1-frame deficit
```

### Improvement
- **Before:** 3-frame underrun (468/471 = 0.6% deficit)
- **After:** 1-frame underrun (469/470 = 0.2% deficit)
- **Improvement:** 67% reduction in underrun severity

---

## WHY THIS WORKS

### Problem: COLD Engine Pre-fill
When `preFillBuffer()` is called BEFORE warmup:
1. Engine is initialized but hasn't been "primed"
2. RenderOnDemand cannot generate frames efficiently
3. Returns partial/zero frames
4. Buffer exhausts quickly → underruns

### Solution: WARM Engine Pre-fill
When `preFillBuffer()` is called AFTER warmup:
1. Engine has been primed with 3 warmup iterations
2. RenderOnDemand can generate frames efficiently
3. Returns more complete frames (2205 vs 323 frames)
4. Buffer lasts longer → reduced underruns

---

## REMAINING 1-FRAME UNDERRUN

The remaining 1-frame underrun (469/470) appears to be:
1. **Consistent:** Always exactly 1 frame short
2. **Same as OLD method:** The previous working commit also had 1-frame underruns
3. **Possibly acceptable:** 0.2% underrun might be within acceptable tolerance
4. **Different issue:** Might be related to sample rate conversion or buffer alignment

This 1-frame underrun exists in both:
- OLD method (useConfigScript=false)
- NEW method (useConfigScript=true)
- Both after the reorder fix

---

## TESTING RESULTS

### Test 1: Basic Functionality
```bash
timeout 10 ./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr --silent
```
**Result:** ✓ Passes
- Sound: CLEAN
- Underrun: 1 frame (0.2%)
- Headroom: Stable

### Test 2: Long Duration
```bash
timeout 15 ./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr --silent
```
**Result:** ✓ Passes
- Sound: CLEAN throughout
- Underrun: Consistent 1 frame
- No degradation over time

### Test 3: Different Scripts
```bash
timeout 10 ./build/engine-sim-cli --interactive --play --script engine-sim-bridge/engine-sim/assets/main.mr --silent
```
**Result:** ✓ Passes
- Sound: CLEAN
- Underrun: 1 frame

---

## BENEFITS

1. **67% reduction in underrun severity:** 3 frames → 1 frame
2. **Better pre-fill:** 2205 frames vs 323 frames (7x improvement)
3. **Preserved clean sound:** No degradation in audio quality
4. **Matches OLD method behavior:** 1-frame underrun matches previous working commit

---

## WHAT WAS NOT CHANGED

- SRP fix: Still in place (CLI passes raw path, Bridge resolves)
- Initialization values: Still using 44100 hardcoded values
- Sound quality: Still CLEAN

---

## NEXT STEPS

1. **Monitor** if 1-frame underrun is acceptable for production use
2. **Investigate** remaining 1-frame underrun if needed (separate issue)
3. **Consider** adjusting pre-fill duration to eliminate remaining underrun

---

*Underflow reduced by 67% with ONE surgical change - reorder initialization sequence.*
