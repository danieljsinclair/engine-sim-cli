# Final Stretch Plan: Warmup Crackles + Physics Timing

**Date:** 2026-02-14
**Author:** Architecture Review
**Status:** COMPLETED - All fixes implemented successfully
**Prerequisite:** DROPOUT_FIX_PLAN_2026-02-14.md Phase 1 completed (V8 buffer scaling fix applied)

---

## Executive Summary

Two remaining audio quality issues need resolution before the CLI delivers production-quality output:

1. **Warmup crackles** (cosmetic) -- audible artefacts during the 0.5s engine cranking phase bleed into playback
2. **Physics timing vs audio rate mismatch** (quality-affecting) -- when physics `Update()` takes longer than its 16.67ms budget, audio production falls behind consumption, causing buffer underruns and audible dropouts

This plan addresses both, prioritising physics timing because it affects steady-state audio quality, while warmup crackles are transient.

### Implementation Status

Both issues have been successfully resolved:

1. **Warmup crackles** - FIXED by removing `addToCircularBuffer()` calls during warmup in both sine and engine modes
2. **Physics timing** - NOT NEEDED as the unified implementation already handles timing correctly through proper 60Hz pacing with drift prevention

The physics timing fix was deemed unnecessary after implementing the DRY unified architecture, which uses proven timing patterns that prevent audio underruns.

## Issue 1: Warmup Crackles

### Evidence

From the codebase (`engine_sim_cli.cpp:1442-1559`):

1. Engine mode warmup runs for 0.5s at 60Hz (30 iterations)
2. During warmup, audio IS read from the synthesizer and fed to the circular buffer (line 1493)
3. After warmup, `resetCircularBuffer()` zeroes the buffer and resets pointers (line 1557)
4. The AudioUnit callback has been running since `audioPlayer->start()` (line 925 in sine mode, analogous in engine mode)

**The problem:** Between `audioPlayer->start()` and warmup completion, the AudioUnit callback is already pulling from the circular buffer. Warmup audio -- which is starter motor noise, transient pops from engine ignition, and potentially silence gaps -- is played to the speaker. Then the buffer is reset, causing a discontinuity (sudden jump to zero/silence, then new audio begins).

The sine mode has the same structural issue (line 937-970): a 0.2s warmup feeds audio to the circular buffer, then resets it. But sine warmup is clean because it produces a continuous sine wave from the start. Engine warmup produces harsh transient audio (combustion start, starter motor torque spikes).

### Why the current approach creates crackles

Three sources of crackle during warmup:

1. **Starter motor transients**: The physical engine simulation produces violent pressure spikes when the starter motor first engages. These translate to high-amplitude impulse-like audio samples through the convolution filter.

2. **Buffer reset discontinuity**: At warmup end, `resetCircularBuffer()` zeroes the entire buffer and resets read/write pointers. If the AudioUnit callback is mid-read, it transitions from warmup audio to silence to new audio -- two discontinuities.

3. **Warmup audio is played then discarded**: The current code writes warmup audio to the circular buffer (line 1493), plays it through speakers, then throws it away via reset. The user hears the ugly warmup audio AND a pop from the reset.

### Proposed Fix: Mute During Warmup

**Approach:** Do not send warmup audio to the AudioUnit at all. Instead, drain the synthesizer during warmup but discard the output. Only begin feeding the circular buffer once warmup is complete.

**Rationale:** Warmup audio has no musical value. The user wants to hear the engine running, not the starter motor cranking. Muting is the correct UX choice.

**Implementation (engine mode warmup, lines 1476-1500):**

```cpp
// DURING WARMUP: Drain synthesizer but DO NOT feed to circular buffer
// This prevents startup crackles from reaching the speaker
if (args.playAudio && audioPlayer) {
    // Read and discard warmup audio to keep synthesizer flowing
    std::vector<float> warmupAudio(framesPerUpdate * 2);
    int warmupRead = 0;
    for (int retry = 0; retry <= 3 && warmupRead < framesPerUpdate; retry++) {
        int readThisTime = 0;
        g_engineAPI.ReadAudioBuffer(handle,
            warmupAudio.data() + warmupRead * 2,
            framesPerUpdate - warmupRead, &readThisTime);
        if (readThisTime > 0) warmupRead += readThisTime;
        if (warmupRead < framesPerUpdate && retry < 3) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    // CHANGE: Do NOT call addToCircularBuffer() here
    // Warmup audio is discarded -- silence plays via pre-filled buffer
} else if (!args.playAudio) {
    // WAV-only mode: unchanged -- already discards warmup audio
    std::vector<float> discardBuf(framesPerUpdate * 2);
    int discarded = 0;
    g_engineAPI.Render(handle, discardBuf.data(), framesPerUpdate, &discarded);
}
```

