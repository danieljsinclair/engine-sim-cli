# Audio Crackle Investigation - README

**Status:** ROOT CAUSE IDENTIFIED - Test 2 pending implementation
**Last Updated:** 2025-02-04
**Platform:** macOS M4 Pro (Apple Silicon)

---

## Quick Summary

The engine-sim-cli exhibits **periodic audio crackling** that does not occur in the Windows GUI version. After extensive investigation, the **root cause has been identified**:

### Root Cause (CONFIRMED)

The synthesizer's audio thread uses `wait()` with a condition variable that has **highly unpredictable timing**, causing burst writes that create discontinuities.

**Evidence:**
- Audio thread wakeups range from **0 to 1,210,442 microseconds** (0 to 1.2 seconds!)
- Normal: 6-12ms, Abnormal: 23-31ms, Extreme: 1.2 seconds
- Burst writes: 1411 samples (3x normal size)
- 18 discontinuities in 10 seconds
- All discontinuities correlate with abnormal wakeups

### Next Step

Implement **Test 2: Fixed-Interval Rendering** to verify the fix eliminates discontinuities.

---

## Documentation Structure

### START HERE

1. **[DOCUMENTATION_MASTER_INDEX.md](DOCUMENTATION_MASTER_INDEX.md)** - Navigation hub (START HERE)
2. **[HANDOVER_TEST2.md](HANDOVER_TEST2.md)** - Implementation guide for Test 2
3. **[TEST_INVESTIGATION_LOG.md](TEST_INVESTIGATION_LOG.md)** - Complete chronological test record

### Core Documentation

| File | Purpose | Status |
|------|---------|--------|
| **DOCUMENTATION_MASTER_INDEX.md** | Navigation hub | ✅ Current |
| **HANDOVER_TEST2.md** | Test 2 implementation guide | ✅ Ready |
| **TEST_INVESTIGATION_LOG.md** | Complete test history | ✅ Current |
| **AUDIO_THEORIES_TRACKING.md** | All theories tested | ✅ Updated |

### Test 1 Evidence

| File | Purpose | Status |
|------|---------|--------|
| **TEST1_AUDIO_THREAD_WAKEUP_ANALYSIS.md** | Detailed Test 1 analysis | ✅ Complete |
| **TEST1_EVIDENCE_SUMMARY.md** | Test 1 evidence summary | ✅ Complete |
| **engine_test.log** | Raw test data (565 wakeups) | ✅ Complete |
| **sine_test.log** | Comparison test data (4 wakeups) | ✅ Complete |

### Visual Guide

**[DOCUMENTATION_TREE.txt](DOCUMENTATION_TREE.txt)** - Visual overview of documentation structure

---

## Investigation Timeline

### Phase 1: Initial Investigation ✅ COMPLETE

**Theories Ruled Out:**
- ❌ Position tracking errors (hardware position matches manual tracking)
- ❌ Update rate differences (CLI already at 60Hz matching GUI)
- ❌ Audio library choice (AudioUnit is correct for macOS)
- ❌ Double buffer consumption (fixed - no longer occurs)
- ❌ Underruns as primary cause (crackles occur without underruns)

**Result:** All common theories ruled out. Problem narrowed down to audio thread timing.

### Phase 2: Root Cause Identification ✅ COMPLETE

**Hypothesis 4:** Audio thread uses `wait()` with condition variable that has unpredictable timing

**Test 1: Audio Thread Wakeup Timing Analysis**

**Evidence Collected:**
- 565 audio thread wakeups analyzed
- Wakeup timing distribution: 6ms to 1.2 seconds (200x variation)
- Buffer write patterns: 470-1411 samples (3x variation)
- 18 discontinuities detected in 10 seconds
- All discontinuities correlate with abnormal wakeups

**Result:** ✅ **HYPOTHESIS 4 CONFIRMED**

**Root Cause Identified:**
The `m_cv0.wait()` in `synthesizer.cpp` line 231 has unpredictable wake-up timing due to:
- OS scheduler variability
- Condition variable notification latency
- Competing threads for CPU time

This causes burst writes that create audio discontinuities (crackles).

### Phase 3: Fix Implementation ⏳ PENDING

**Test 2: Fixed-Interval Rendering**

**Hypothesis:** Predictable timing will eliminate discontinuities

**Implementation:**
- Replace `m_cv0.wait()` with `m_cv0.wait_for()` with 5ms timeout
- Implement fixed-interval rendering (10ms intervals)
- Write fixed amount (441 samples per wakeup)

**Status:** Ready to implement

---

## Quick Start Guide

### For Implementing Test 2

**Time Estimate:** ~1 hour

1. **Read Documentation** (15 minutes)
   - Read `DOCUMENTATION_MASTER_INDEX.md` for overview
   - Read `HANDOVER_TEST2.md` for implementation guide
   - Read `TEST_INVESTIGATION_LOG.md` - Test 1 entry

2. **Implement Code Changes** (15 minutes)
   - Modify `engine-sim-bridge/engine-sim/src/synthesizer.cpp` lines 221-266
   - Replace `m_cv0.wait()` with `m_cv0.wait_for()`
   - Implement fixed-interval rendering

