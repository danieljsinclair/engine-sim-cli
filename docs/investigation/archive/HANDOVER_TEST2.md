# Handover: Test 2 Implementation - Fixed-Interval Rendering

**Date:** 2025-02-04
**Status:** Root cause identified, Test 2 ready to implement
**Platform:** macOS M4 Pro (Apple Silicon)
**Project:** engine-sim-cli audio crackle investigation

## Executive Summary

**BREAKTHROUGH:** Root cause identified! The synthesizer's audio thread uses `wait()` with a condition variable that has **highly unpredictable timing**, causing burst writes that create discontinuities.

**Test 1 Results:**
- Audio thread wakeups range from **0 to 1,210,442 microseconds** (0 to 1.2 seconds!)
- Normal: 6-12ms, Abnormal: 23-31ms, Extreme: 1.2 seconds
- Burst writes: 1411 samples (3x normal size)
- 18 discontinuities detected in 10 seconds
- All discontinuities correlate with abnormal wakeups

**Hypothesis 4: CONFIRMED**

**Next Step:** Implement Test 2 - Fixed-Interval Rendering to verify the fix.

## What We Know

### Root Cause (CONFIRMED)

The `m_cv0.wait()` in `synthesizer.cpp` line 231 has unpredictable wake-up timing:
- OS scheduler variability
- Condition variable notification latency
- Competing threads for CPU time

This causes:
1. Long wakeups (up to 1.2 seconds)
2. Burst writes (up to 3x normal size)
3. Audio discontinuities (crackles)

### Why This Matters

When the audio thread finally wakes up after a long delay, it tries to "catch up" by writing large bursts of samples. These burst writes cause the synthesizer to generate audio with large jumps between consecutive samples, creating audible crackles.

### Why Sine Mode Works

Sine mode has simpler audio generation:
- No convolution, no filters
- Less CPU time per sample
- More predictable execution time
- Same audio thread architecture, less stress on timing

Result: Zero discontinuities in sine mode.

## Test 2 Implementation Plan

### Objective

Verify that predictable timing eliminates discontinuities.

### Hypothesis

Replacing the condition variable with a timed wait and implementing fixed-interval rendering will provide predictable timing, eliminating burst writes and discontinuities.

### Code Changes Required

**File:** `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Function:** `renderAudio()` (lines 221-266)

**Current Implementation:**
```cpp
void Synthesizer::renderAudio() {
    // DIAGNOSTIC: Log wakeup timing
    static auto wakeupStart = std::chrono::steady_clock::now();
    static int wakeupCount = 0;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - wakeupStart).count();

    std::unique_lock<std::mutex> lk0(m_lock0);

    m_cv0.wait(lk0, [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;
        return !m_run || (inputAvailable && !m_processed);
    });

    const int n = std::min(
        std::max(0, 2000 - (int)m_audioBuffer.size()),
        (int)m_inputChannels[0].data.size());

    fprintf(stderr, "[AUDIO THREAD WAKEUP #%d] Time:%lldus BufferSize:%zu Writing:%d samples\n",
            ++wakeupCount, elapsed, m_audioBuffer.size(), n);

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_filters[i].airNoiseLowPass.setCutoffFrequency(
            static_cast<float>(m_audioParameters.airNoiseFrequencyCutoff), m_audioSampleRate);
        m_filters[i].jitterFilter.setJitterScale(m_audioParameters.inputSampleNoise);
    }

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    wakeupStart = now;
    m_cv0.notify_one();
}
```

**Proposed Implementation:**
```cpp
void Synthesizer::renderAudio() {
    // DIAGNOSTIC: Log wakeup timing
    static auto wakeupStart = std::chrono::steady_clock::now();
    static int wakeupCount = 0;
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - wakeupStart).count();

    // Test 2: Fixed-interval rendering
    static const int targetSamplesPerWakeup = 441;  // 10ms @ 44.1kHz
    static const int targetWakeupIntervalUs = 10000;  // 10ms in microseconds

    std::unique_lock<std::mutex> lk0(m_lock0);

    // Test 2: Use timed wait with 5ms timeout for predictable wakeup
    // This ensures we wake up regularly even if condition variable is slow
    m_cv0.wait_for(lk0, std::chrono::milliseconds(5), [this] {
        const bool inputAvailable =
            m_inputChannels[0].data.size() > 0
            && m_audioBuffer.size() < 2000;
        return !m_run || (inputAvailable && !m_processed);
    });

    // Test 2: Calculate how many samples to write (fixed amount)
    // This prevents burst writes that cause discontinuities
    const int n = std::min(
        targetSamplesPerWakeup,  // Fixed: 441 samples
        std::min(
            std::max(0, 2000 - (int)m_audioBuffer.size()),
            (int)m_inputChannels[0].data.size()));

    fprintf(stderr, "[AUDIO THREAD WAKEUP #%d] Time:%lldus Target:%dus BufferSize:%zu Writing:%d samples\n",
            ++wakeupCount, elapsed, targetWakeupIntervalUs, m_audioBuffer.size(), n);

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.read(n, m_inputChannels[i].transferBuffer);
    }

    m_inputSamplesRead = n;
    m_processed = true;

    lk0.unlock();

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_filters[i].airNoiseLowPass.setCutoffFrequency(
            static_cast<float>(m_audioParameters.airNoiseFrequencyCutoff), m_audioSampleRate);
        m_filters[i].jitterFilter.setJitterScale(m_audioParameters.inputSampleNoise);
    }

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    wakeupStart = now;
    m_cv0.notify_one();
}
```

### Key Changes

1. **Added fixed-interval constants:**
   - `targetSamplesPerWakeup = 441` (10ms @ 44.1kHz)
   - `targetWakeupIntervalUs = 10000` (10ms in microseconds)

2. **Replaced `wait()` with `wait_for()`:**
   - Old: `m_cv0.wait(lk0, [this] { ... });`
   - New: `m_cv0.wait_for(lk0, std::chrono::milliseconds(5), [this] { ... });`
   - This ensures timeout after 5ms if condition is not met

3. **Changed sample calculation to fixed amount:**
   - Old: Variable based on buffer size (up to 1411 samples)
   - New: Fixed at 441 samples maximum

### Expected Outcomes

**If the hypothesis is correct:**
1. Wakeup timing will be more consistent (~10ms)
2. Write amounts will be consistent (441 samples)
3. Discontinuities will be eliminated or significantly reduced
4. Audio output will be smooth without crackles

**Metrics to Measure:**
- Wakeup timing distribution (should be tight around 10ms)
- Write amounts (should be consistently 441 samples)
- Discontinuity counts (should be zero or minimal)
- Audio output quality (should be smooth)

### Build Commands

```bash
cd /Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/build
cmake .. && make
cd /Users/danielsinclair/vscode/engine-sim-cli
make
```

### Test Commands

```bash
# Engine mode test (10 seconds)
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee test2_engine.log