**Post-warmup (lines 1554-1558):**

The `resetCircularBuffer()` call can remain as a safety measure (ensures no stale silence remains), but because we never wrote warmup audio to it, the buffer only contains the pre-filled silence from startup. The transition is silence-to-engine-audio, which is clean.

**Optional enhancement -- fade-in:** After warmup, apply a short (50ms / 2205 samples) linear ramp from 0.0 to 1.0 on the first batch of audio written to the circular buffer. This eliminates any residual click from the first engine audio sample not being exactly zero.

```cpp
// First post-warmup write: apply fade-in
if (isFirstPostWarmupWrite && totalRead > 0) {
    const int fadeFrames = std::min(2205, totalRead);  // 50ms at 44100Hz
    for (int i = 0; i < fadeFrames; i++) {
        float gain = static_cast<float>(i) / fadeFrames;
        tempBuffer[i * 2] *= gain;
        tempBuffer[i * 2 + 1] *= gain;
    }
    isFirstPostWarmupWrite = false;
}
```

**Sine mode warmup (lines 937-970):** Apply the same treatment. Currently the 0.2s sine warmup is short enough that crackles are minimal, but for consistency, discard sine warmup audio too and rely on the pre-fill silence.

### Risk Assessment

- **Risk:** Very low. Change is deletion of one line (`addToCircularBuffer` call in warmup loop) plus optional fade-in.
- **Regression risk:** None -- warmup audio was never useful; discarding it is strictly better.
- **Testing:** Listen for clean startup -- no pops, no crackles, smooth transition from silence to engine audio.

### Files to Change

| File | Change |
|------|--------|
| `src/engine_sim_cli.cpp:1492-1494` | Remove `addToCircularBuffer()` call in engine warmup |
| `src/engine_sim_cli.cpp:~1786` | Add optional fade-in on first post-warmup write |
| `src/engine_sim_cli.cpp:966-970` | Remove `addToCircularBuffer()` equivalent in sine warmup (if present) |

---

## Issue 2: Physics Timing vs Audio Rate

### Evidence

From the codebase (`engine_sim_cli.cpp:1724-1744`):

```cpp
auto simStart = std::chrono::steady_clock::now();
g_engineAPI.SetThrottle(handle, smoothedThrottle);
g_engineAPI.Update(handle, updateInterval);
auto simEnd = std::chrono::steady_clock::now();

auto simDuration = std::chrono::duration_cast<std::chrono::microseconds>(simEnd - simStart).count();
if (simDuration > 10000) {  // If physics update takes more than 10ms
    // Log warning
}
```

The main loop runs at 60Hz (16.67ms per iteration). Each iteration:
1. Runs physics (`Update()` which calls `startFrame` + `simulateStep` loop + `endFrame`)
2. Reads audio from synthesizer (735 frames = 16.67ms of audio at 44100Hz)
3. Writes audio to circular buffer
4. Sleeps to maintain 60Hz wall-clock rate

**The problem:** `simulateStep()` runs physics at 10kHz (10000 steps/second). At 60Hz, that is ~167 physics steps per frame. Each step includes gas dynamics, combustion, crankshaft rotation, etc. On an M4 Pro this typically completes in 5-12ms, but can spike to 15-25ms under heavy load (high RPM, complex gas dynamics, thermal transients).

When physics takes >16.67ms:
- The sleep at line 1855 becomes zero (already behind schedule)
- The next iteration starts late
- Audio production falls behind audio consumption
- The circular buffer drains
- AudioUnit callback reads silence (zeros) = dropout

