# Audio Configuration Comparison Table

## Parameter Comparison

| Parameter | GUI (44.1kHz) | GUI (48kHz equiv) | CLI Current | CLI Recommended | Difference |
|-----------|---------------|-------------------|-------------|-----------------|------------|
| **Sample Rate** | 44100 Hz | 48000 Hz | 48000 Hz | 48000 Hz | ✓ Same |
| **inputBufferSize** | 44100 | 48000 | 1024 | **48000** | **43x too small** |
| **audioBufferSize** | 44100 | 48000 | 96000 | **48000** | 2x too large |
| **Input Buffer Duration** | 1000 ms | 1000 ms | **21.3 ms** | **1000 ms** | **48x too short** |
| **Audio Buffer Duration** | 1000 ms | 1000 ms | 2000 ms | **1000 ms** | 2x too long |
| **Input:Output Ratio** | 1:1 | 1:1 | 1:47 | **1:1** | Mismatch |

## Buffer Duration Calculations

### GUI (44.1kHz)
```
inputBufferSize = 44100 samples
duration = 44100 / 44100 Hz = 1.000 second
```

### CLI Current (48kHz)
```
inputBufferSize = 1024 samples
duration = 1024 / 48000 Hz = 0.0213 seconds (21.3 ms)
```

### CLI Recommended (48kHz)
```
inputBufferSize = 48000 samples
duration = 48000 / 48000 Hz = 1.000 second
```

## Impact Analysis

### Current CLI Issues

1. **Input Buffer Starvation**
   - 21ms buffer can drain between render calls
   - At 60 FPS, each frame has 16.6ms budget
   - Only 4.7ms margin for timing variations
   - Any delay > 4.7ms causes underrun

2. **GUI Comparison**
   - 1000ms buffer at 60 FPS = 16.6ms per frame
   - 983.4ms margin for timing variations
   - Virtually impossible to underrun

3. **Low Throttle Sensitivity**
   - At 10% throttle, fewer audio events
   - Audio thread produces less output
   - Small input buffer drains faster
   - Higher probability of underrun

## The Fix

### Change Required in src/engine_sim_cli.cpp

**Lines 592-593:**
```cpp
// BEFORE (Current - MISMATCH):
config.inputBufferSize = 1024;
config.audioBufferSize = 96000;

// AFTER (Fixed - MATCH GUI):
config.inputBufferSize = config.sampleRate;  // 48000
config.audioBufferSize = config.sampleRate;  // 48000
```

### Why This Works

1. **Matches GUI Ratio**: inputBufferSize = sampleRate (1:1 ratio)
2. **Adequate Headroom**: 1 second buffer = 983ms margin at 60 FPS
3. **Prevents Underruns**: Timing variations have plenty of buffer
4. **Consistent Duration**: Same 1-second duration as GUI

## Verification

After applying the fix, verify with:

```bash
# Test at low throttle (most likely to show issues)
./engine-sim-cli --default-engine --load 5 --duration 60 --play 2>&1 | grep -i underrun

# Test at medium throttle
./engine-sim-cli --default-engine --load 50 --duration 60 --play 2>&1 | grep -i underrun

# Test at high throttle
./engine-sim-cli --default-engine --load 100 --duration 60 --play 2>&1 | grep -i underrun
```

Expected result: **No underrun messages** at any throttle level.

## Additional Note: Stereo Conversion Bug

The investigation also revealed a stereo conversion bug in `EngineSimRender`:

**File**: engine-sim-bridge/src/engine_sim_bridge.cpp:520
```cpp
// INCORRECT - assumes stereo input
for (int i = 0; i < samplesRead * 2; ++i) {
    buffer[i] = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
}
```

Should be:
```cpp
// CORRECT - mono to stereo conversion
for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left channel
    buffer[i * 2 + 1] = sample; // Right channel
}
```

This is already correctly implemented in `EngineSimReadAudioBuffer` (line 618-622).