3. **Build and Test** (5 minutes)
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/build
   cmake .. && make
   cd /Users/danielsinclair/vscode/engine-sim-cli
   make

   ./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee test2_engine.log
   ./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee test2_sine.log
   ```

4. **Analyze Results** (15 minutes)
   - Compare wakeup timing with Test 1
   - Compare write amounts with Test 1
   - Count discontinuities
   - Listen to audio output

5. **Update Documentation** (10 minutes)
   - Update `TEST_INVESTIGATION_LOG.md` with Test 2 entry
   - Update `AUDIO_THEORIES_TRACKING.md` with status
   - Create `TEST2_RESULTS.md` with analysis

### For Understanding the Investigation

**Time Estimate:** ~30 minutes

1. **Start** with `DOCUMENTATION_MASTER_INDEX.md` (5 minutes)
2. **Read** `TEST_INVESTIGATION_LOG.md` - Test 1 entry (10 minutes)
3. **Review** `TEST1_AUDIO_THREAD_WAKEUP_ANALYSIS.md` (10 minutes)
4. **Check** `AUDIO_THEORIES_TRACKING.md` for all theories (5 minutes)

---

## Key Findings

### What We Know

1. **Root Cause Identified**
   - Audio thread timing is unpredictable (6ms to 1.2 seconds)
   - This causes burst writes (up to 3x normal size)
   - Burst writes create audio discontinuities (crackles)

2. **Why Sine Mode Works**
   - Simpler audio generation (no convolution, no filters)
   - Less CPU time per sample
   - More predictable execution time
   - Zero discontinuities detected

3. **Evidence is Overwhelming**
   - 565 wakeups analyzed
   - Timing distribution measured
   - Discontinuity correlation confirmed
   - Sine mode comparison validates theory

### What We Don't Know

1. **Why GUI Doesn't Have This Problem**
   - Does GUI use different audio thread scheduling?
   - Does GUI use different synchronization primitives?
   - Is there a platform difference (Windows vs macOS)?

2. **Will Test 2 Fix Work?**
   - Will timed wait provide predictable timing?
   - Will fixed-interval rendering eliminate discontinuities?
   - Are there other factors we haven't considered?

---

## Success Criteria

### Test 2 Success Criteria

**Metrics:**
- ✅ Wakeup timing: Consistent ~10ms (no more 1.2 second outliers)
- ✅ Write amounts: Consistent 441 samples (no more 1411 sample bursts)
- ✅ Discontinuities: Zero or minimal (vs 18 in Test 1)
- ✅ Audio quality: Smooth, no audible crackles

**Documentation:**
- ✅ Update `TEST_INVESTIGATION_LOG.md` with Test 2 results
- ✅ Update `AUDIO_THEORIES_TRACKING.md` with status
- ✅ Create `TEST2_RESULTS.md` with analysis
- ✅ Save test output logs

### Overall Project Success Criteria

- ✅ Complete chronological record from Test 1 to solution
- ✅ Every attempt documented with evidence
- ✅ Final solution clearly identified
- ✅ Anyone can read history and understand what worked
- ⏳ Prevention of repeating same mistakes (pending Test 2)

---

## Code Locations

### Files Modified for Test 1

**Synthesizer Audio Thread:**
- **File:** `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
- **Lines:** 221-266 (renderAudio function)
- **Changes:** Added wakeup timing diagnostics

### Files to Modify for Test 2

**Synthesizer Audio Thread:**
- **File:** `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
- **Lines:** 221-266 (renderAudio function)
- **Changes:** Replace wait() with wait_for(), implement fixed-interval rendering

---

## Build and Test Commands

### Build Commands

```bash
cd /Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/build
cmake .. && make
cd /Users/danielsinclair/vscode/engine-sim-cli
make
```

### Test Commands

**Test 1 (Completed):**
```bash
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee engine_test.log
./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee sine_test.log
```

**Test 2 (Pending):**
```bash
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee test2_engine.log
./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee test2_sine.log
```

---

## Documentation Principles

### What We Do

✅ **Update after EVERY attempt** - no batching
✅ **Include FAILURES** - failures are progress, not setbacks
✅ **SHOW EVIDENCE** - code diffs, test outputs, measurements
✅ **NO SPECULATION** - only document what actually happened
✅ **MARK RESOLUTION** - when finally solved, document what worked

### What We Don't Do

❌ **Don't change diagnostic format** - Need to compare tests
❌ **Don't remove timing diagnostics** - Critical for verification
❌ **Don't skip testing** - Must test both engine and sine modes
❌ **Don't speculate without evidence** - Measure everything

---

## Contact and Context

### Project Context

This is a command-line interface for engine-sim audio generation. The Windows GUI version works perfectly with smooth audio. The macOS CLI version exhibits periodic crackling that have been investigated extensively.

The root cause has been identified as audio thread timing unpredictability in the synthesizer's condition variable wait. Test 2 will implement a fix using timed wait and fixed-interval rendering.

### Investigation Philosophy

**NO SPECULATION - ONLY EVIDENCE**

Every theory must be tested with diagnostics. Every result must be documented. Failed theories are valuable - they rule out possibilities and narrow the search.

---

## Status Summary

**Phase 1:** Initial Investigation ✅ COMPLETE
**Phase 2:** Root Cause Identification ✅ COMPLETE
**Phase 3:** Fix Implementation ⏳ PENDING

**Current Status:** Root cause identified, Test 2 ready to implement
**Next Action:** Implement Test 2 - Fixed-Interval Rendering
**Final Goal:** Eliminate audio crackles

---

**Remember:** Keep a historical record of everything you try. Don't come back until resolved.

**Good luck with Test 2!**
