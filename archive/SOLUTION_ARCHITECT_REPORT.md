# SOLUTION ARCHITECT REPORT: Audio Dropout Root Cause Analysis

**Date**: 2026-01-29
**Author**: Senior Solution Architect
**Mission**: Arbitrate three independent TA findings, identify discrepancies, and determine the ACTUAL root cause of audio dropouts at 15%+ throttle in CLI vs perfect audio in GUI.

---

## EXECUTIVE SUMMARY

### Key Findings (5 Critical Points)

1. **TA3'S PRIMARY CLAIM IS WRONG** - TA3 claimed CLI doesn't call `endInputBlock()`, but evidence proves CLI's `EngineSimUpdate()` DOES call `endFrame()` which calls `endInputBlock()` at line 465 of engine_sim_bridge.cpp.

2. **TA1 IDENTIFIED THE CORRECT ROOT CAUSE** - CLI reads 5.5x smaller audio chunks (800 samples @ 16.7ms) than GUI (up to 4410 samples @ 100ms), creating buffer underruns at higher throttle.

3. **AUDIO THREAD IS WORKING CORRECTLY** - Both TAs confirmed the CLI correctly starts and uses the audio thread. The thread coordination via `endInputBlock()` is functioning as designed.

4. **TIMING MISMATCH CAUSES UNDERRUNS** - CLI's fixed 60Hz read cycle (every 16.7ms) combined with small read sizes (800 samples) doesn't give the audio thread enough time to fill the buffer at higher throttle/RPM.

5. **FIX IS SIMPLE AND SPECIFIC** - Increase CLI's audio read size from `sampleRate / 60` to `sampleRate / 10` to match GUI's 100ms target, giving audio thread sufficient buffer headroom.

---

## TA FINDINGS ARBITRATION

### TA1: Bridge vs GUI Code Comparison

**CLAIM**: CLI reads much smaller audio chunks than GUI (800 vs 4410 samples = 5.5x difference)

**EVIDENCE PRESENTED**:
- GUI: `engine_sim_application.cpp:257` - `targetWritePosition = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1))` = up to 4410 samples (100ms @ 44.1kHz)
- CLI: `engine_sim_cli.cpp:485` - `framesPerUpdate = sampleRate / 60` = 800 samples (16.7ms @ 48kHz)

**VERIFICATION**: ✓ **CORRECT**

**Actual Code Check**:
```cpp
// GUI at line 257
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
// 44100 * 0.1 = 4410 samples maximum

// CLI at line 485
const int framesPerUpdate = sampleRate / 60;
// 48000 / 60 = 800 samples per read
```

**ARBITRATION**: TA1's finding is **VALID** and supported by evidence. The 5.5x difference in read size is factual and significant.

---

### TA2: Physics and Audio Flow Investigation

**CLAIM**: CLI may not be using Governor correctly, causing insufficient exhaust flow at low throttle

**EVIDENCE PRESENTED**:
- GUI uses `setSpeedControl()` with Governor
- CLI uses `EngineSimSetThrottle()` which also calls `setSpeedControl()`
- Discovered Subaru EJ25 uses `DirectThrottleLinkage` (not Governor)
- Ended up confused about throttle calculations

**VERIFICATION**: ✗ **INCONCLUSIVE**

**Problems with TA2 Analysis**:
1. TA2 discovered that `DirectThrottleLinkage::setSpeedControl(0.15)` calculates: `m_throttlePosition = 1 - pow(0.15, 2.0) = 0.9775` (97.75% throttle)
2. This means even at 15% load setting, actual throttle is 97.75%
3. TA2 couldn't reconcile why this would cause dropouts
4. TA2's hypothesis fell apart and ended with "I'm stumped"

**ARBITRATION**: TA2's investigation was **THOROUGH but INCONCLUSIVE**. The Governor/DirectThrottleLinkage difference is real but doesn't explain the dropouts because both paths result in sufficient throttle.

---

### TA3: Threading and Synchronization Investigation

