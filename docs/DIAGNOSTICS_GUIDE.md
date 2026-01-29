# Diagnostics Guide - engine-sim-cli

## Overview

This guide explains how to use the diagnostic tools in engine-sim-cli to identify and resolve issues with engine simulation and audio output.

## What is diagnostics.cpp?

`diagnostics.cpp` is a comprehensive diagnostic tool that tests each stage of the engine simulation and audio pipeline independently. It helps identify exactly where issues occur in the system.

## What Does It Test?

The diagnostic tool tests 5 critical stages:

1. **Stage 1: Engine Simulation** - Verifies the engine is generating RPM
2. **Stage 2: Combustion Events** - Infers combustion activity from engine behavior
3. **Stage 3: Exhaust Flow** - Measures raw exhaust gas flow from the engine
4. **Stage 4: Synthesizer Input** - Confirms data is reaching the audio synthesizer
5. **Stage 5: Audio Output** - Validates final audio samples are being generated

## Building the Diagnostic Tool

### Method 1: Manual Compilation (Recommended)

```bash
cd /Users/danielsinclair/vscode/engine-sim-cli

g++ -std=c++17 -O2 -Wall -Wextra \
    -I./engine-sim-bridge/include \
    diagnostics.cpp \
    -o diagnostics \
    build/engine-sim-bridge/src/libengine-sim-bridge.a \
    -pthread
```

The compiled binary will be at `./diagnostics`.

### Method 2: Using CMake Build Artifacts

If you've already built the main project with CMake:

```bash
cd build
cmake ..
make
g++ -std=c++17 -O2 -Wall -Wextra \
    -I../engine-sim-bridge/include \
    ../diagnostics.cpp \
    -o diagnostics \
    engine-sim-bridge/src/libengine-sim-bridge.a \
    -pthread
mv diagnostics ..
```

## Usage

### Basic Usage

```bash
./diagnostics <engine_config.mr> [duration_seconds]
```

### Arguments

- `engine_config.mr` - Path to the engine configuration file (.mr format)
- `duration_seconds` - (Optional) Duration in seconds (default: 5.0)

### Optional Arguments

- `--output <path>` - Specify custom output WAV file path (default: diagnostic_output.wav)
- `--help` or `-h` - Show help message

### Examples

```bash
# Run 5-second diagnostic on default engine
./diagnostics engine-sim/assets/main.mr

# Run 10-second diagnostic on specific engine
./diagnostics es/v8_engine.mr 10.0

# Run with custom output file
./diagnostics es/v8_engine.mr 5.0 --output my_test.wav

# Show help
./diagnostics --help
```

## Understanding the Output

### Phase 1: Warmup

The tool first warms up the engine for 2 seconds with gradually increasing throttle:

```
Phase 1: Warming up engine (2.0s)
  Warmup: 0.5s | RPM: 250 | Flow: 1.23e-05 m3/s
  Warmup: 1.0s | RPM: 550 | Flow: 3.45e-05 m3/s
  Warmup: 1.5s | RPM: 780 | Flow: 6.78e-05 m3/s
Phase 1 complete. Starter motor disabled.
```

**Purpose**: Ensures the engine reaches self-sustaining RPM before collecting diagnostic data.

### Phase 2: Data Collection

The main diagnostic phase collects statistics at 80% throttle:

```
Phase 2: Collecting diagnostic data (5.0s)
  Progress: 10% | RPM: 850 | Flow: 7.89e-05 m3/s
  Progress: 20% | RPM: 920 | Flow: 8.12e-05 m3/s
  [1.0s] RPM: 950 | Load: 80.0% | Flow: 8.45e-05 m3/s
  [2.0s] RPM: 980 | Load: 80.0% | Flow: 8.67e-05 m3/s
```

**Purpose**: Collects detailed statistics about each stage of the pipeline.

### Diagnostic Report

After completion, a detailed report is generated:

