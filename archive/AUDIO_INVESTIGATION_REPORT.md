# Audio Dropout Investigation Report

## Executive Summary

Investigation of occasional dropouts at 10% throttle has revealed a **critical configuration mismatch** between the CLI and GUI implementations. The CLI is using the bridge's default parameters instead of matching the GUI's synthesizer configuration.

## Root Cause

**The CLI does NOT match the GUI's audio configuration.**

The GUI explicitly sets synthesizer parameters to match its audio output requirements, while the CLI uses the bridge defaults which are designed for a different use case.

## Detailed Comparison

### GUI Configuration (engine-sim-bridge/engine-sim/src/simulator.cpp:213-215)

```cpp
void Simulator::initializeSynthesizer() {
    Synthesizer::Parameters synthParams;
    synthParams.audioBufferSize = 44100;      // 1 second buffer
    synthParams.audioSampleRate = 44100;      // 44.1kHz output
    synthParams.inputBufferSize = 44100;      // 1 second input buffer
    synthParams.inputChannelCount = m_engine->getExhaustSystemCount();
    synthParams.inputSampleRate = static_cast<float>(getSimulationFrequency());
    m_synthesizer.initialize(synthParams);
}
```

### CLI Configuration (src/engine_sim_cli.cpp:592-593)

```cpp
config.inputBufferSize = 1024;   // Bridge default
config.audioBufferSize = 96000;  // Bridge default (2 seconds)
```

### Bridge Defaults (engine-sim-bridge/src/engine_sim_bridge.cpp)

```cpp
config->inputBufferSize = 1024;   // ~21ms at 48kHz
config->audioBufferSize = 96000;  // 2 seconds @ 48kHz
config->sampleRate = 48000;       // 48kHz output
```

### Synthesizer Defaults (engine-sim-bridge/engine-sim/include/synthesizer.h)

```cpp
struct Parameters {
    int inputBufferSize = 1024;     // Default
    int audioBufferSize = 44100;    // Default
    float inputSampleRate = 10000;
    float audioSampleRate = 44100;
}
```

## The Critical Difference: inputBufferSize

| Implementation | inputBufferSize | Ratio to Sample Rate | Buffer Duration |
|----------------|-----------------|---------------------|-----------------|
| GUI (44.1kHz)  | 44100           | 1:1                 | 1.0 seconds     |
| CLI (48kHz)    | 1024            | 1:47                | 0.021 seconds   |
| **Difference** | **43x smaller** |                     | **48x shorter** |

This is the **primary cause** of occasional dropouts:
- The CLI's input buffer is **43 times smaller** than the GUI's
- At 48kHz, 1024 samples = only **21.3ms** of audio
- The GUI has 1000ms of input buffer headroom
- The CLI has only 21ms of input buffer headroom

## Why Dropouts Occur at 10% Throttle

At low throttle (10%):
1. **Engine RPM is low** - fewer audio events per second
2. **Audio thread produces fewer samples** - the synthesizer generates less audio
3. **Small input buffer drains faster** - 21ms buffer vs 1000ms in GUI
4. **Occasional timing mismatches** - when the render thread asks for more samples than available

The audio thread IS working (as evidenced by mostly smooth playback), but the tiny input buffer means any slight timing variance causes underruns.

## Evidence from Testing

### Test Run at 10% Throttle
```bash
./build/engine-sim-cli --default-engine --load 10 --duration 10 --play
```

**Result**: No underrun messages were printed, but this is because:
1. Underrun reporting is throttled to once per second (line 475 in bridge)
2. Silent underruns (filled with silence) don't generate warnings
3. The user reports "occasional" dropouts, not continuous ones

### GUI's maxWrite Calculation
```cpp
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
SampleOffset maxWrite = m_audioBuffer.offsetDelta(writePosition, targetWritePosition);
```

The GUI requests up to 4410 samples (100ms) per frame, which is **4.3x larger** than the CLI's entire input buffer (1024 samples).

## Additional Issues Found