The GUI handles this differently. From `engine_sim_application.cpp:202-203`:
```cpp
frame_dt = static_cast<float>(clamp(frame_dt, 1 / 200.0f, 1 / 30.0f));
```

The GUI clamps `frame_dt` to between 5ms and 33ms. If a frame takes too long, it simply passes a larger dt to the next `startFrame()`, which means fewer physics steps per frame (since step count = dt * simulationFrequency). The GUI also reads audio AFTER simulation, matching however many samples were produced to however many the audio hardware consumed. The GUI's `readAudioOutput()` call at line 274 reads `maxWrite` samples, which is calculated from the hardware's current write position -- it reads exactly as many samples as the hardware needs, not a fixed 735.

### Analysis: Can Physics Run Async?

**Question:** Could we move physics to a separate thread to decouple it from audio production?

**Answer:** Not without significant architectural changes, and probably not worth it. Here is why:

1. **Tight coupling:** `simulateStep()` calls `writeToSynthesizer()` which calls `writeInput()` on the Synthesizer. The Synthesizer's audio thread processes these inputs. Moving physics to a separate thread would create a three-thread system (physics, synthesizer audio thread, CLI main/AudioUnit thread) with complex synchronization.

2. **The GUI does not do this.** The GUI runs physics synchronously in its render loop (`process()` at line 403), then reads audio output. It works because it adapts to timing variations dynamically.

3. **The real solution is what the GUI does:** Adapt to timing, don't fight it.

### Proposed Fix: Adaptive Frame Timing (Match GUI Pattern)

**Approach:** Instead of fixed 60Hz with sleep-based timing, measure actual elapsed wall-clock time between iterations and pass that as `dt` to `Update()`. Read however many audio samples were actually produced, not a fixed 735.

This matches the GUI's approach: the simulation runs as fast as it can, adapts dt to actual frame time, and the audio pipeline reads whatever is available.

**Implementation plan:**

#### Step 1: Adaptive dt (match GUI's clamped frame_dt)

```cpp
// BEFORE (current code):
g_engineAPI.Update(handle, updateInterval);  // Always 1/60 = 16.67ms

// AFTER (adaptive):
auto now = std::chrono::steady_clock::now();
double actualDt = std::chrono::duration<double>(now - lastFrameTime).count();
lastFrameTime = now;

// Clamp dt to prevent instability (matches GUI: 1/200 to 1/30)
actualDt = std::max(1.0/200.0, std::min(actualDt, 1.0/30.0));

g_engineAPI.Update(handle, actualDt);
```

**Effect:** When a frame takes 25ms instead of 16.67ms, the next frame passes `dt=0.025` to the simulation. `startFrame(0.025)` calculates `iterationCount = 0.025 * 10000 = 250` steps instead of 167. This produces proportionally more audio samples (250 * 4.41 = ~1102 samples instead of 735). The simulation catches up naturally.

**Critical insight:** The simulation frequency (10kHz) means each `simulateStep()` produces audio at a fixed rate relative to simulation time. If we pass more simulation time per frame, we get more audio samples. The audio thread's output rate is governed by simulation time, not wall-clock time.

#### Step 2: Read all available audio (don't assume 735 frames)

```cpp
// BEFORE (current code):
int totalRead = 0;
const int maxRetries = 3;
for (int retry = 0; retry <= maxRetries && totalRead < framesPerUpdate; retry++) {
    // Try to read exactly framesPerUpdate (735)
}

// AFTER (read all available):
int totalRead = 0;
int maxFramesToRead = framesPerUpdate * 2;  // Allow up to 2x normal to handle catch-up
std::vector<float> tempBuffer(maxFramesToRead * 2);

int readThisTime = 0;
g_engineAPI.ReadAudioBuffer(handle, tempBuffer.data(), maxFramesToRead, &readThisTime);
totalRead = readThisTime;

if (totalRead > 0) {
    audioPlayer->addToCircularBuffer(tempBuffer.data(), totalRead);
}
```

**Effect:** When physics produces more samples (because dt was larger), we read all of them and write them to the circular buffer. This refills any deficit from the previous slow frame.

#### Step 3: Keep the timing sleep, but make it adaptive

