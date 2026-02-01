# Audio Dropout Root Cause Analysis

## Executive Summary

**ROOT CAUSE**: The CLI uses an `inputBufferSize = 1024` which is **too small** for the `targetSynthesizerLatency = 0.05` (50ms). At 15% throttle, the adaptive feedback loop reduces simulation steps, causing input buffer underruns and audio dropouts.

## Audio Generation Path Comparison

### GUI (engine_sim_application.cpp)

**Path:**
1. **Line 234-235**: `startFrame(1 / avgFramerate)` - Starts frame with ~60fps (16.67ms dt)
2. **Line 239-241**: `while (m_simulator->simulateStep())` - Runs all simulation steps for this frame
   - Each step calls `writeToSynthesizer()` (simulator.cpp:152)
   - `writeToSynthesizer()` calls `synthesizer().writeInput()` (piston_engine_simulator.cpp:412)
   - `writeInput()` writes interpolated samples to input buffer (synthesizer.cpp:168-195)
3. **Line 245**: `endFrame()` - Calls `endInputBlock()` (simulator.cpp:167)
   - Sets `m_processed = false`
   - Notifies audio thread via `m_cv0.notify_one()`
4. **Line 274**: `readAudioOutput(maxWrite, samples)` - Reads from audio buffer

**Configuration:**
- `inputBufferSize = 44100` (simulator.cpp:215) - 1 second at 44100 Hz
- `audioBufferSize = 44100` (simulator.cpp:213)
- `audioSampleRate = 44100` (simulator.cpp:214)
- `inputSampleRate = 10000` (from simulation frequency)
- `targetSynthesizerLatency = 0.1` (simulator.cpp:12) - 100ms default

**Timing:**
- Update rate: ~60 fps (16.67ms per frame)
- Simulation steps per frame: ~166 (10000 Hz / 60 fps)
- Each `writeInput()` writes `44100/10000 = 4.41` samples (interpolated)
- Total input samples per frame: ~166 * 4.41 = 732 samples
- Input buffer capacity: 44100 samples (~60 frames worth)

### CLI (engine_sim_cli.cpp)

**Path:**
1. **Line 1058**: `EngineSimUpdate(handle, updateInterval)` - updateInterval = 1/60 (16.67ms)
   - Internally calls `startFrame()`, `while (simulateStep())`, `endFrame()`
   - Same internal flow as GUI
2. **Line 1087**: `EngineSimReadAudioBuffer(handle, writePtr, framesToReadNow, &samplesWritten)`
   - Reads from audio buffer via bridge

**Configuration:**
- `inputBufferSize = 1024` (engine_sim_cli.cpp:666) - **ONLY 23ms at 44100 Hz**
- `audioBufferSize = 96000` (engine_sim_cli.cpp:667)
- `sampleRate = 48000` (engine_sim_cli.cpp:556)
- `simulationFrequency = 10000` (engine_sim_cli.cpp:668)
- `targetSynthesizerLatency = 0.05` (engine_sim_cli.cpp:670) - **50ms (half of GUI)**

**Bridge override (engine_sim_bridge.cpp:239-250):**
```cpp
Synthesizer::Parameters synthParams;
synthParams.inputChannelCount = 2;
synthParams.inputBufferSize = ctx->config.inputBufferSize;  // 1024 from CLI
synthParams.audioBufferSize = ctx->config.audioBufferSize;  // 96000 from CLI
synthParams.inputSampleRate = static_cast<float>(ctx->config.simulationFrequency);  // 10000
synthParams.audioSampleRate = static_cast<float>(ctx->config.sampleRate);  // 48000
ctx->simulator->synthesizer().initialize(synthParams);
```

**Timing:**
- Update rate: 60 fps (16.67ms per frame)
- Simulation steps per frame: ~166 (same as GUI)
- Each `writeInput()` writes `48000/10000 = 4.8` samples (interpolated)
- Total input samples per frame: ~166 * 4.8 = 797 samples
- Input buffer capacity: **1024 samples (~1.3 frames worth)**

## The Critical Difference: Input Buffer Size

| Parameter | GUI | CLI | Ratio |
|-----------|-----|-----|-------|
| inputBufferSize | 44100 | 1024 | 43x larger in GUI |
| targetSynthesizerLatency | 0.1 (100ms) | 0.05 (50ms) | 2x larger in GUI |
| audioSampleRate | 44100 | 48000 | 1.09x larger in CLI |
| Input buffer capacity | ~60 frames | ~1.3 frames | **46x difference** |