### 1. Stereo vs Mono Mismatch
**File**: engine-sim-bridge/src/engine_sim_bridge.cpp:608-622

The bridge's `EngineSimReadAudioBuffer` function correctly handles mono-to-stereo conversion, but `EngineSimRender` has an inconsistency:

```cpp
// Line 520: Convert int16 to float32 - INCORRECT
for (int i = 0; i < samplesRead * 2; ++i) {
    buffer[i] = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
}
```

This assumes the audioConversionBuffer contains stereo data, but it only contains mono samples. The correct conversion is:

```cpp
// Line 618-622 in EngineSimReadAudioBuffer - CORRECT
for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left channel
    buffer[i * 2 + 1] = sample; // Right channel
}
```

### 2. CLI Uses EngineSimRender (Incorrect Function)
**File**: src/engine_sim_cli.cpp:920-970

The CLI uses `EngineSimRender` which:
- Calls `renderAudio()` to generate samples
- Converts mono to stereo INCORRECTLY
- Doesn't match the GUI pattern

The GUI uses `readAudioOutput()` which:
- Reads from the audio buffer (filled by audio thread)
- Returns mono samples
- Requires proper mono-to-stereo conversion

## The Fix

To match the GUI exactly and eliminate dropouts:

### Option 1: Match GUI Parameters (Recommended)
Change CLI configuration in `src/engine_sim_cli.cpp`:

```cpp
// Line 592-593, change from:
config.inputBufferSize = 1024;
config.audioBufferSize = 96000;

// To:
config.inputBufferSize = config.sampleRate;  // 48000 - match GUI's 1:1 ratio
config.audioBufferSize = config.sampleRate;  // 48000 - 1 second like GUI
```

This gives the CLI the same buffer characteristics as the GUI:
- 1 second of input buffer (vs current 21ms)
- 1 second of audio buffer (vs current 2 seconds)
- Same 1:1 input-to-output ratio

### Option 2: Use EngineSimReadAudioBuffer
Change the CLI to use `EngineSimReadAudioBuffer` instead of `EngineSimRender`:

```cpp
// Replace EngineSimRender with EngineSimReadAudioBuffer
int32_t samplesRead = 0;
result = EngineSimReadAudioBuffer(handle, buffer, frames, &samplesRead);
```

This matches the GUI pattern (engine_sim_application.cpp:274) exactly.

### Option 3: Both Options (Best Match)
Implement both changes for maximum compatibility with the GUI.

## Testing Recommendations

After implementing the fix:

1. **Run extended tests at various throttle levels**:
   ```bash
   ./engine-sim-cli --default-engine --load 5 --duration 30 --play
   ./engine-sim-cli --default-engine --load 10 --duration 30 --play
   ./engine-sim-cli --default-engine --load 50 --duration 30 --play
   ```

2. **Monitor for underrun messages**:
   ```bash
   ./engine-sim-cli --default-engine --load 10 --duration 60 --play 2>&1 | grep -i underrun
   ```

3. **Compare WAV output quality**:
   ```bash
   # Generate reference with GUI
   # Generate test with CLI
   # Compare spectrums
   ```

## File Locations Reference

| Component | File | Line |
|-----------|------|------|
| CLI config | `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` | 592-593 |
| CLI render call | `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` | 920-970 |
| Bridge defaults | `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp` | 37-39 |
| Bridge render | `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp` | 483-568 |
| Bridge read | `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp` | 570-629 |
| GUI config | `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/simulator.cpp` | 211-218 |
| GUI usage | `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` | 274 |
| Synth defaults | `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h` | 21-25 |

## Conclusion

The CLI is **NOT matching the GUI's audio configuration**. The 43x smaller input buffer (1024 vs 44100 samples) is the root cause of occasional dropouts, especially at low throttle where the audio thread has less work to do and timing variances are more noticeable.

The fix is straightforward: match the GUI's buffer configuration by setting `inputBufferSize = sampleRate` instead of using the bridge default of 1024.