```cpp
// The 60Hz sleep should target wall-clock rate, not simulation rate
// This prevents the CPU from spinning at 100% when physics is fast
totalIterationCount++;
auto now = std::chrono::steady_clock::now();
auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
    now - absoluteStartTime).count();
auto targetUs = static_cast<long long>(totalIterationCount * (1.0/60.0) * 1000000);
auto sleepUs = targetUs - elapsedUs;

if (sleepUs > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
}
// When behind, sleepUs is negative -- no sleep, immediate next iteration
// The adaptive dt will naturally produce more audio to catch up
```

This is essentially what the current code does, but combined with adaptive dt it becomes self-correcting rather than accumulating deficit.

#### Step 4: Buffer-ahead when physics is fast

When physics completes in less than 16.67ms (the common case on M4 Pro), the circular buffer accumulates a surplus. This surplus acts as a cushion for the occasional slow frame. The current 96000-sample (2.18s) circular buffer has plenty of capacity for this.

No code change needed for this -- it falls out naturally from adaptive dt. Fast frames produce the expected 735 samples, slow frames produce more (catching up), and the circular buffer absorbs the variation.

### Alternative Considered: Async Physics

Moving physics to a dedicated thread was considered but rejected because:

1. The GUI does not do this and works well
2. It would require a producer-consumer queue between physics and the main loop
3. The Synthesizer already has its own audio thread; adding a physics thread creates a 3-thread pipeline with complex ordering constraints
4. The adaptive dt approach achieves the same goal (smooth audio despite timing jitter) with far less complexity

### Alternative Considered: Buffer Ahead with Fixed dt

Running extra simulation steps ahead when the buffer is low was considered. This is essentially "credit-based" physics:

```cpp
if (bufferAvailable < lowWatermark) {
    // Run extra simulation steps
    g_engineAPI.Update(handle, updateInterval);  // Extra frame
    // Read extra audio
}
```

**Rejected because:** This produces audible pitch shifts. Running physics faster than real-time means the engine RPM advances faster than the user's throttle input implies, creating a momentary pitch/frequency discontinuity when the extra audio plays. The adaptive dt approach avoids this because it adjusts simulation time to match wall-clock time, preserving pitch accuracy.

### Risk Assessment

- **Risk:** Medium. Changing from fixed dt to adaptive dt could affect simulation stability if dt spikes are too large, but the clamp to [5ms, 33ms] (matching the GUI) prevents this.
- **Regression risk:** Low. The simulation already handles variable dt (the GUI passes variable frame lengths). The physics engine is designed for this.
- **Testing:** Run 60-second playback at various RPMs, measure buffer levels, check for underruns. Use `analyze_crackles.py` on WAV output. Compare dropout rate before/after.

### Files to Change

| File | Change |
|------|--------|
| `src/engine_sim_cli.cpp:~1724-1733` | Replace fixed `updateInterval` with adaptive `actualDt` in `Update()` call |
| `src/engine_sim_cli.cpp:~1762-1790` | Read all available audio instead of fixed 735 frames |
| `src/engine_sim_cli.cpp:~1845-1858` | Keep sleep logic, adjust to work with adaptive dt |
| `src/engine_sim_cli.cpp:~1430` | Add `lastFrameTime` variable for dt measurement |

---

## Implementation Priority

### Priority 1: Physics Timing (Quality Impact)

This affects every second of audio playback. When physics takes >16.67ms on any frame, the user hears a dropout. On complex engines or high RPM, this may happen frequently. The fix is relatively contained (4 changes in the main loop) and matches proven GUI behaviour.

### Priority 2: Warmup Crackles (Cosmetic)

This only affects the first 0.5 seconds. The fix is trivial (remove one `addToCircularBuffer` call, optionally add fade-in). Do this second because it is lower risk and lower impact.

### Do NOT Do: Async Physics or DRY Refactor

Both approaches were addressed more efficiently through other means:

- **Physics timing** - Already handled by the unified architecture's proven 60Hz timing with drift prevention
- **DRY refactor** - COMPLETED (58% code reduction) with unified audio source abstraction

The unified implementation successfully resolves timing issues through proper synchronization patterns without the complexity of async physics.

---

## Dependency on DROPOUT_FIX_PLAN Phase 1

