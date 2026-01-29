# Testing Guide - engine-sim-cli

## Overview

This guide provides comprehensive testing procedures for the engine-sim-cli project, including basic functionality tests, diagnostic tests, and troubleshooting scenarios.

## Prerequisites

Before running tests, ensure you have:

1. **Built the project**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli
   mkdir build && cd build
   cmake ..
   make
   ```

2. **Built the diagnostic tool**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli
   g++ -std=c++17 -O2 -Wall -Wextra \
       -I./engine-sim-bridge/include \
       diagnostics.cpp \
       -o diagnostics \
       build/engine-sim-bridge/src/libengine-sim-bridge.a \
       -pthread
   ```

3. **Available engine configurations**:
   - `engine-sim/assets/main.mr` - Default engine (known-good)
   - `es/v8_engine.mr` - V8 engine configuration
   - Other `.mr` files in the assets directory

## Quick Smoke Test

The quickest way to verify the system is working:

```bash
# Test 1: Run diagnostics for 2 seconds
./diagnostics engine-sim/assets/main.mr 2.0

# Expected output:
# - Stage 1: PASS (Engine running)
# - Stage 3: PASS (Exhaust flow)
# - Stage 5: PASS (Audio output)
# - diagnostic_output.wav created
```

If the smoke test passes, the system is working correctly.

## Test Scenarios

### Scenario 1: Basic Functionality Test

**Purpose**: Verify core engine simulation and audio output

**Steps**:
```bash
# 1. Run diagnostics on default engine
./diagnostics engine-sim/assets/main.mr 5.0

# 2. Check the report
# All stages should PASS

# 3. Listen to the output
# On macOS: open diagnostic_output.wav
# On Linux: xdg-open diagnostic_output.wav
```

**Expected Results**:
- Stage 1: Engine RPM > 0 (PASS)
- Stage 3: Exhaust flow > 1e-9 (PASS)
- Stage 5: Audio level > 1e-6 (PASS)
- WAV file contains engine audio

**Common Issues**:
- If Stage 1 fails: Engine configuration problem
- If Stage 3 fails: Fuel system or combustion issue
- If Stage 5 fails: Synthesizer or audio output issue

### Scenario 2: Multiple Engine Test

**Purpose**: Verify different engine configurations work

**Steps**:
```bash
# Test with default engine
./diagnostics engine-sim/assets/main.mr 3.0 --output test_default.wav

# Test with V8 engine
./diagnostics es/v8_engine.mr 3.0 --output test_v8.wav

# Test with other engines if available
./diagnostics path/to/other.mr 3.0 --output test_other.wav
```

**Expected Results**:
- All engines should start and run
- Each engine should have distinct audio characteristics
- No errors during engine loading

### Scenario 3: Duration Test

**Purpose**: Verify system stability over longer runs

**Steps**:
```bash
# Short run (2 seconds)
./diagnostics engine-sim/assets/main.mr 2.0 --output test_2s.wav

# Medium run (10 seconds)
./diagnostics engine-sim/assets/main.mr 10.0 --output test_10s.wav

# Long run (30 seconds)
./diagnostics engine-sim/assets/main.mr 30.0 --output test_30s.wav
```

**Expected Results**:
- All durations complete successfully
- Audio remains consistent throughout
- No memory leaks or crashes
- Buffer underruns should be minimal ( < 10)

### Scenario 4: Custom Output Path Test

**Purpose**: Verify configurable output feature

**Steps**:
```bash
# Test custom output paths
./diagnostics engine-sim/assets/main.mr 2.0 --output /tmp/test1.wav
./diagnostics engine-sim/assets/main.mr 2.0 --output /tmp/test2.wav
./diagnostics engine-sim/assets/main.mr 2.0 --output ~/Desktop/test3.wav
```

**Expected Results**:
- All output files created at specified paths
- All files contain valid audio
- No permission errors (for writable paths)

### Scenario 5: Corruption Detection Test

**Purpose**: Verify the diagnostic tool detects corrupted audio

**Note**: This test requires manual code modification to inject corruption. Skip unless debugging corruption issues.

**Expected Behavior**:
- Tool should report "CORRUPTED" status
- NaN/Inf should be detected and reported
- Issue should be listed in "ISSUES DETECTED"

### Scenario 6: CLI Integration Test

**Purpose**: Test the main CLI application

**Steps**:
```bash
# Build the CLI first (if not already built)
cd build && make

# Run CLI with default engine
./engine-sim-cli --default-engine --rpm 1000 --duration 5 --play
```

**Expected Results**:
- CLI starts without errors
- Engine runs at approximately 1000 RPM
- Audio plays cleanly for 5 seconds
- Clean exit after duration

## Regression Tests

