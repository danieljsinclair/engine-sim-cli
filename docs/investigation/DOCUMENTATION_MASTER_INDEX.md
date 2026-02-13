# Audio Crackle Investigation - Master Documentation Index

**Project:** engine-sim-cli Audio System
**Platform:** macOS M4 Pro (Apple Silicon)
**Status:** ROOT CAUSE IDENTIFIED - Test 2 pending implementation
**Last Updated:** 2025-02-04

---

## Quick Start

### For the Next Developer

**You are here because:** Audio crackles have been investigated and the root cause has been identified. Test 2 implementation is ready to begin.

**Start here:**
1. Read this index to understand the structure
2. Read `HANDOVER_TEST2.md` for implementation instructions
3. Read `TEST_INVESTIGATION_LOG.md` for complete history
4. Implement Test 2 and document results

**Current Status:**
- ✅ Root cause identified (Hypothesis 4 confirmed)
- ⏳ Test 2 implementation pending
- ❓ Fix verification pending

---

## Documentation Structure

### Core Documentation (Read These First)

#### 1. MASTER INDEX (this file)
- **Purpose:** Navigation and overview
- **Status:** Current
- **When to read:** First

#### 2. HANDOVER_TEST2.md
- **Purpose:** Implementation guide for Test 2
- **Status:** Current
- **When to read:** Before implementing Test 2
- **Contents:**
  - Root cause summary
  - Implementation plan
  - Code changes required
  - Expected outcomes
  - Success criteria

#### 3. TEST_INVESTIGATION_LOG.md
- **Purpose:** Complete chronological test record
- **Status:** Current (will be updated after Test 2)
- **When to read:** For complete investigation history
- **Contents:**
  - Test 1: Audio Thread Wakeup Timing Analysis
  - Test 2: Fixed-Interval Rendering (pending)
  - Every test with evidence, analysis, and next steps

#### 4. AUDIO_THEORIES_TRACKING.md
- **Purpose:** Track all theories and evidence
- **Status:** Current (will be updated after Test 2)
- **When to read:** To understand what was tried
- **Contents:**
  - Phase 1: Pull Model Position Hypothesis (DISPROVEN)
  - Phase 2: Root Cause Identified (CONFIRMED)
  - Complete history of all theories tested

### Test 1 Documentation (Evidence)

#### 5. TEST1_AUDIO_THREAD_WAKEUP_ANALYSIS.md
- **Purpose:** Detailed analysis of Test 1 results
- **Status:** Complete
- **When to read:** To understand Test 1 evidence
- **Contents:**
  - Executive summary
  - Build results
  - Test results (engine and sine modes)
  - Critical evidence timeline
  - Root cause analysis
  - Recommendations

#### 6. TEST1_EVIDENCE_SUMMARY.md
- **Purpose:** Summary of Test 1 evidence
- **Status:** Complete
- **When to read:** For quick Test 1 overview
- **Contents:**
  - Test configuration
  - Critical evidence collected
  - Data files generated
  - Hypothesis verification

### Test Data Files

#### 7. engine_test.log
- **Purpose:** Full diagnostic output from Test 1 engine mode
- **Status:** Complete
- **When to read:** To analyze raw Test 1 data
- **Contents:** 565 audio thread wakeups with timing and buffer data

#### 8. sine_test.log
- **Purpose:** Full diagnostic output from Test 1 sine mode
- **Status:** Complete
- **When to read:** To compare with engine mode
- **Contents:** 4 audio thread wakeups with timing and buffer data

### Historical Documentation (Background)

#### 9. AUDIO_INVESTIGATION_HANDOVER.md
- **Purpose:** Pre-Test 1 investigation handover
- **Status:** Historical (superseded by Test 1 findings)
- **When to read:** For historical context
- **Contents:**
  - What was ruled out before Test 1
  - Investigation history
  - Architecture comparison

#### 10. DOCUMENTATION_INDEX.md
- **Purpose:** Old documentation index
- **Status:** Historical (superseded by this index)
- **When to read:** For historical context

#### 11. README_DOCUMENTATION.md
- **Purpose:** Documentation README
- **Status:** Historical
- **When to read:** For historical context

---

## Investigation Timeline

### Phase 1: Initial Investigation (2025-02-03)

**Theories Tested:**
- ❌ Position tracking errors (DISPROVEN - hardware position matches manual tracking)
- ❌ Update rate differences (DISPROVEN - CLI already at 60Hz matching GUI)
- ❌ Audio library choice (RESOLVED - AudioUnit is correct for macOS)
- ❌ Double buffer consumption (FIXED - no longer occurs)
- ❌ Underruns as primary cause (DISPROVEN - crackles occur without underruns)

**Result:** All common theories ruled out. Problem remains unidentified.

### Phase 2: Root Cause Identification (2025-02-04)

**Hypothesis 4:** Audio thread uses `wait()` with condition variable that has unpredictable timing

**Test 1: Audio Thread Wakeup Timing Analysis**

**Evidence:**
- Audio thread wakeups range from **0 to 1,210,442 microseconds** (0 to 1.2 seconds!)
- Normal: 6-12ms (90% of wakeups)
- Abnormal: 23-31ms (8% of wakeups)
- Extreme: 98ms-1.2s (2% of wakeups)
- Burst writes: 1411 samples (3x normal size)
- 18 discontinuities in 10 seconds
- All discontinuities correlate with abnormal wakeups