# Sine mode test (10 seconds)
./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee test2_sine.log
```

### Success Criteria

1. **Wakeup timing**: Consistent ~10ms (no more 1.2 second outliers)
2. **Write amounts**: Consistent 441 samples (no more 1411 sample bursts)
3. **Discontinuities**: Zero or minimal (vs 18 in Test 1)
4. **Audio quality**: Smooth, no audible crackles

### Failure Modes

**If Test 2 fails:**
1. Discontinuities persist - timing may not be the only factor
2. New issues introduced - timed wait may cause other problems
3. No improvement - root cause may be elsewhere

**Next steps if Test 2 fails:**
1. Investigate real-time audio thread priorities
2. Implement double/triple buffering
3. Consider SIMD optimizations
4. Investigate GUI vs CLI differences

## Documentation Files

### Existing Documentation

1. **TEST_INVESTIGATION_LOG.md** - Complete chronological test record
2. **AUDIO_THEORIES_TRACKING.md** - All theories and evidence
3. **TEST1_AUDIO_THREAD_WAKEUP_ANALYSIS.md** - Detailed Test 1 analysis
4. **TEST1_EVIDENCE_SUMMARY.md** - Test 1 evidence summary
5. **engine_test.log** - Test 1 engine mode output
6. **sine_test.log** - Test 1 sine mode output

### Files to Update After Test 2

1. **TEST_INVESTIGATION_LOG.md** - Add Test 2 entry with results
2. **AUDIO_THEORIES_TRACKING.md** - Update status based on Test 2 results
3. **test2_engine.log** - Save test output
4. **test2_sine.log** - Save test output
5. **TEST2_RESULTS.md** - Create new analysis document

## Critical Reminders

### What NOT to Do

❌ **Don't change the diagnostic format** - We need to compare Test 1 and Test 2 results
❌ **Don't remove timing diagnostics** - They're critical for verification
❌ **Don't skip testing** - Must test both engine and sine modes
❌ **Don't speculate without evidence** - Measure everything

### What to Do

✅ **Keep diagnostic format consistent** - Add Target field, keep everything else
✅ **Measure everything** - Wakeup timing, write amounts, discontinuities
✅ **Compare with Test 1** - Use Test 1 as baseline
✅ **Document failures** - If it doesn't work, that's valuable data
✅ **Listen to the audio** - Your ears are the ultimate test

### Testing Checklist

- [ ] Build successful
- [ ] Engine mode test (10 seconds)
- [ ] Sine mode test (10 seconds)
- [ ] Analyze wakeup timing distribution
- [ ] Analyze write amount distribution
- [ ] Count discontinuities
- [ ] Listen to audio output
- [ ] Compare with Test 1 results
- [ ] Document findings

## Code Locations Reference

**Files to modify:**
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp` (lines 221-266)

**Files to reference:**
- `/Users/danielsinclair/vscode/engine-sim-cli/TEST1_AUDIO_THREAD_WAKEUP_ANALYSIS.md` (Test 1 results)
- `/Users/danielsinclair/vscode/engine-sim-cli/TEST_INVESTIGATION_LOG.md` (Test log)
- `/Users/danielsinclair/vscode/engine-sim-cli/AUDIO_THEORIES_TRACKING.md` (Theories)

## Context

This is the culmination of extensive investigation:

1. **Phase 1:** Ruled out position tracking errors, update rate differences, audio library choice, double buffer consumption, underruns as primary cause

2. **Phase 2:** Identified root cause - audio thread timing unpredictability

3. **Test 1:** Confirmed Hypothesis 4 with overwhelming evidence

4. **Test 2:** (This handover) Implement fix and verify

## Success Criteria

**Complete Test 2 implementation:**
- Code changes implemented
- Build successful
- Tests executed
- Results analyzed
- Documentation updated

**Determine if fix works:**
- If discontinuities eliminated → Problem solved
- If discontinuities reduced → Partial success, iterate
- If no improvement → Back to drawing board

## Contact

**Questions?**
- Review existing documentation first
- Check TEST_INVESTIGATION_LOG.md for complete history
- Check AUDIO_THEORIES_TRACKING.md for all theories
- Use evidence, not speculation

---

**Remember:** NO SPECULATION - ONLY EVIDENCE

Every theory must be tested with diagnostics. Every result must be documented. Failed tests are valuable - they rule out possibilities and narrow the search.

**Good luck with Test 2!**
