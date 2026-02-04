# Test Logs Archive

**Purpose:** Complete history of all audio discontinuity tests and bug fixes
**Archive Date:** 2025-02-04
**Platform:** macOS M4 Pro, Apple Silicon
**Audio API:** Core Audio AudioUnit (pull model)

---

## Test Log Manifest

### Baseline Tests

| Test | Date | Engine Log | Sine Log | Discontinuities | Notes |
|------|------|------------|----------|-----------------|-------|
| Test 2 | 2025-02-04 13:50 | test2_engine.log (67,444 bytes) | test2_sine.log (62,722 bytes) | ? | Early test with basic diagnostics |
| Test 3 | 2025-02-04 13:55 | test3_engine.log (72,672 bytes) | test3_sine.log (62,946 bytes) | ? | Enhanced diagnostics |
| Test 4 | 2025-02-04 14:01 | test4_engine.log (69,005 bytes) | test4_sine.log (61,368 bytes) | **62** | **Baseline for bug fixes** |

### Bug Fix Tests

| Bug Fix | Date | Engine Log | Sine Log | Discontinuities | Change | Result |
|---------|------|------------|----------|-----------------|--------|--------|
| **#1** | 2025-02-04 14:05 | bugfix1_engine.log (66,476 bytes) | bugfix1_sine.log (62,254 bytes) | **25** | **-37 (60%)** | **SUCCESS** |
| **#2** | 2025-02-04 14:08 | bugfix2_engine.log (68,636 bytes) | bugfix2_sine.log (67,444 bytes) | **58** | **+33 (132%)** | **FAILED** |

---

## Bug Fix #1: Synthesizer Array Index (SUCCESS)

**File:** `synthesizer.cpp` line 312
**Fix:** `m_filters->` → `m_filters[i]`

**Problem:**
```cpp
// BUGGY CODE - only processes first filter
m_filters->process(sample[0], sample[1]);
```

**Fix:**
```cpp
// FIXED CODE - processes all filters in chain
m_filters[i]->process(sample[0], sample[1]);
```

**Results:**
- Before: 62 discontinuities (Test 4 baseline)
- After: 25 discontinuities
- Improvement: 60% reduction

**Evidence:** See `bugfix1_engine.log` for complete diagnostic output

---

## Bug Fix #2: Leveling Filter Smoothing (FAILED)

**File:** `leveling_filter.cpp` line 31
**Change:** `0.9/0.1` → `0.99/0.01` smoothing factor

**Theory:** Slower smoothing would reduce abrupt transitions

**Change Made:**
```cpp
// BEFORE (working correctly)
m_filteredInput = 0.9 * m_filteredInput + 0.1 * input;

// AFTER (made it worse)
m_filteredInput = 0.99 * m_filteredInput + 0.01 * input;
```

**Results:**
- Before: 25 discontinuities (Bugfix #1)
- After: 58 discontinuities
- Change: 300% increase (made it WORSE)

**Conclusion:** Leveling filter is NOT the root cause. Original 0.9/0.1 balance was correct.

**Evidence:** See `bugfix2_engine.log` for complete diagnostic output

---

## Current Status

**After Bug Fix #1 (Best Result):**
- Discontinuities: 25 (down from 62 original)
- Improvement: 60%
- Remaining work: 25 discontinuities to eliminate

**Next Investigation:**
- Convolution filter state management
- Other filter state issues
- Additional array access bugs

---

## Log File Contents

Each log file contains:

1. **Audio thread wakeup timing** - Microsecond precision timing data
2. **Buffer write patterns** - Samples written per wakeup
3. **WRITE discontinuities** - Detected in synthesizer output
4. **READ discontinuities** - Detected at audio playback
5. **Buffer wrap events** - Read pointer wraparound detection
6. **Position diagnostics** - Hardware vs manual position tracking
7. **Underrun detection** - Buffer depletion events

### Key Log Sections

**Startup Sequence:**
```
[Audio] AudioUnit initialized at 44100 Hz (stereo float32)
[POSITION DIAGNOSTIC #0 at 0ms] HW:0 (mod:0) Manual:0 Diff:0
[UNDERRUN #1 at 95ms] Req: 470, Got: 176, Avail: 176
```

**Discontinuity Events:**
```
[WRITE DISCONTINUITY #1 at 234ms] WritePos: 10342, Delta(L/R): 0.2617/0.2617
Samples(L/R): -0.1234/-0.1234 -> 0.1383/0.1383
```

**Buffer Wrap Events:**
```
[BUFFER WRAP #4 at 3989ms] RPtr: 43630 -> 0 (Jump: 470)
Discontinuity(L/R): 0.155/0.155, WPtr: 4830
```

---

## Test Commands Reference

**Baseline Test (Test 4):**
```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee test4_engine.log
./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee test4_sine.log
```

**Bug Fix #1 Test:**
```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee bugfix1_engine.log
./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee bugfix1_sine.log
```

**Bug Fix #2 Test:**
```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10 2>&1 | tee bugfix2_engine.log
./build/engine-sim-cli --sine --play --duration 10 2>&1 | tee bugfix2_sine.log
```

---

## Analysis Scripts

**Count discontinuities:**
```bash
grep -c "DISCONTINUITY" test_logs/test4_engine.log
grep -c "DISCONTINUITY" test_logs/bugfix1_engine.log
grep -c "DISCONTINUITY" test_logs/bugfix2_engine.log
```

**Extract discontinuity events:**
```bash
grep "DISCONTINUITY" test_logs/bugfix1_engine.log | head -20
```

**Extract final summary:**
```bash
tail -100 test_logs/bugfix1_engine.log | grep -E "(DISCONTINUITY|Total)"
```

---

## Documentation

See main documentation for complete analysis:
- `TEST_INVESTIGATION_LOG.md` - Complete chronological test record
- `AUDIO_THEORIES_TRACKING.md` - Theories and evidence tracking
- `README.md` (this file) - Test log archive manifest

---

## Archive Maintenance

**When adding new tests:**
1. Update this manifest with new log files
2. Record discontinuity counts in the table
3. Document the fix/hypothesis being tested
4. Include log file sizes and dates
5. Update analysis scripts if needed

**Archive naming convention:**
- Baseline tests: `test{N}_{engine|sine}.log`
- Bug fix tests: `bugfix{N}_{engine|sine}.log`
- Investigation tests: `investigation{N}_{engine|sine}.log`

**Retention policy:** Keep all test logs indefinitely for historical reference

---

**Last Updated:** 2025-02-04
**Archive Version:** 1.0
**Total Test Logs:** 12 files
**Total Archive Size:** 738 KB