## Why 15% Throttle Causes Dropouts But Idle Doesn't

### Adaptive Feedback Loop (simulator.cpp:80-89)

Every frame, `startFrame()` adjusts the number of simulation steps based on synthesizer latency:

```cpp
const double targetLatency = getSynthesizerInputLatencyTarget();  // 0.05 (50ms) in CLI
if (m_synthesizer.getLatency() < targetLatency) {
    m_steps = static_cast<int>((m_steps + 1) * 1.1);  // Increase steps
}
else if (m_synthesizer.getLatency() > targetLatency) {
    m_steps = static_cast<int>((m_steps - 1) * 0.9);  // Decrease steps
}
```

**At idle (~600 RPM, low exhaust flow):**
- Low data generation rate
- Input buffer latency is low (buffer empties quickly)
- `getLatency() < targetLatency` → **Increase m_steps by 10%**
- More simulation steps = more `writeInput()` calls = more input data
- System compensates for low data rate

**At 15% throttle (~1200-1500 RPM, higher exhaust flow):**
- Higher data generation rate
- Input buffer fills up faster
- `getLatency() > targetLatency` → **Decrease m_steps by 10%**
- Fewer simulation steps = fewer `writeInput()` calls = less input data
- **PROBLEM**: CLI's input buffer is only 1024 samples (~1.3 frames)
- When m_steps decreases, input buffer can underrun between frames

### The Dropout Mechanism

1. **Frame N** (at 15% throttle):
   - Input buffer has ~800 samples
   - Audio thread reads ~800 samples
   - `getLatency()` returns high value
   - Feedback loop: `m_steps *= 0.9` (reduce to ~150 steps)

2. **Frame N+1**:
   - Only ~150 `writeInput()` calls → ~720 samples written
   - Audio thread needs ~800 samples
   - **Shortfall: 80 samples**
   - `readAudioOutput()` returns partial data

3. **Audio thread (synthesizer.cpp:232-234)**:
   ```cpp
   const int n = std::min(
       std::max(0, 2000 - (int)m_audioBuffer.size()),
       (int)m_inputChannels[0].data.size());  // Limited by available input!
   ```
   - If `m_inputChannels[0].data.size()` < 2000, `n` is limited
   - Audio buffer doesn't get filled fast enough
   - **Dropout occurs**

4. **Frame N+2**:
   - Input buffer is now depleted
   - Audio thread wakes up (line 225-230) but finds insufficient data
   - Severe dropout or silence

## Why GUI Works Perfectly

GUI's `inputBufferSize = 44100` provides **60 frames of buffering**:
- Even if the feedback loop reduces steps temporarily
- The large buffer absorbs the variation
- No underruns occur
- Smooth audio output

## The Fix

**Solution: Increase CLI's `inputBufferSize` to match GUI**

Change line 666 of `engine_sim_cli.cpp`:
```cpp
// OLD (causes dropouts):
config.inputBufferSize = 1024;

// NEW (matches GUI):
config.inputBufferSize = 44100;  // Or calculate: sampleRate * targetLatency * 2
```

**Better yet**: Calculate dynamically based on sample rate and target latency:
```cpp
config.inputBufferSize = static_cast<int>(sampleRate * config.targetSynthesizerLatency * 2);
// For 48000 Hz * 0.05 * 2 = 4800 samples (provides 2x safety margin)
```

**Alternative fix**: Increase `targetSynthesizerLatency` to match GUI:
```cpp
config.targetSynthesizerLatency = 0.1;  // 100ms (same as GUI)
```

**Best solution**: Do both:
```cpp
config.targetSynthesizerLatency = 0.1;  // Match GUI's 100ms
config.inputBufferSize = static_cast<int>(sampleRate * config.targetSynthesizerLatency * 2);
// 48000 * 0.1 * 2 = 9600 samples
```

## Verification

To verify this is the root cause:

1. **Test 1**: Increase `inputBufferSize` to 44100
   - Expected: Dropouts disappear at all throttle levels

2. **Test 2**: Monitor `m_steps` and input buffer size during throttle changes
   - Expected: CLI shows wild swings in m_steps; GUI shows stable behavior

3. **Test 3**: Measure actual input buffer latency
   - Expected: CLI fluctuates wildly; GUI stays stable

## Related Files

- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` (line 666-670)
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/simulator.cpp` (line 67-96, 211-219)
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp` (line 168-213, 222-256)
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp` (line 239-250)