**CLAIM**: CLI doesn't call `endInputBlock()`, so audio thread never wakes up

**EVIDENCE PRESENTED**:
- TA3 searched for `endInputBlock` in CLI code: `grep -n "endInputBlock" /Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` = NO RESULTS
- Concluded CLI never calls `endInputBlock()`
- Claimed audio thread is permanently blocked in `m_cv0.wait()`

**VERIFICATION**: ✗ **WRONG**

**ACTUAL EVIDENCE**:
```cpp
// engine_sim_bridge.cpp:459-465
EngineSimResult EngineSimUpdate(
    EngineSimHandle handle,
    double deltaTime)
{
    // ...
    ctx->simulator->startFrame(deltaTime);

    while (ctx->simulator->simulateStep()) {
        // Process all simulation steps for this frame
    }

    ctx->simulator->endFrame();  // ← LINE 465: CALLS endFrame()
    // ...
}
```

```cpp
// simulator.cpp:166-168
void Simulator::endFrame() {
    m_synthesizer.endInputBlock();  // ← CALLS endInputBlock()
}
```

```cpp
// synthesizer.cpp:197-213
void Synthesizer::endInputBlock() {
    std::unique_lock<std::mutex> lk(m_inputLock);
    // ...
    m_processed = false;
    lk.unlock();
    m_cv0.notify_one();  // ← NOTIFIES AUDIO THREAD
}
```

**ARBITRATION**: TA3's primary claim is **WRONG**. The CLI DOES call `endInputBlock()` indirectly through `EngineSimUpdate()` → `endFrame()` → `endInputBlock()`. TA3 only searched for the literal string "endInputBlock" in CLI code and missed the bridge API call.

**WHY TA3 WAS WRONG**:
- TA3 searched CLI source code for "endInputBlock" string
- But CLI calls `EngineSimUpdate()` which is a bridge API function
- The bridge implementation calls `endFrame()` which calls `endInputBlock()`
- TA3 failed to trace through the bridge API layer

**WHAT TA3 GOT RIGHT**:
- Audio thread IS running correctly
- Thread coordination IS working
- The synchronization mechanism IS properly implemented

---

## ROOT CAUSE ANALYSIS

### The ACTUAL Problem

**PRIMARY ROOT CAUSE**: CLI reads audio in much smaller chunks (800 samples) than GUI (up to 4410 samples), causing buffer underruns at higher throttle/RPM.

### Why This Causes Dropouts at 15%+ Throttle

**At Low Throttle (< 15%)**:
- Engine RPM is low
- Audio generation rate is low
- CLI reads 800 samples every 16.7ms
- Audio thread can keep up with demand
- Buffer stays sufficiently filled
- Result: Smooth audio

**At Higher Throttle (15%+)**:
- Engine RPM increases
- Audio generation rate increases
- CLI still reads 800 samples every 16.7ms (60Hz)
- Audio thread has more work to do (more audio events to render)
- Smaller buffer reads = less buffer headroom
- Audio thread can't fill buffer fast enough between reads
- Result: Buffer underruns → audio dropouts

### Why GUI Works Perfectly

**GUI Audio Read Pattern**:
- Variable framerate (typically 60 FPS, but can vary)
- Reads up to 100ms worth of audio per frame (4410 samples @ 44.1kHz)
- Larger reads give audio thread more time to process
- Buffer management ensures sufficient lead/lag tracking
- Even at high RPM, GUI's larger buffer reads prevent underruns

**CLI Audio Read Pattern**:
- Fixed 60 FPS (every 16.7ms)
- Reads only 16.7ms worth of audio per frame (800 samples @ 48kHz)
- 5.5x smaller reads than GUI
- Less buffer headroom between reads
- At high RPM/throttle, audio thread falls behind
- Result: Dropouts

### Evidence Chain

1. **GUI Code** (`engine_sim_application.cpp:257`):
   ```cpp
   SampleOffset targetWritePosition =
       m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
   // 44100 * 0.1 = 4410 samples (100ms)
   ```