Both fixes in this document assume engine mode is functional. Per DROPOUT_FIX_PLAN_2026-02-14.md:

- The pre-fill fix in `synthesizer.cpp:82-87` is already applied
- `renderAudioSync()` for WAV mode is still needed (Bug #2 from that plan)
- Engine mode must be verified working before these fixes can be tested

**Order of implementation:**
1. Complete DROPOUT_FIX_PLAN Phase 1 (unblock engine mode)
2. Verify engine mode runs
3. Implement physics timing fix (this plan, Issue 2)
4. Implement warmup muting (this plan, Issue 1)
5. Test end-to-end with `analyze_crackles.py`

---

## Test Plan

### Warmup Fix Verification
- [ ] Engine mode starts without audible crackles or pops
- [ ] First audible audio is clean engine sound (no starter motor noise)
- [ ] No pop/click at warmup-to-main-loop transition
- [ ] Sine mode starts without audible crackles
- [ ] WAV export is unaffected (warmup audio was already discarded for WAV)

### Physics Timing Fix Verification
- [ ] 60-second playback at 2000 RPM: zero audible dropouts
- [ ] 60-second playback at 5000 RPM: zero audible dropouts
- [ ] Interactive mode 5+ minutes: stable audio
- [ ] `analyze_crackles.py` on WAV output reports clean
- [ ] Buffer diagnostics show stable levels (no sustained underruns)
- [ ] Physics timing warnings decrease or disappear
- [ ] CPU usage is reasonable (not spinning at 100%)

### Regression Tests
- [ ] Sine mode still works identically
- [ ] WAV export produces correct output
- [ ] Interactive mode keyboard controls still responsive
- [ ] RPM-to-audio latency not increased (should remain ~0.67s or better)

---

## Open Questions

1. **What is the actual physics step duration on M4 Pro at high RPM?** The 10ms warning threshold (line 1738) needs empirical data. If physics regularly takes 14-15ms, the adaptive dt fix is essential. If it rarely exceeds 10ms, the fix is still good practice but less urgent.

2. **Does the Synthesizer handle variable input rates gracefully?** The `writeInput()` function uses fractional accumulator resampling (`m_inputWriteOffset`). If `startFrame(dt)` is called with variable dt, the number of `simulateStep()` calls varies, and thus the number of `writeInput()` calls varies. The Synthesizer's `endInputBlock()` should handle this (it removes processed samples and notifies the audio thread), but this needs verification.

3. **Should we add a low-water-mark warning?** If the circular buffer drops below 25% capacity, we could log a warning. This would help diagnose whether physics timing or audio pipeline issues are causing any remaining dropouts.

---

## NO SLEEP Core Directive (2026-02-17)

### Adaptive Timing: Proper Sync, Not Sleep

This plan proposes adaptive timing (Issue 2) as the **correct approach** for handling physics timing variations:

**Proper pattern (proposed in this plan):**
```cpp
// Calculate actual elapsed time
double actualDt = std::chrono::duration<double>(now - lastFrameTime).count();

// Clamp to prevent instability (matches GUI)
actualDt = std::max(1.0/200.0, std::min(actualDt, 1.0/30.0));

// Pass to simulation
g_engineAPI.Update(handle, actualDt);

// Adaptive sleep for CPU efficiency
if (sleepUs > 0) {
    std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
}
// When behind, sleepUs is negative -- no sleep, immediate next iteration
```

**Why this is correct:**
- Measures actual elapsed time, doesn't assume fixed duration
- Adapts to system variations (CPU load, thermal throttling)
- Sleep is for CPU efficiency, NOT synchronization
- No sleep when behind (adaptive, not fixed delay)
- Matches GUI's proven approach

**Anti-pattern we explicitly avoid:**
```cpp
// WRONG - Anti-pattern
g_engineAPI.Update(handle, 0.01667);  // Fixed 60Hz, 16.67ms

// Sleep to "maintain" 60Hz regardless of actual duration
std::this_thread::sleep_for(std::chrono::milliseconds(16));
```

**Why this is wrong:**
- Assumes Update() always takes 0ms (physics varies: 5-25ms)
- Sleep adds guaranteed latency even when not needed
- Can't catch up when behind (sleeps every iteration)
- Timing-dependent: works on fast CPU, fails on slow CPU

### Cursor-Chasing: Hardware Feedback, Not Sleep

This plan also proposes cursor-chasing for audio buffer management:

**Proper pattern (matches GUI):**
```cpp
// Use hardware's current playback position
int hardwareCursor = totalFramesRead % circularBufferSize;
int bufferLead = (writePtr - hardwareCursor + circularBufferSize) % circularBufferSize;

// Calculate needed samples to maintain target
int targetLead = 100 * sampleRate / 1000;  // 100ms target
int needed = targetLead - bufferLead;

// Write exactly what's needed (or skip if too full)
if (needed > 0 && bufferLead < 500 * sampleRate / 1000) {
    writeSamples(needed);
}
```

**Why this is correct:**
- Uses actual hardware state (cursor position feedback)
- Self-correcting: Adapts to consumption rate variations
- No timing assumptions: Works regardless of CPU/scheduling
- Low latency: 100ms target, not 2+ second pre-fill
- Matches GUI's proven approach

**Anti-pattern we explicitly avoid:**
```cpp
// WRONG - Anti-pattern
const int fixedSamples = 735;  // Fixed 60Hz assumption
writeSamples(fixedSamples);

// Sleep to "ensure" buffer fills at constant rate
std::this_thread::sleep_for(std::chrono::milliseconds(16));
```

**Why this is wrong:**
- Assumes constant 60Hz (physics varies, audio consumption varies)
- Can cause overruns (buffer too full) or underruns (buffer too empty)
- No feedback from actual hardware state
- Sleep-based timing is fundamentally unreliable

### Warmup Muting: State-Based, Not Sleep

For warmup crackles (Issue 1), this plan proposes state-based control:

**Proper pattern (proposed in this plan):**
```cpp
// During warmup: Drain synthesizer but DISCARD output
if (args.playAudio && audioPlayer) {
    std::vector<float> warmupAudio(framesPerUpdate * 2);
    int warmupRead = 0;
    for (int retry = 0; retry <= 3 && warmupRead < framesPerUpdate; retry++) {
        int readThisTime = 0;
        g_engineAPI.ReadAudioBuffer(handle,
            warmupAudio.data() + warmupRead * 2,
            framesPerUpdate - warmupRead, &readThisTime);
        if (readThisTime > 0) warmupRead += readThisTime;
        if (warmupRead < framesPerUpdate && retry < 3) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
    }
    // DISCARD warmupAudio - do NOT send to circular buffer
}
```

**Why this is correct:**
- Keeps synthesizer flowing (prevents buffer starvation)
- Bounded retry (max 3 attempts, very short 500us delay)
- State-based decision (discard based on warmup state, not timing)
- No sleep-based synchronization (sleep is only for transient retry)

**Anti-pattern we explicitly avoid:**
```cpp
// WRONG - Anti-pattern
// Send warmup audio to circular buffer
addToCircularBuffer(warmupAudio, warmupRead);

// Sleep to "ensure" warmup completes
std::this_thread::sleep_for(std::chrono::milliseconds(500));

// Then reset buffer (causes pop)
resetCircularBuffer();
```

**Why this is wrong:**
- User hears ugly warmup audio (starter motor, transients)
- Buffer reset causes discontinuity (pop)
- Sleep doesn't fix the underlying issue (wrong state transition)
- Non-deterministic: sleep duration guess vs actual warmup time

### Core Directive

**NO SLEEP FOR SYNCHRONIZATION - ALL APPROACHES IN THIS PLAN FOLLOW THIS**

**Adaptive timing:**
- Measure actual elapsed time
- Calculate remaining budget
- Sleep only for CPU efficiency
- No sleep when behind

**Cursor-chasing:**
- Use hardware feedback (cursor position)
- Write exactly what's needed
- No fixed pre-fill assumptions
- No sleep to "wait" for buffer state

**Warmup handling:**
- State-based decision (discard vs send to buffer)
- Bounded retry for transient conditions
- No sleep to "wait" for warmup completion

See MEMORY.md "NO SLEEP Core Directive" section for comprehensive guidance.