**Result:** ✅ HYPOTHESIS 4 CONFIRMED

**Root Cause Identified:**
The `m_cv0.wait()` in `synthesizer.cpp` line 231 has unpredictable wake-up timing due to:
- OS scheduler variability
- Condition variable notification latency
- Competing threads for CPU time

This causes burst writes that create audio discontinuities (crackles).

### Phase 3: Fix Implementation (Pending - 2025-02-04)

**Test 2: Fixed-Interval Rendering**

**Status:** ⏳ PENDING IMPLEMENTATION

**Hypothesis:** Predictable timing will eliminate discontinuities

**Implementation:**
- Replace `m_cv0.wait()` with `m_cv0.wait_for()` with 5ms timeout
- Implement fixed-interval rendering (10ms intervals)
- Write fixed amount (441 samples per wakeup)

**Expected Outcome:**
- Wakeup timing will be consistent (~10ms)
- Write amounts will be consistent (441 samples)
- Discontinuities will be eliminated or significantly reduced
- Audio output will be smooth without crackles

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
   - Does GUI use fixed-interval rendering already?

2. **Will Test 2 Fix Work?**
   - Will timed wait provide predictable timing?
   - Will fixed-interval rendering eliminate discontinuities?
   - Are there other factors we haven't considered?

---

## Code Locations

### Files Modified for Test 1

**Synthesizer Audio Thread:**
- File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
- Lines: 221-266 (renderAudio function)
- Changes: Added wakeup timing diagnostics

### Files to Modify for Test 2

**Synthesizer Audio Thread:**
- File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
- Lines: 221-266 (renderAudio function)
- Changes: Replace wait() with wait_for(), implement fixed-interval rendering

### Reference Files

**GUI Implementation (Working):**
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
- Uses DirectSound push model
- No crackles reported

**CLI Implementation (Broken):**
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
- Uses AudioUnit pull model
- Crackles confirmed

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

## Success Criteria

### Test 2 Success Criteria

**Metrics:**
- Wakeup timing: Consistent ~10ms (no more 1.2 second outliers)
- Write amounts: Consistent 441 samples (no more 1411 sample bursts)
- Discontinuities: Zero or minimal (vs 18 in Test 1)
- Audio quality: Smooth, no audible crackles

**Documentation:**
- Update TEST_INVESTIGATION_LOG.md with Test 2 results
- Update AUDIO_THEORIES_TRACKING.md with status
- Create TEST2_RESULTS.md with analysis
- Save test output logs

### Overall Project Success Criteria

- Complete chronological record from Test 1 to solution
- Every attempt documented with evidence
- Final solution clearly identified
- Anyone can read history and understand what worked
- Prevention of repeating same mistakes

---

## Critical Reminders

### What NOT to Do

❌ **Don't change diagnostic format** - Need to compare Test 1 and Test 2
❌ **Don't remove timing diagnostics** - Critical for verification
❌ **Don't skip testing** - Must test both engine and sine modes
❌ **Don't speculate without evidence** - Measure everything

### What to Do

✅ **Keep diagnostic format consistent** - Add Target field, keep rest
✅ **Measure everything** - Wakeup timing, write amounts, discontinuities
✅ **Compare with Test 1** - Use Test 1 as baseline
✅ **Document failures** - If it doesn't work, that's valuable data
✅ **Listen to the audio** - Your ears are the ultimate test

---

## Document Maintenance

### After Test 2 Implementation

**Update These Files:**
1. **TEST_INVESTIGATION_LOG.md** - Add Test 2 entry with results
2. **AUDIO_THEORIES_TRACKING.md** - Update status based on results
3. **MASTER_INDEX.md** - Update Test 2 status
4. **HANDOVER_TEST2.md** - Mark as complete if successful

**Create These Files:**
1. **test2_engine.log** - Save test output
2. **test2_sine.log** - Save test output
3. **TEST2_RESULTS.md** - Create analysis document
4. **HANDOVER_TEST3.md** - If Test 2 fails, prepare Test 3

### Documentation Principles

- **Update after EVERY attempt** - no batching
- **Include FAILURES** - failures are progress, not setbacks
- **SHOW EVIDENCE** - code diffs, test outputs, measurements
- **NO SPECULATION** - only document what actually happened
- **MARK RESOLUTION** - when finally solved, document what worked

---

## Contact and Context

### Project Context

This is a command-line interface for engine-sim audio generation. The Windows GUI version works perfectly with smooth audio. The macOS CLI version exhibits periodic crackling that have been investigated extensively.

The root cause has been identified as audio thread timing unpredictability in the synthesizer's condition variable wait. Test 2 will implement a fix using timed wait and fixed-interval rendering.

### Investigation Philosophy

**NO SPECULATION - ONLY EVIDENCE**

Every theory must be tested with diagnostics. Every result must be documented. Failed theories are valuable - they rule out possibilities and narrow the search.

The diagnostics infrastructure is comprehensive. Use it to test theories before implementing changes.

### For the Next Developer

You are picking up at a critical point:
- Root cause identified ✅
- Fix designed ✅
- Implementation pending ⏳
- Verification pending ❓

Read HANDOVER_TEST2.md for complete implementation instructions. Good luck!

---

**End of Master Documentation Index**

**Last Updated:** 2025-02-04
**Status:** Root cause identified, Test 2 pending
**Next Action:** Implement Test 2
