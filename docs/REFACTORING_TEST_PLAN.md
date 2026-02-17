# Refactoring Test Plan: Verify DRY Implementation

## Objective

Verify that the unified audio loop implementation produces IDENTICAL behavior to the original duplicated code for both sine and engine modes.

## Test Environment

- **Platform**: macOS M4 Pro
- **Sample Rate**: 44100 Hz
- **Update Rate**: 60 Hz
- **Buffer Size**: 96000 samples (~2.18s)

## Compilation

```bash
# Build with unified implementation
cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j8

# Verify both libraries built
ls -lh libenginesim-mock.dylib libenginesim.dylib
```

## Test Suite

### Test 1: Sine Mode Functional Equivalence

**Purpose**: Verify sine mode behavior unchanged

```bash
# Test basic sine mode
./engine_sim_cli --sine --play --duration 3

# Expected output:
# - No errors
# - Smooth 60Hz updates
# - RPM ramp from ~800 to 6000
# - Frequency ramp from ~133Hz to 1000Hz
# - No crackles/pops in audio
```

**Verification**:
- [ ] Starts without errors
- [ ] Audio plays smoothly
- [ ] RPM increases smoothly
- [ ] No buffer underruns reported
- [ ] Completes after 3 seconds

### Test 2: Engine Mode Functional Equivalence

**Purpose**: Verify engine mode behavior unchanged

```bash
# Test basic engine mode
./engine_sim_cli --script engine-sim-bridge/engine-sim/assets/main.mr --play --duration 3

# Expected output:
# - No errors
# - Smooth 60Hz updates
# - RPM stabilizes around 800-1000
# - Realistic engine sound
# - No crackles/pops
```

**Verification**:
- [ ] Starts without errors
- [ ] Audio plays smoothly
- [ ] Engine sound is realistic
- [ ] No buffer underruns reported
- [ ] Completes after 3 seconds

### Test 3: Latency Measurement (Sine Mode)

**Purpose**: Verify latency remains ~0.67s

```bash
# Run sine mode interactive
./engine_sim_cli --sine --play --interactive

# Manual test procedure:
# 1. Let engine stabilize at idle
# 2. Press 'W' to increase throttle
# 3. Measure time from key press to pitch change
# 4. Expected: ~0.67s delay
```

**Measurement**:
- Current latency: _______ ms
- Expected: ~670ms
- **Pass if**: Within 50ms of expected

### Test 4: Latency Measurement (Engine Mode)

**Purpose**: Verify engine mode has SAME latency as sine mode

```bash
# Run engine mode interactive
./engine_sim_cli --script engine-sim-bridge/engine-sim/assets/main.mr --play --interactive

# Manual test procedure:
# 1. Let engine stabilize
# 2. Press 'W' to increase throttle
# 3. Measure time from key press to sound change
# 4. Expected: ~0.67s delay (SAME as sine mode)
```

**Measurement**:
- Current latency: _______ ms
- Expected: ~670ms (SAME as sine)
- **Pass if**: Within 50ms of sine mode latency

### Test 5: Buffer Management Consistency

**Purpose**: Verify buffer operations identical for both modes

**Test procedure**:
1. Add debug prints to BufferOps functions
2. Run both modes
3. Compare buffer operation sequence

```cpp
// Add to BufferOps::preFillCircularBuffer()
std::cout << "[DEBUG] Pre-fill: " << (PRE_FILL_ITERATIONS * FRAMES_PER_UPDATE) << " frames\n";

// Add to BufferOps::resetAndRePrefillBuffer()
std::cout << "[DEBUG] Re-pre-fill: " << (RE_PRE_FILL_ITERATIONS * FRAMES_PER_UPDATE) << " frames\n";
```

**Expected output (BOTH modes)**:
```
Pre-fill: 4410 frames
Warmup: 3 iterations
Re-pre-fill: 2205 frames
```

**Verification**:
- [ ] Sine mode matches expected output
- [ ] Engine mode matches expected output
- [ ] Both modes have IDENTICAL output

### Test 6: Timing Control Consistency

**Purpose**: Verify 60Hz loop rate for both modes

**Test procedure**:
1. Add timing instrumentation to main loop
2. Run both modes for 60 seconds
3. Count actual iterations

