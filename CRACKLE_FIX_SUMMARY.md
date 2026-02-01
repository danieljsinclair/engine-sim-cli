# Audio Crackling Fix - Summary

## Problem
User reported crackling/static throughout audio playback:
> "Responsiveness is much better, but it's incredibly crackly. Sounds like static all the way through or a sparking speaker wire. Very, very dirty sound."

## Root Cause Analysis

### The Bug: Uninitialized Memory in Audio Buffer

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

**Functions Affected**:
1. `EngineSimRender()` (lines 557-567)
2. `EngineSimReadAudioBuffer()` (lines 627-646)

### Technical Details

#### Audio Flow
1. **AudioUnit Callback** (in `engine_sim_cli.cpp`):
   - Called by audio hardware when it needs samples
   - Requests 512 frames (stereo float32, range [-1.0, 1.0])
   - Calls `EngineSimReadAudioBuffer()` to get audio data

2. **Bridge Layer** (`EngineSimReadAudioBuffer()`):
   - Calls `readAudioOutput()` to read from synthesizer
   - Returns int16 mono samples, converts to float32 stereo
   - **BUG**: Only converts the samples that were returned
   - **BUG**: Leaves remaining buffer positions uninitialized

3. **Synthesizer** (`readAudioOutput()` in `synthesizer.cpp`):
   - Returns available samples (e.g., 400 when 512 requested)
   - Fills its internal int16 buffer with zeros for remaining samples
   - But the bridge doesn't convert those zeros to float!

#### The Crackling Mechanism

```cpp
// In EngineSimReadAudioBuffer():
int samplesRead = simulator->readAudioOutput(frames, int16Buffer);

// Convert ONLY the samples that were read
for (int i = 0; i < samplesRead; ++i) {  // Only loops 400 times
    const float sample = static_cast<float>(int16Buffer[i]) * scale;
    buffer[i * 2] = sample;     // Left channel
    buffer[i * 2 + 1] = sample; // Right channel
}
// Buffer positions [800] through [1023] are NEVER written!
// They contain uninitialized memory = random garbage = crackling!
```

The AudioUnit callback receives a buffer with:
- Positions 0-799: Valid audio samples
- Positions 800-1023: **Uninitialized memory** (garbage values)

When played through speakers, this garbage sounds like static, crackling, or "sparking speaker wire".

## The Fix

Zero-fill the remaining buffer positions after conversion:

```cpp
// Convert mono int16 to stereo float32 [-1.0, 1.0]
constexpr float scale = 1.0f / 32768.0f;

for (int i = 0; i < samplesRead; ++i) {
    const float sample = static_cast<float>(ctx->audioConversionBuffer[i]) * scale;
    buffer[i * 2] = sample;     // Left channel
    buffer[i * 2 + 1] = sample; // Right channel
}

// CRITICAL: Zero-fill any remaining frames to prevent crackling from uninitialized memory
// If we have a buffer underrun (samplesRead < frames), the rest of the buffer
// must be explicitly zeroed - otherwise the callback receives garbage data = static/crackling
if (samplesRead < frames) {
    const int remainingFrames = frames - samplesRead;
    float* silenceStart = buffer + samplesRead * 2;  // Stereo offset
    std::memset(silenceStart, 0, remainingFrames * 2 * sizeof(float));
}
```

### Changes Made

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

**Function**: `EngineSimRender()` (after line 561)
**Function**: `EngineSimReadAudioBuffer()` (after line 631)

Added zero-fill code to handle buffer underruns gracefully by filling remaining frames with silence instead of leaving them as uninitialized memory.

## Why This Happens

### Buffer Underruns
- The audio synthesizer runs on a separate thread at 10kHz
- The audio callback runs on the hardware's real-time thread
- Sometimes the callback requests more samples than are available
- This is a "buffer underrun" - normal in real-time audio systems

### Why the Bridge Didn't Handle It
The bridge conversion code only wrote to buffer positions it converted:
- `readAudioOutput()` returns the count of actual samples (e.g., 400)
- The loop only converts those 400 samples
- Remaining buffer positions were never initialized
- Result: **Garbage data output to speakers = crackling**

## Testing

### Build
```bash
cmake --build build --target engine-sim-cli
```

### Test
```bash
./build/engine-sim-cli --default-engine --interactive --play
```

### Expected Results
- No crackling or static in audio
- Smooth clean engine sound
- If buffer underruns occur, they manifest as brief silence (not crackling)
- Responsive to throttle changes

### Verification
- No "UNDERRUN" messages in console output
- Engine runs and stabilizes around 750-800 RPM
- Audio is clean throughout playback

## Code Quality Notes

### Real-Time Safety
- `memset()` is fast and allocation-free
- No dynamic memory allocation in audio callback
- No blocking calls in audio path
- Safe for real-time audio thread

### Performance
- Minimal overhead: only executes when underrun occurs
- Single `memset()` call vs. per-sample initialization
- Cache-friendly: sequential memory access

## Related Issues

This fix addresses the same class of bug that could affect:
- Any audio API that reads from the bridge (OpenAL, AudioUnit, etc.)
- WAV file export (though less noticeable since it's not real-time)
- Future audio implementations

## Conclusion

The crackling was caused by **uninitialized memory in the audio buffer** during buffer underruns. The fix ensures that any unfilled buffer positions are explicitly zeroed (silence) rather than containing random garbage data.

This is a common pitfall in real-time audio systems:
- Always initialize your buffers
- Handle buffer underruns gracefully
- Never assume the buffer is pre-initialized
- Test with underrun conditions

**Status**: Fixed in `engine-sim-bridge/src/engine_sim_bridge.cpp`
