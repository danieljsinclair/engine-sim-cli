# Audio Dropout Fix - Implementation Summary

## Problem Statement

User reports occasional dropouts at 10% throttle despite audio thread implementation.

## Root Cause

The CLI is using bridge default parameters instead of matching the GUI's synthesizer configuration:

| Parameter | GUI | CLI Current | CLI Fix |
|-----------|-----|-------------|---------|
| inputBufferSize | 44100 (1s @ 44.1kHz) | 1024 (21ms @ 48kHz) | **48000 (1s @ 48kHz)** |
| audioBufferSize | 44100 (1s) | 96000 (2s) | **48000 (1s)** |

The CLI's input buffer is **43x smaller** than the GUI's, causing:
- Only 21ms of input buffer vs 1000ms in GUI
- Insufficient headroom for timing variations
- Occasional underruns at low throttle

## The Fix

### File: src/engine_sim_cli.cpp

**Location**: Lines 592-593 in the `runSimulation()` function

**Current Code**:
```cpp
config.inputBufferSize = 1024;
config.audioBufferSize = 96000;
```

**Fixed Code**:
```cpp
config.inputBufferSize = config.sampleRate;  // 48000 - match GUI's 1:1 ratio
config.audioBufferSize = config.sampleRate;  // 48000 - 1 second buffer like GUI
```

**Rationale**:
- Matches GUI's 1:1 input-to-output ratio
- Provides 1 second of input buffer (vs current 21ms)
- Eliminates underruns by providing adequate headroom
- Consistent with GUI's buffer management strategy

## Additional Finding: Stereo Conversion Bug

### File: engine-sim-bridge/src/engine_sim_bridge.cpp

**Location**: Line 520 in `EngineSimRender` function

**Current Code (INCORRECT)**:
```cpp
for (int i = 0; i < samplesRead * 2; ++i) {
    buffer[i] = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
}
```

**Fixed Code (CORRECT)**:
```cpp
for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left channel
    buffer[i * 2 + 1] = sample; // Right channel
}
```

**Note**: This is already correctly implemented in `EngineSimReadAudioBuffer` (lines 618-622).

## Testing

### Before Fix
```bash
./build/engine-sim-cli --default-engine --load 10 --duration 60 --play 2>&1 | grep -i underrun
# Expected: Occasional underruns (or silent dropouts)
```

### After Fix
```bash
./build/engine-sim-cli --default-engine --load 10 --duration 60 --play 2>&1 | grep -i underrun
# Expected: No underruns
```

### Comprehensive Test
```bash
# Test at various throttle levels
for throttle in 5 10 25 50 75 100; do
    echo "Testing at ${throttle}% throttle..."
    ./build/engine-sim-cli --default-engine --load $throttle --duration 30 --play 2>&1 | grep -i underrun
done
```

## Expected Outcome

After implementing the fix:
1. **No underruns** at any throttle level
2. **Smooth playback** even at 10% throttle
3. **Consistent behavior** with the GUI
4. **Proper stereo imaging** (if stereo bug is also fixed)

## Files to Modify

1. **Primary Fix**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` (lines 592-593)
2. **Secondary Fix**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp` (line 520)

## References

- GUI Configuration: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/simulator.cpp:213`
- Bridge Defaults: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp:37-39`
- Synthesizer Defaults: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h:21-25`
- GUI Usage: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp:274`