These tests verify that previously fixed bugs remain fixed:

### Regression Test 1: Starter Motor Fix

**Purpose**: Verify starter motor is enabled (Issue #1 fix)

**Test**:
```bash
./diagnostics engine-sim/assets/main.mr 2.0
```

**Verify**: Stage 1 shows RPM > 0 within first second

**Original Bug**: RPM stayed at 0 because starter motor was never enabled

### Regression Test 2: Mono-to-Stereo Fix

**Purpose**: Verify mono-to-stereo conversion works (Piranha fix)

**Test**:
```bash
./diagnostics engine-sim/assets/main.mr 5.0
# Check that no "CORRUPTED" status is reported
# Listen to output - should sound clean, not like "upside down saw tooth"
```

**Original Bug**: Audio sounded like "upside down saw tooth" due to garbage memory reads

### Regression Test 3: Buffer Management Fix

**Purpose**: Verify proper buffer management

**Test**:
```bash
./diagnostics engine-sim/assets/main.mr 10.0
# Check BUFFER STATUS section
# Buffer underruns should be minimal (< 10)
# No buffer overruns
```

**Original Bug**: Simple modulo wrapping caused buffer overwrites and audio artifacts

## Performance Tests

### Performance Test 1: Rendering Speed

**Purpose**: Verify audio rendering keeps up with real-time

**Test**:
```bash
time ./diagnostics engine-sim/assets/main.mr 5.0
```

**Expected Results**:
- Wall clock time should be slightly less than real-time (due to no actual playback)
- For 5 seconds of audio, should complete in < 5 seconds
- Excessive time indicates performance issues

### Performance Test 2: Memory Usage

**Purpose**: Check for memory leaks

**Test**:
```bash
# Run multiple sequential tests
for i in {1..5}; do
    ./diagnostics engine-sim/assets/main.mr 2.0 --output test_$i.wav
done

# Check memory growth (macOS)
# Use Activity Monitor or: sudo leaks -atExit -- diagnostics
# (requires rebuilding with debug symbols)
```

**Expected Results**:
- Constant memory usage across runs
- No significant memory growth
- Clean exit without leaks

## Troubleshooting Tests

### Troubleshooting Test 1: Silent Audio Investigation

**When to use**: Audio output is silent or extremely quiet

**Steps**:
```bash
# 1. Run diagnostics
./diagnostics engine-sim/assets/main.mr 5.0

# 2. Check Stage 5 status
# If "FAIL" - no audio at all
# If "PASS" but silent - check WAV file, may be very quiet

# 3. Listen to WAV file directly
# Use audio editor to check actual levels

# 4. Check asset path
ls -la es/sound-library/
# Should contain impulse response files

# 5. Try known-good engine
./diagnostics engine-sim/assets/main.mr 5.0
```

### Troubleshooting Test 2: Engine Won't Start

**When to use**: RPM stays at 0

**Steps**:
```bash
# 1. Verify engine file exists
ls -la engine-sim/assets/main.mr

# 2. Check diagnostic output for errors
./diagnostics engine-sim/assets/main.mr 2.0
# Look for "ERROR:" messages

# 3. Try absolute path
./diagnostics /absolute/path/to/engine-sim/assets/main.mr 2.0

# 4. Check Stage 1 in diagnostic report
# If FAIL - engine configuration issue
```

### Troubleshooting Test 3: Audio Distortion

**When to use**: Audio plays but sounds distorted

**Steps**:
```bash
# 1. Check for corruption
./diagnostics engine-sim/assets/main.mr 5.0
# Look for "CORRUPTED" or "WARNING" status

# 2. Check for clipped samples
# In Stage 5, "Clipped Samples" should be 0

# 3. Check buffer status
# "Buffer Underruns" should be minimal
# "Buffer Overruns" should be 0

# 4. Test with shorter duration
./diagnostics engine-sim/assets/main.mr 2.0
# If distortion disappears, may be heat/buffer issue
```

## Test Checklist

Use this checklist before considering the system "tested":

- [ ] Smoke test passes (2-second diagnostic)
- [ ] All diagnostic stages PASS
- [ ] WAV file contains valid audio
- [ ] Multiple engines work (at least 2 different configs)
- [ ] Multiple durations work (2s, 5s, 10s)
- [ ] Custom output paths work
- [ ] No NaN/Inf corruption detected
- [ ] Buffer underruns minimal (< 10)
- [ ] No buffer overruns
- [ ] No clipped samples
- [ ] CLI integration test passes
- [ ] Performance acceptable (real-time or better)
- [ ] No obvious memory leaks

## Automated Testing Script

Save this as `run_tests.sh`:

```bash
#!/bin/bash

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

PASSED=0
FAILED=0

run_test() {
    local test_name="$1"
    local command="$2"
    local expected="$3"

    echo -n "Running: $test_name... "

    if eval "$command" > /dev/null 2>&1; then
        if [ "$expected" = "pass" ]; then
            echo -e "${GREEN}PASS${NC}"
            ((PASSED++))
        else
            echo -e "${RED}FAIL (should have failed)${NC}"
            ((FAILED++))
        fi
    else
        if [ "$expected" = "fail" ]; then
            echo -e "${GREEN}PASS (expected failure)${NC}"
            ((PASSED++))
        else
            echo -e "${RED}FAIL${NC}"
            ((FAILED++))
        fi
    fi
}

# Main tests
echo "=========================================="
echo "  engine-sim-cli Test Suite"
echo "=========================================="
echo ""

# Smoke test
run_test "Smoke test (2s)" \
    "./diagnostics engine-sim/assets/main.mr 2.0" \
    "pass"

# Duration tests
run_test "Short duration (2s)" \
    "./diagnostics engine-sim/assets/main.mr 2.0" \
    "pass"

run_test "Medium duration (5s)" \
    "./diagnostics engine-sim/assets/main.mr 5.0" \
    "pass"

run_test "Long duration (10s)" \
    "./diagnostics engine-sim/assets/main.mr 10.0" \
    "pass"

# Custom output path
run_test "Custom output path" \
    "./diagnostics engine-sim/assets/main.mr 2.0 --output /tmp/test_output.wav" \
    "pass"

# Multiple engines (if available)
if [ -f "es/v8_engine.mr" ]; then
    run_test "V8 engine test" \
        "./diagnostics es/v8_engine.mr 2.0" \
        "pass"
fi

# Summary
echo ""
echo "=========================================="
echo "  Test Summary"
echo "=========================================="
echo -e "Passed: ${GREEN}$PASSED${NC}"
echo -e "Failed: ${RED}$FAILED${NC}"
echo ""

if [ $FAILED -eq 0 ]; then
    echo -e "${GREEN}All tests passed!${NC}"
    exit 0
else
    echo -e "${RED}Some tests failed!${NC}"
    exit 1
fi
```

Make it executable:
```bash
chmod +x run_tests.sh
./run_tests.sh
```

## Continuous Integration

For CI/CD pipelines, use this minimal test:

```bash
#!/bin/bash
set -e  # Exit on first error

# Build
mkdir -p build && cd build
cmake ..
make

# Build diagnostics
cd ..
g++ -std=c++17 -O2 -Wall -Wextra \
    -I./engine-sim-bridge/include \
    diagnostics.cpp \
    -o diagnostics \
    build/engine-sim-bridge/src/libengine-sim-bridge.a \
    -pthread

# Quick smoke test
./diagnostics engine-sim/assets/main.mr 2.0

# Verify output exists
[ -f diagnostic_output.wav ] || { echo "WAV file not created"; exit 1; }

# Clean up
rm diagnostic_output.wav
```

## Test Result Interpretation

### Normal Operating Values

**RPM**:
- Idle: 500-1000 RPM
- Normal operation: 1000-3000 RPM
- High RPM: 3000-7000 RPM

**Exhaust Flow**:
- Idle: 1e-7 to 1e-5 m^3/s
- Under load: 1e-5 to 1e-4 m^3/s
- Can be negative transiently (normal)

**Audio Levels**:
- Quiet: 1e-4 to 1e-3
- Normal: 1e-3 to 1e-2
- Loud: 1e-2 to 1e-1
- Clipping: > 1.0 (bad)

**Buffer Statistics**:
- Successful reads: Should equal number of updates
- Failed reads: Should be 0
- Buffer underruns: < 10 (few is acceptable)
- Buffer overruns: Should be 0

**Sample Quality**:
- Silent samples: < 5% is acceptable
- Clipped samples: Should be 0
- NaN/Inf: Should be false

## Reporting Issues

When reporting test failures, include:

1. **Test output**: Complete diagnostic report
2. **Command used**: Exact command line
3. **Expected vs Actual**: What you expected vs what happened
4. **System info**: OS, compiler version
5. **Files**: Attach diagnostic_output.wav if relevant

## Test Maintenance

Keep tests up to date:

1. **Add new tests** when adding features
2. **Update expected values** when changing behavior
3. **Remove obsolete tests** when features are removed
4. **Document test changes** in this guide

## Related Documentation

- `DEBUGGING_HISTORY.md` - Technical history and bug fixes
- `DIAGNOSTICS_GUIDE.md` - How to use diagnostic tools

---

**Document Version**: 1.0
**Last Updated**: January 28, 2026
**Status**: Complete - All test scenarios documented
