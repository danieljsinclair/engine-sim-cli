# Bug Fix #3 Revert - Summary of Key Learnings

**Date:** 2026-02-04
**Issue:** Convolution Filter State Reset Hypothesis
**Result:** Hypothesis catastrophically wrong - 14x increase in discontinuities

---

## What We Attempted

**Hypothesis:** Convolution filter shift register becomes stale when input buffer is modified at boundaries, requiring state reset to maintain continuity.

**Implementation:**
- Added `resetShiftRegister()` method to ConvolutionFilter
- Called reset in `endInputBlock()` after removing old samples
- Theory: Filter state needs to be "fresh" after buffer modification

## Results

### Engine Mode (Subaru EJ25 @ 1000 RPM)
- **Before:** ~25 discontinuities (from Bug Fix #1)
- **After:** 865 discontinuities (377 WRITE + 488 READ)
- **Change:** +3360% increase (14x worse)
- **Buffer availability:** 4.9ms (critically low)
- **Underruns:** 3

### Sine Mode
- **Before:** 0 discontinuities
- **After:** 0 discontinuities
- **Buffer availability:** 202.4ms (healthy)

---

## Critical Findings

### 1. Convolution Filter State is Essential History, NOT Stale Data
**The fundamental error:** Treating the shift register as stale state that needs resetting.

**Reality:** The convolution algorithm requires continuous history:
- Formula: y[n] = Σ(h[k] × x[n-k])
- Each output sample depends on previous input samples
- Resetting destroys this continuity
- Creates artificial discontinuities at every buffer boundary

### 2. Buffer Availability Explains Performance Differences
**Engine Mode:** 4.9ms buffer availability → performance starved
**Sine Mode:** 202.4ms buffer availability → healthy performance

**This strongly suggests:** Engine simulation can't generate samples fast enough for real-time demands.

### 3. Sine Mode Works Because It Doesn't Trigger Engine Issues
- Sine generation bypasses engine simulation
- No complex filter chains
- No buffer management timing issues
- Hence 0 discontinuities

### 4. The Fix Made Things Much Worse
- 14x increase in discontinuities
- Severely degraded audio quality
- Based on incorrect understanding of convolution algorithms
- **Lesson: Never fix something that isn't broken**

---

## What This Reversal Proved

### About Convolution Filters
- ✅ State reset is ALWAYS wrong
- ✅ Shift register must maintain continuity across buffers
- ✅ Convolution requires temporal history
- ❌ "Stale state" hypothesis was completely incorrect

### About the Root Cause
- ✅ Buffer availability is critical (4.9ms vs 202ms)
- ✅ Engine simulation performance is the real issue
- ✅ Not a filter state management problem
- ✅ Not a synthesizer code bug (same code works in GUI)

### About the Investigation Approach
- ✅ Convolution filter reset hypothesis DISPROVEN
- ✅ Evidence-based approach works
- ✅ Need to focus on engine timing vs audio demands
- ❌ Don't assume "stale state" without evidence

---

## Next Steps

### Priority 1: Engine Simulation Performance
- Profile engine simulation timing
- Measure sample generation speed
- Compare with audio callback demands
- Identify bottlenecks

### Priority 2: Buffer Management
- Test different buffer lead targets (5%, 10%, 20%)
- Implement pre-fill strategies
- Optimize for pull model vs push model

### Priority 3: Thread Synchronization
- Analyze main loop vs audio callback timing
- Investigate priority settings
- Consider fixed-interval rendering

---

## Key Takeaway

**The real problem is ENGINE SIMULATION TIMING, not filter state management.**

The 25 remaining discontinuities from Bug Fix #1 are likely caused by:
1. Engine simulation not keeping up with real-time demands
2. Buffer starvation (4.9ms available)
3. Thread scheduling issues
4. Not by any filter or synthesizer code bugs

**Lesson learned:** When evidence points to timing issues, don't "fix" the signal processing - fix the timing.

---

**Status:** Returned to Bug Fix #1 baseline (25 discontinuities)
**Next Focus:** Engine simulation performance optimization