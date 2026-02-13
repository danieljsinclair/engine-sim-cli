# Engine-Sim-CLI Verification System

This document describes the comprehensive verification system for the engine-sim-cli project, designed to objectively confirm that the team's fixes are working correctly.

## Overview

The verification system consists of multiple tools and scripts that automatically test each component of the engine-sim-CLI:

1. **Audio Output Verification** - Tests sine mode audio output with frequency measurement
2. **Input Response Verification** - Tests interactive mode keypress response times
3. **Engine Startup Verification** - Tests engine mode startup and RPM stabilization
4. **Binary Verification** - Verifies executable and library dependencies
5. **Configuration Verification** - Validates engine configuration files

## Critical Failures Being Tested

The system is designed to detect and verify fixes for the following critical failures:

1. **Sine mode produces no sound**
   - Frequency detection and amplitude measurement
   - Audio file generation validation
   - dB level verification

2. **Keypress response doesn't work**
   - Response time measurement (< 50ms threshold)
   - All keyboard controls verification
   - No hanging or crashes

3. **Engine mode hangs**
   - Startup time monitoring
   - RPM stabilization tracking
   - Process hang detection

## Components

### 1. Main Verification Script (`verification_system.sh`)

The main entry point for running verification tests.

**Usage:**
```bash
./verification_system.sh [options]
```

**Options:**
- `--mode <full|audio|input|engine|binary>` - Test mode (default: full)
- `--engine-sim-cli <path>` - Path to engine-sim-cli executable
- `--test-rpm <rpm>` - Target RPM for testing (default: 2000)
- `--duration <seconds>` - Test duration in seconds (default: 10)
- `--verbose` - Enable verbose output
- `--output-dir <path>` - Output directory for reports
- `--report-file <file>` - Combined report file
- `--skip-binary-check` - Skip binary verification
- `--timeout <seconds>` - Individual test timeout
- `--retry-count <n>` - Retry failed tests
- `--continue-on-error` - Continue testing even if a test fails

**Example:**
```bash
# Run all tests
./verification_system.sh --engine-sim-cli ./engine-sim-cli

# Test only audio output
./verification_system.sh --mode audio --engine-sim-cli ./engine-sim-cli --test-rpm 3000

# Quick test with verbose output
./verification_system.sh --mode full --engine-sim-cli ./engine-sim-cli --verbose --duration 5
```

### 2. Audio Verification Tool (`verify_audio.c`)

C program to measure audio output frequency, amplitude, and quality.

**Features:**
- Frequency detection using autocorrelation
- Amplitude measurement in dBFS
- Clipping detection
- THD (Total Harmonic Distortion) calculation
- Frequency stability analysis

**Usage:**
```bash
gcc -o verify_audio verify_audio.c -lm
./verify_audio --input test.wav --expected-rpm 2000 --detailed --output-report audio_report.json
```

### 3. Input Verification Tool (`verify_input.c`)

C program to test interactive mode keypress response times.

**Features:**
- Response time measurement
- All keyboard controls testing
- Hang and crash detection
- Success rate tracking

**Usage:**
```bash
gcc -o verify_input verify_input.c -lm
./verify_input --engine-sim-cli ./engine-sim-cli --duration 10.0 --output input_report.json
```

### 4. Engine Verification Tool (`verify_engine.c`)

C program to test engine startup and RPM stabilization.

**Features:**
- Startup time measurement
- RPM stabilization tracking
- Process monitoring
- Statistics collection

**Usage:**
```bash
gcc -o verify_engine verify_engine.c -lm
./verify_engine --engine-sim-cli ./engine-sim-cli --test-rpm 2000 --duration 10 --output engine_report.json
```

### 5. Test Cases Configuration (`test_cases.json`)

JSON file defining test scenarios and expected results.

**Key Sections:**
- `test_cases` - Individual test definitions
- `test_scenarios` - Pre-defined test sequences
- `diagnostics` - Analysis parameters

### 6. CI Verification Script (`ci_verification.sh`)

Script optimized for CI environments with faster execution.

**Features:**
- Quick mode for critical tests only
- Parallel test execution
- Prerequisites checking
- Summary report generation

**Usage:**
```bash
# Quick CI run (critical tests only)
./ci_verification.sh --quick

# Full CI run with parallel tests
./ci_verification.sh --parallel --engine-sim-cli ./engine-sim-cli

# Custom CI configuration
./ci_verification.sh --rpm 3000 --duration 8 --timeout 30
```

## Test Modes

### Full Mode
Runs all tests in sequence:
1. Binary verification
2. Configuration verification
3. Engine startup test
4. Audio output test
5. Input response test

### Audio Mode
Only tests audio output generation and quality:
- Sine mode frequency verification
- WAV file generation
- Audio analysis

### Input Mode
Only tests interactive mode controls:
- Keypress response time
- All keyboard functionality
- No hanging detection

### Engine Mode
Only tests engine startup and RPM control:
- Startup time measurement
- RPM stabilization tracking
- Process monitoring

### Binary Mode
Only verifies binary dependencies:
- Executable existence and permissions
- Library dependencies
- File integrity

## Expected Results

### Audio Output Tests
- Frequency accuracy: 95% or better
- Response time: < 5ms
- Amplitude: -20 dBFS minimum
- No clipping detected
- Frequency stability: > 90%

### Input Response Tests
- Response time: < 50ms
- All keys functional
- No hangs or crashes
- Success rate: 100%

### Engine Tests
- Startup time: < 5 seconds
- RPM stabilization: < 3 seconds
- RPM tolerance: ±50 RPM
- No process hangs

## Output Reports

The system generates several types of reports:

1. **Individual test reports** - JSON files with detailed results
2. **Combined verification report** - Summary of all tests
3. **CI summary report** - Optimized for CI systems
4. **Test run directory** - Contains all test artifacts

## Integration

### Build Integration
Add verification to your build process:
```bash
# After building
./verification_system.sh --engine-sim-cli ./build/engine-sim-cli
```

### CI Integration
Add to your CI pipeline:
```yaml
steps:
  - build: ...
  - script: |
      ./ci_verification.sh --quick
      ./ci_verification.sh --parallel --engine-sim-cli ./build/engine-sim-cli
```

### Pre-commit Hook
Create a pre-commit hook for basic testing:
```bash
#!/bin/bash
./verification_system.sh --mode binary --engine-sim-cli ./engine-sim-cli --continue-on-error
```

## Troubleshooting

### Common Issues

1. **Engine-sim-CLI not found**
   - Ensure the path to the executable is correct
   - Check that the file has execute permissions

2. **Test timeouts**
   - Increase timeout duration with `--timeout`
   - Check system resources (CPU, memory)

3. **Missing dependencies**
   - Verify engine-sim-bridge library is available
   - Check configuration files exist

4. **Audio tests fail**
   - Check audio device permissions
   - Verify output directory is writable

### Debug Mode
Enable verbose output for debugging:
```bash
./verification_system.sh --verbose --engine-sim-cli ./engine-sim-cli
```

## Exit Codes

- `0` - All tests passed
- `1` - One or more tests failed
- `2` - Invalid arguments
- `3` - Missing prerequisites
- `4` - Test timeout

## Contributing

When adding new verification tests:

1. Update `test_cases.json` with new test definitions
2. Add C verification tools if needed
3. Update the main verification script
4. Add CI integration if applicable
5. Update this documentation

## License

This verification system is part of the engine-sim-cli project and follows the same license terms.