```
==========================================
       DIAGNOSTIC REPORT
==========================================

STAGE 1: ENGINE SIMULATION
----------------------------
  RPM Range:      250.0 - 1850.0 RPM
  Average RPM:    950.0 RPM
  Samples:        300
  Status:         PASS (Engine is simulating)

STAGE 2: COMBUSTION EVENTS
----------------------------
  Total Events:   0
  Status:         UNKNOWN (Cannot directly measure combustion events)
                  Check if RPM > 0 and exhaust flow > 0

STAGE 3: EXHAUST FLOW (RAW)
----------------------------
  Flow Range:     1.23e-05 - 9.45e-05 m^3/s
  Average Flow:   7.82e-05 m^3/s
  Samples:        300
  Zero Flow Count: 0 / 300
  Status:         PASS (Exhaust flow detected)

STAGE 4: SYNTHESIZER INPUT
----------------------------
  Input Range:    0.00e+00 - 0.00e+00
  Average Input:  0.00e+00
  Samples:        0
  Status:         UNKNOWN (Synthesizer input not directly measurable)
                  Check if exhaust flow > 0

STAGE 5: AUDIO OUTPUT
----------------------------
  Frames Rendered: 240000
  Samples Rendered: 480000
  Audio Level:    0.000000 - 0.123456
  Average Level:  0.023456
  Active Frames:  4000 / 4000
  Silent Frames:  0 / 4000
  Silent Samples: 1234 / 480000 (0.3%)
  Clipped Samples: 0
  Status:         PASS (Audio samples generated)

BUFFER STATUS
----------------------------
  Successful Reads: 300
  Failed Reads:     0
  Buffer Underruns: 2
  Buffer Overruns:  0

==========================================
       OVERALL SUMMARY
==========================================
  Stage 1 (Engine):    PASS
  Stage 2 (Combustion):PASS (inferred)
  Stage 3 (Exhaust):   PASS
  Stage 4 (Synthesizer):UNKNOWN
  Stage 5 (Audio):     PASS
==========================================

ISSUES DETECTED
==========================================
  No critical issues detected. Audio chain working correctly.
==========================================
```

## Interpreting Results

### Stage 1: Engine Simulation

**PASS**: Engine is running (RPM > 0)
- Engine simulation is working correctly
- Ignition and starter motor are functioning

**FAIL**: Engine not running (RPM = 0)
- Check ignition is enabled
- Check starter motor is working
- Verify engine configuration is valid

### Stage 2: Combustion Events

**PASS (inferred)**: RPM > 0 AND exhaust flow > 0
- Engine is likely combusting fuel

**UNKNOWN**: Cannot directly measure
- Use Stage 1 and Stage 3 to infer combustion status

### Stage 3: Exhaust Flow

**PASS**: Exhaust flow > 1e-9 m^3/s
- Combustion is generating exhaust gases

**FAIL**: Exhaust flow = 0
- Engine may not be combusting
- Check fuel system configuration
- Check ignition timing

### Stage 4: Synthesizer Input

**PASS (inferred)**: Exhaust flow > 0
- Data should be reaching synthesizer

**UNKNOWN**: Cannot directly measure via bridge API
- If Stage 3 passes but Stage 5 fails, issue is in synthesizer

### Stage 5: Audio Output

**PASS**: Audio level > 1e-6
- Audio samples are being generated

**CORRUPTED**: NaN or Inf detected
- Check buffer handling
- Check sample rate conversion

**WARNING**: Samples out of range (> 1.0)
- Check volume settings
- Check synthesizer gain

**FAIL**: All samples silent
- Check synthesizer configuration
- Verify impulse responses are loaded
- Check volume settings

## New Diagnostic Features

The enhanced diagnostics.cpp includes these features (ported from audio_chain_diagnostics.cpp):

### 1. NaN/Inf Corruption Detection

Detects corrupted floating-point values in audio output:

```cpp
if (std::isnan(sample)) {
    stats.hasNaN = true;
}
if (std::isinf(sample)) {
    stats.hasInf = true;
}
```

**Reported in**: "ISSUES DETECTED" section

### 2. Buffer Underrun/Overrun Tracking

Monitors buffer health during audio rendering:

```cpp
if (samplesWritten < framesToRender) {
    stats.bufferUnderruns++;
}
```

**Reported in**: "BUFFER STATUS" section

### 3. Configurable Output Path

Specify custom output file location:

```bash
./diagnostics engine.mr 5.0 --output custom_output.wav
```

### 4. Silent Samples Percentage

Calculates the percentage of silent samples:

```
Silent Samples: 1234 / 480000 (0.3%)
```

**Useful for**: Detecting partial audio output issues

### 5. Clipped Samples Detection

Detects samples exceeding valid range:

```cpp
if (std::abs(sample) > 1.0f) {
    stats.hasOutOfRange = true;
    stats.clippedSamples++;
}
```

**Reported in**: Stage 5 and "ISSUES DETECTED" sections

## Common Issues and Solutions

### Issue: Stage 1 FAIL (Engine not running)

**Symptoms**: RPM stays at 0

**Possible Causes**:
1. Engine configuration file is invalid
2. Ignition not enabled
3. Starter motor not engaging

**Solutions**:
```bash
# Verify engine file exists
ls -la engine-sim/assets/main.mr

# Check for error messages in diagnostics output
# The tool will report if EngineSimLoadScript fails
```

### Issue: Stage 3 FAIL (No exhaust flow)

**Symptoms**: RPM > 0 but exhaust flow = 0

**Possible Causes**:
1. Fuel system not configured
2. No combustion occurring
3. Engine stuck in motoring mode