```cpp
// Add to runUnifiedLoop()
static int iterCount = 0;
static auto startTime = std::chrono::steady_clock::now();

iterCount++;
if (iterCount % 600 == 0) {  // Every 10 seconds
    auto elapsed = std::chrono::duration<double>(
        std::chrono::steady_clock::now() - startTime).count();
    double actualHz = iterCount / elapsed;
    std::cout << "[Timing] " << actualHz << " Hz (target: 60 Hz)\n";
}
```

**Expected output**:
```
[Timing] 60.01 Hz (target: 60 Hz)
[Timing] 60.00 Hz (target: 60 Hz)
[Timing] 59.99 Hz (target: 60 Hz)
```

**Verification**:
- [ ] Sine mode: 60Hz ± 0.5Hz
- [ ] Engine mode: 60Hz ± 0.5Hz
- [ ] Both modes have IDENTICAL timing

### Test 7: Interactive Controls Equivalence

**Purpose**: Verify keyboard controls work identically

**Test sequence** (run for BOTH modes):
1. Start interactive mode
2. Press 'W' 10 times → throttle should reach 100%
3. Press Space → throttle should drop to 0%
4. Press 'R' → throttle should reset to 20%
5. Press 'K' 5 times → throttle to 45%
6. Press 'J' 5 times → throttle back to 20%
7. Press 'Q' → should exit

**Verification**:
- [ ] Sine mode: All controls work
- [ ] Engine mode: All controls work
- [ ] Both modes respond identically

### Test 8: Memory Usage Comparison

**Purpose**: Verify memory reduction from code elimination

```bash
# Measure before refactoring
ps aux | grep engine_sim_cli  # Note RSS value

# Measure after refactoring
ps aux | grep engine_sim_cli  # Note RSS value
```

**Expected**:
- Memory usage: Same or slightly lower (less code loaded)

### Test 9: Warmup Sequence Validation

**Purpose**: Verify warmup produces clean audio in both modes

```bash
# Sine mode - capture warmup audio
./engine_sim_cli --sine --play --duration 5 --output sine_warmup.wav

# Engine mode - capture warmup audio
./engine_sim_cli --script engine-sim-bridge/engine-sim/assets/main.mr --play --duration 5 --output engine_warmup.wav

# Analyze with tools/analyze_crackles.py
python3 tools/analyze_crackles.py sine_warmup.wav
python3 tools/analyze_crackles.py engine_warmup.wav
```

**Expected**:
- No crackles in first 0.5s (warmup period)
- Smooth transition from warmup to main loop
- Both modes have < 5 crackle events total

### Test 10: Code Coverage

**Purpose**: Verify unified code paths are exercised

```bash
# Build with coverage
cmake .. -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage"
make -j8

# Run both modes
./engine_sim_cli --sine --play --duration 1
./engine_sim_cli --script engine-sim-bridge/engine-sim/assets/main.mr --play --duration 1

# Generate coverage report
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```

**Expected**:
- BufferOps functions: 100% coverage
- WarmupOps functions: 100% coverage
- LoopTimer: 100% coverage
- runUnifiedLoop: 100% coverage
- Both audio sources: 100% coverage

## Success Criteria

### Must Pass (Blocking)
- ✅ All functional tests pass
- ✅ Latency measurements within 50ms of target
- ✅ No new buffer underruns
- ✅ No audio quality degradation
- ✅ Both modes have identical buffer/timing behavior

### Should Pass (Non-blocking)
- ✅ Memory usage same or lower
- ✅ Coverage > 95% for shared code
- ✅ No compiler warnings

## Regression Detection

If ANY of these occur, refactoring has introduced bugs:

🚨 **FAIL**: Latency increases by >50ms
🚨 **FAIL**: Buffer underruns increase
🚨 **FAIL**: Audio quality degrades (crackles/pops)
🚨 **FAIL**: Sine and engine modes have different latency
🚨 **FAIL**: Interactive controls behave differently between modes
🚨 **FAIL**: Warmup produces artifacts

## Sign-off

- [ ] All functional tests passed
- [ ] Latency verified identical
- [ ] Buffer operations verified identical
- [ ] Timing control verified identical
- [ ] Interactive controls verified
- [ ] No regressions detected

**Tested by**: _________________
**Date**: _________________
**Result**: PASS / FAIL
**Notes**: _________________