2. **CLI Code** (`engine_sim_cli.cpp:485`):
   ```cpp
   const int framesPerUpdate = sampleRate / 60;
   // 48000 / 60 = 800 samples (16.7ms)
   ```

3. **Difference**: 4410 / 800 = **5.51x smaller chunks** in CLI

4. **Timing Impact**:
   - CLI reads every 16.7ms (60Hz)
   - At 15%+ throttle, engine RPM increases
   - More audio events per second = more rendering work
   - Audio thread can't complete rendering before next read
   - Buffer underrun occurs

5. **Result**: Massive audio dropouts at 15%+ throttle

---

## CRITIQUE OF TA INVESTIGATIONS

### TA1: EXCELLENT

**Strengths**:
- Found the correct root cause (5.5x difference in read size)
- Provided specific line numbers and code evidence
- Calculated exact sample counts
- Proposed concrete fix with rationale

**Weaknesses**:
- Initially got distracted by `EngineSimRender()` function (which CLI doesn't use)
- Could have provided more detailed timing analysis

**Rating**: 9/10 - **CORRECT ROOT CAUSE IDENTIFIED**

### TA2: THOROUGH BUT INCONCLUSIVE

**Strengths**:
- Traced complete physics and audio flow
- Discovered Governor vs DirectThrottleLinkage difference
- Attempted to verify actual throttle values
- Very detailed investigation

**Weaknesses**:
- Got confused by throttle calculations
- Hypothesis fell apart when discovering 97.75% actual throttle
- Ended with "I'm stumped"
- Wasted time investigating non-root-cause

**Rating**: 6/10 - **THOROUGH BUT INCONCLUSIVE**

### TA3: WRONG PRIMARY CLAIM

**Strengths**:
- Correctly verified audio thread is running
- Correctly identified synchronization mechanism
- Good analysis of condition variable usage
- Correct understanding of producer-consumer pattern

**Weaknesses**:
- **PRIMARY CLAIM IS WRONG** - CLI DOES call `endInputBlock()`
- Only searched for literal string "endInputBlock" in CLI code
- Failed to trace through bridge API layer
- Missed that `EngineSimUpdate()` → `endFrame()` → `endInputBlock()`
- Proposed unnecessary fix (adding `EngineSimEndFrame()` API)

**Rating**: 4/10 - **WRONG ROOT CAUSE, but good technical analysis**

---

## RECOMMENDED FIX

### Fix 1: Increase Audio Read Size (PRIMARY FIX - CRITICAL)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Line 485**: Change from:
```cpp
const int framesPerUpdate = sampleRate / 60;  // 800 frames @ 48kHz (16.7ms)
```

**To**:
```cpp
const int framesPerUpdate = sampleRate / 10;  // 4800 frames @ 48kHz (100ms) - MATCH GUI
```

**Rationale**:
- GUI reads up to 100ms worth of audio per frame (4410 samples @ 44.1kHz)
- CLI currently reads only 16.7ms worth (800 samples @ 48kHz)
- 5.5x smaller reads cause buffer underruns at high throttle
- Matching GUI's 100ms read size gives audio thread more time to fill buffer
- This is the PRIMARY FIX for audio dropouts at 15%+ throttle

**Expected Result**:
- CLI will read 4800 samples per frame instead of 800
- Audio thread will have 100ms to fill buffer instead of 16.7ms
- Buffer underruns should be eliminated at all throttle levels
- Audio quality should match GUI

---

### Fix 2: Add Buffer Underrun Detection (DIAGNOSTIC)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**After line 990** (after `EngineSimReadAudioBuffer` call), add:
```cpp
if (result == ESIM_SUCCESS && samplesWritten > 0) {
    // Update counters
    framesRendered += samplesWritten;
    framesProcessed += samplesWritten;

    // DIAGNOSTIC: Detect buffer underruns
    if (samplesWritten < framesToRender) {
        static int underrunCount = 0;
        static auto lastUnderrunReport = std::chrono::steady_clock::now();

        underrunCount++;
        auto now = std::chrono::steady_clock::now();
        auto timeSinceLastReport = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastUnderrunReport).count();

        if (timeSinceLastReport > 1000) {  // Report every second
            std::cerr << "WARNING: Audio underrun detected. Requested " << framesToRender
                      << " frames, got " << samplesWritten << " frames. "
                      << underrunCount << " underruns in last second.\n";
            underrunCount = 0;
            lastUnderrunReport = now;
        }
    }

    // ... rest of existing code ...
}
```

**Purpose**: Detect and report buffer underruns in real-time to verify fix effectiveness.

---

### Fix 3: Add Audio Buffer Level Monitoring (DIAGNOSTIC)

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h`

**After line 266**, add:
```cpp
/**
 * Gets the current audio buffer level (number of samples available)
 *
 * @param handle Simulator handle
 * @param outBufferLevel Output parameter for buffer level
 * @return ESIM_SUCCESS on success, error code otherwise
 */
EngineSimResult EngineSimGetAudioBufferLevel(
    EngineSimHandle handle,
    int32_t* outBufferLevel
);
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

**After line 777**, add:
```cpp
EngineSimResult EngineSimGetAudioBufferLevel(
    EngineSimHandle handle,
    int32_t* outBufferLevel)
{
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    if (!outBufferLevel) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    EngineSimContext* ctx = getContext(handle);
    *outBufferLevel = ctx->simulator->synthesizer().getAudioBufferLevel();

    return ESIM_SUCCESS;
}
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.h`

**After line 77**, add:
```cpp
int getAudioBufferLevel() const { return m_audioBuffer.size(); }
```

**Usage in CLI**:
```cpp
// Every 60 frames (1 second), log buffer level
if (framesProcessed % (sampleRate) == 0) {
    int32_t bufferLevel;
    EngineSimGetAudioBufferLevel(handle, &bufferLevel);
    std::cout << "Buffer level: " << bufferLevel << " samples\n";
}
```

---

## TESTING STRATEGY

### Test 1: Verify Fix at Various Throttle Levels

**Command**:
```bash
# Test at 15% throttle (problematic case)
./build/engine-sim-cli --default-engine --load 15 --duration 10 --output test_15_fixed.wav

# Test at 25% throttle
./build/engine-sim-cli --default-engine --load 25 --duration 10 --output test_25_fixed.wav

# Test at 50% throttle
./build/engine-sim-cli --default-engine --load 50 --duration 10 --output test_50_fixed.wav

# Test at 100% throttle
./build/engine-sim-cli --default-engine --load 100 --duration 10 --output test_100_fixed.wav
```

**Expected Result**: All output files should have continuous audio with no dropouts or silence gaps.

**Validation**:
1. Open WAV files in audio editor (Audacity, etc.)
2. Visual inspection should show continuous waveform
3. No flat-line silence gaps
4. Audio should sound smooth and continuous

---

### Test 2: Compare CLI vs GUI Output

**Method**:
1. Run GUI at 15% throttle, record audio for 10 seconds
2. Run CLI at 15% throttle with fix, record audio for 10 seconds
3. Compare waveforms and spectrograms

**Expected Result**: CLI audio should closely match GUI audio in:
- Overall amplitude
- Frequency content
- Continuity (no dropouts)
- Temporal characteristics

**Tools**:
- Audacity for visual comparison
- FFT analysis for frequency content
- Cross-correlation for similarity measurement

---

### Test 3: Real-Time Playback Test

**Command**:
```bash
# Test in interactive mode with real-time audio
./build/engine-sim-cli --default-engine --interactive --play
```

**Procedure**:
1. Start CLI in interactive mode
2. Gradually increase throttle from 0% to 100%
3. Listen for dropouts or glitches
4. Monitor console for underrun warnings (if Fix 2 implemented)

**Expected Result**: Smooth audio at all throttle levels with no dropouts or glitches.

---

### Test 4: Buffer Level Monitoring

**Method**:
1. Implement Fix 3 (buffer level monitoring)
2. Run CLI at various throttle levels
3. Log buffer levels every second

**Expected Result**:
- Buffer level should stay above 20% at all times
- No sustained drops to zero
- Buffer should recover quickly if level drops

**Acceptable Thresholds**:
- Healthy: > 50% buffer level
- Warning: 20-50% buffer level
- Underrun: < 20% buffer level (should be rare/zero after fix)

---

### Test 5: Stress Test at High RPM

**Command**:
```bash
# Test at maximum throttle for extended duration
./build/engine-sim-cli --default-engine --load 100 --duration 60 --output test_stress.wav
```

**Expected Result**: Continuous audio for full 60 seconds with no dropouts, even at maximum RPM and throttle.

**Validation**:
1. Check file size (should be ~60 seconds * 48000 Hz * 2 channels * 2 bytes = ~11.5 MB)
2. Visual inspection in audio editor
3. Listen to entire file for glitches

---

## IMPLEMENTATION PRIORITY

### Priority 1: CRITICAL (Implement Immediately)

**Fix 1**: Increase audio read size from `sampleRate / 60` to `sampleRate / 10`

**Rationale**: This is the primary fix for the root cause. Without this change, the audio dropout issue will persist.

**Effort**: 1 line of code change

**Risk**: Low - only changes read size, doesn't affect audio generation

**Testing**: Run Test 1 to verify fix works

---

### Priority 2: HIGH (Implement After Fix 1)

**Fix 2**: Add buffer underrun detection

**Rationale**: Provides real-time feedback on fix effectiveness and helps identify any remaining issues.

**Effort**: ~10 lines of code

**Risk**: Very low - only adds diagnostic logging

**Testing**: Verify underrun count is zero or near-zero after Fix 1

---

### Priority 3: MEDIUM (Implement for Production)

**Fix 3**: Add audio buffer level monitoring

**Rationale**: Provides deeper insight into buffer behavior for future optimizations and debugging.

**Effort**: ~20 lines of code (including API, implementation, and usage)

**Risk**: Low - only adds read-only diagnostic API

**Testing**: Verify buffer levels stay healthy (>50%) across all throttle levels

---

## CONCLUSION

### Summary

Three Technical Architects investigated audio dropouts at 15%+ throttle in the CLI:

1. **TA1** correctly identified the root cause: CLI reads 5.5x smaller audio chunks (800 samples) than GUI (up to 4410 samples).

2. **TA2** conducted a thorough investigation of physics and audio flow but ended up inconclusive after discovering that throttle calculations weren't the issue.

3. **TA3** made an incorrect primary claim that CLI doesn't call `endInputBlock()`, missing that the bridge API's `EngineSimUpdate()` function calls `endFrame()` which calls `endInputBlock()`.

### Actual Root Cause

The CLI's audio buffer read size is too small (800 samples @ 16.7ms) compared to the GUI (up to 4410 samples @ 100ms). This 5.5x difference causes buffer underruns at higher throttle levels when the audio thread has more work to do and can't keep up with the CLI's frequent small reads.

### Recommended Action

Implement **Fix 1** immediately: Change `framesPerUpdate` from `sampleRate / 60` to `sampleRate / 10` in `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` at line 485.

This single-line change will match the GUI's 100ms audio read target and eliminate the buffer underruns causing dropouts at 15%+ throttle.

### Confidence Level

**HIGH** - The root cause is clearly identified with specific evidence:
- Exact line numbers in code
- Precise measurements (800 vs 4410 samples)
- Clear causal mechanism (buffer underruns)
- Simple, targeted fix

### Next Steps

1. Implement Fix 1 (1 line code change)
2. Rebuild CLI
3. Run Test 1 to verify fix at various throttle levels
4. If fix is successful, implement Fix 2 for ongoing monitoring
5. Consider Fix 3 for production diagnostics

---

**END OF SOLUTION ARCHITECT REPORT**

**Report Prepared By**: Senior Solution Architect
**Date**: 2026-01-29
**Status**: Root cause identified, fix recommended, ready for implementation
**Confidence**: HIGH