**Solutions**:
- Check engine configuration for fuel system definition
- Verify throttle is opening (tool uses 80% throttle)
- Try longer warmup period

### Issue: Stage 5 FAIL (Silent audio)

**Symptoms**: All stages pass but audio output is silent

**Possible Causes**:
1. Impulse responses not loaded
2. Synthesizer not initialized
3. Volume too low

**Solutions**:
- Check if impulse response files exist in `es/sound-library`
- Verify asset base path is correct
- Try increasing volume in diagnostics code

### Issue: Data Corruption (NaN/Inf)

**Symptoms**: "CORRUPTED" status in Stage 5

**Possible Causes**:
1. Buffer handling errors
2. Sample rate conversion issues
3. Uninitialized memory

**Solutions**:
- Check buffer sizes and array indices
- Verify sample rate consistency
- Review memory allocation code

## Troubleshooting Tips

### 1. Check the WAV Output

The tool saves `diagnostic_output.wav` (or custom path with `--output`). Listen to it to verify if audio is actually silent or just very quiet.

### 2. Compare with Working Configs

Test with known-good engine configurations like `engine-sim/assets/main.mr`.

### 3. Increase Duration

For engines that take longer to warm up, increase the duration parameter:

```bash
./diagnostics engine.mr 10.0  # 10 seconds instead of 5
```

### 4. Check Paths

Ensure all paths to engine configs and asset libraries are absolute paths.

### 5. Review Errors

The tool will report any errors from the engine-sim bridge API. Check for:
- Script compilation errors
- File not found errors
- Initialization failures

## Advanced Usage

### Modifying Diagnostic Parameters

You can modify these constants in `diagnostics.cpp`:

```cpp
const double warmupDuration = 2.0;      // Warmup time in seconds
const double throttle = 0.8;             // Main phase throttle (0.0-1.0)
config.volume = 1.0f;                    // Master volume
config.convolutionLevel = 0.5f;          // Convolution mix
```

### Adding Custom Metrics

The `DiagnosticStats` structure can be extended to track additional metrics:

```cpp
struct DiagnosticStats {
    // Add your custom metrics here
    double customMetric = 0.0;

    void updateCustomMetric(double value) {
        // Your implementation
    }
};
```

## API Reference

The diagnostics tool uses the engine-sim-bridge API:

- `EngineSimCreate()` - Initialize simulator
- `EngineSimLoadScript()` - Load engine configuration
- `EngineSimStartAudioThread()` - Start audio processing
- `EngineSimSetIgnition()` - Enable/disable ignition
- `EngineSimSetStarterMotor()` - Enable/disable starter
- `EngineSimSetThrottle()` - Set throttle position
- `EngineSimUpdate()` - Advance simulation
- `EngineSimRender()` - Render audio samples
- `EngineSimGetStats()` - Get engine statistics
- `EngineSimDestroy()` - Clean up resources

See `engine-sim-bridge/include/engine_sim_bridge.h` for full API documentation.

## Interpreting Specific Metrics

### RPM Values

- **0 RPM**: Engine not running
- **100-500 RPM**: Cranking/starting
- **500-1000 RPM**: Idle range
- **1000-3000 RPM**: Normal operating range
- **3000+ RPM**: High RPM operation

### Exhaust Flow Values

- **0 m^3/s**: No combustion
- **1e-9 to 1e-7 m^3/s**: Very low flow (may indicate issues)
- **1e-7 to 1e-5 m^3/s**: Normal idle flow
- **1e-5+ m^3/s**: Higher flow under load

**Note**: Exhaust flow values can be negative during certain simulation states. This is expected behavior.

### Audio Levels

- **0.0**: Complete silence
- **1e-6 to 1e-4**: Very quiet audio
- **1e-4 to 1e-2**: Normal audio levels
- **1e-2 to 1e-1**: Loud audio
- **> 1.0**: Clipping/distortion (should not occur)

## Getting Help

When asking for help, include:

1. The complete diagnostic report (copy-paste the output)
2. The engine configuration file you're using
3. Any error messages from the diagnostic tool
4. Information about your system (OS, compiler, etc.)
5. The `diagnostic_output.wav` file if applicable

## Cleaning Up

```bash
# Remove generated WAV files
rm diagnostic_output.wav
rm *.wav

# Remove compiled diagnostic tool
rm diagnostics

# Completely clean build artifacts
rm -rf build/
```

## Related Documentation

- `DEBUGGING_HISTORY.md` - Technical history of bug fixes
- `TESTING_GUIDE.md` - Testing procedures and test scenarios

---

**Document Version**: 1.0
**Last Updated**: January 28, 2026
**Status**: Complete - All diagnostic features documented
