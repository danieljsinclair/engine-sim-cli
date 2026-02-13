# Interactive Play Dropout Investigation

## Executive Summary

| Aspect | Finding |
|--------|---------|
| **Root Cause** | Buffer overflow in `audioBuffer` after 3 seconds |
| **Location** | `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` line 1168 |
| **Trigger** | `--interactive --play` mode running longer than 3 seconds |
| **Why WAV works** | Buffer size matches duration, loop terminates before overflow |
| **Fix required** | Circular buffer or larger buffer for interactive mode |

---

## CRITICAL FINDING: Buffer Overflow in Interactive Mode

**Root Cause**: The `audioBuffer` is allocated with a fixed 3-second capacity in interactive mode, but the interactive loop runs indefinitely, causing a buffer overflow that corrupts memory and causes audio dropouts.

---

## Evidence

### 1. Audio Buffer Allocation (Lines 882-886)

```cpp
// Setup recording buffer
// For smooth audio: allocate full duration buffer, write sequentially (like sine wave test)
const int bufferFrames = static_cast<int>(args.duration * sampleRate);
const int totalSamples = bufferFrames * channels;
std::vector<float> audioBuffer(totalSamples);
```

**Problem**: The buffer is sized based on `args.duration`, which defaults to **3.0 seconds** (line 465).

### 2. Duration Default Value (Lines 462-472)

```cpp
struct CommandLineArgs {
    const char* engineConfig = nullptr;
    const char* outputWav = nullptr;
    double duration = 3.0;  // ← DEFAULT IS 3 SECONDS
    double targetRPM = 0.0;
    double targetLoad = -1.0;
    bool interactive = false;
    bool playAudio = false;
    bool useDefaultEngine = false;
    bool sineMode = false;
};
```

**Problem**: In interactive mode, `--duration` is ignored (as stated in help text at line 484), so the default 3.0 seconds is used.

### 3. Interactive Main Loop (Line 996)

```cpp
while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
```

**Problem**: In interactive mode, the loop runs indefinitely (until user presses Q), but the buffer is only 3 seconds!

### 4. Buffer Write Calculation (Line 1168)

```cpp
float* writePtr = audioBuffer.data() + (framesRendered + framesReadTotal) * channels;
```

**Problem**: `framesRendered` increases without bound in interactive mode. Once `framesRendered` exceeds 144,000 (3 seconds at 48kHz), the write pointer goes **beyond the allocated buffer**.

### 5. Missing Bounds Check in Interactive Mode (Lines 1151-1155)

**Non-interactive mode** (HAS bounds check):
```cpp
// In non-interactive mode, check buffer limits
if (!args.interactive) {
    int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
    framesToRender = std::min(framesPerUpdate, totalExpectedFrames - framesProcessed);
}
```

**Interactive mode** (NO bounds check):
- When `args.interactive` is true, this entire check is SKIPPED
- `framesToRender` remains `framesPerUpdate` (800 frames)
- No limit on how much can be written to the buffer

### 6. Buffer Overflow Timeline

At 48kHz sample rate, 2 channels:

- **Buffer capacity**: 3.0 seconds × 48,000 samples/sec × 2 channels = **288,000 samples**
- **Buffer size in frames**: 144,000 frames
- **At 60 Hz update rate**: Each update produces ~800 frames
- **Buffer overflow occurs at**: 144,000 / 800 = **180 iterations** = **3.0 seconds**

After 3 seconds of interactive play:
- `framesRendered` = 144,000
- Write pointer = `audioBuffer.data() + 144,000 * 2` = **exactly at buffer end**
- After 3+ seconds, writes occur **beyond allocated memory**

---

## Code Path Comparison

### Non-Interactive Mode (WAV Export) - WORKS CORRECTLY

**Loop condition** (line 996):
```cpp
while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load()))
```

In non-interactive mode:
- Loop exits when `currentTime >= args.duration`
- Buffer size matches exactly the duration
- No overflow occurs

**Example**: If `--duration 10`, buffer is allocated for 10 seconds, loop runs for 10 seconds.

### Interactive Mode - BUFFER OVERFLOW

**Loop condition** (line 996):
```cpp
while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load()))
```

In interactive mode:
- Loop runs indefinitely (until `g_running` is false)
- Buffer is only 3 seconds (default duration)
- **Overflow occurs after 3 seconds!**

---

## Impact Analysis

### Why WAV Export Works

1. Buffer is allocated for exact duration
2. Loop terminates when buffer is full
3. All writes stay within bounds

### Why Interactive Play Fails

1. Buffer is only 3 seconds (288,000 samples)
2. Loop runs indefinitely (user can run for minutes)
3. After 3 seconds, writes corrupt memory
4. Memory corruption causes:
   - Audio dropouts
   - Potential crashes
   - Undefined behavior

### Why Intermediate Buffer Doesn't Help

The intermediate buffer (lines 888-897) is used for **playback queuing**, but the **source data** (`audioBuffer`) still overflows:

```cpp
// Line 1200-1205: Write from audioBuffer to intermediateBuffer
if (intermediateBuffer) {
    size_t sampleCount = samplesWritten * channels;
    const float* writePtr = audioBuffer.data() + (framesRendered - samplesWritten) * channels;
    intermediateBuffer->write(writePtr, sampleCount);
```

The intermediate buffer reads from `audioBuffer`, which is already corrupted after 3 seconds.

---

## The Fix

### Solution 1: Circular Buffer for Interactive Mode

Allocate a circular buffer for interactive mode that wraps around:

```cpp
// In interactive mode, use a circular buffer
const int bufferFrames = args.interactive ? sampleRate * 10 : static_cast<int>(args.duration * sampleRate);
//                                                             ^^^^^^^^^^^
//                                                            10-second circular buffer for interactive
```

And modify the write pointer to wrap:

```cpp
// Line 1168 - wrap around in interactive mode
int bufferCapacityFrames = args.interactive ? sampleRate * 10 : static_cast<int>(args.duration * sampleRate);
size_t writeOffset = (framesRendered % bufferCapacityFrames) * channels;
float* writePtr = audioBuffer.data() + writeOffset;
```

### Solution 2: Separate Buffer Allocation for Interactive Mode

```cpp
// Line 882-886
const int bufferFrames = args.interactive
    ? sampleRate * 10  // 10-second circular buffer for interactive
    : static_cast<int>(args.duration * sampleRate);  // Exact size for non-interactive
```

---

## Verification Steps

### Test Case 1: Reproduce the Bug (Manual)

```bash
# Run interactive mode - wait 5+ seconds before quitting
./engine-sim-cli --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --play
```

**Expected behavior (BUGGY)**:
- First 3 seconds: Audio plays smoothly
- After 3 seconds: Audio dropouts begin
- After 5+ seconds: Significant audio corruption

### Test Case 2: Verify with AddressSanitizer

```bash
# Build with AddressSanitizer
g++ -fsanitize=address -g -o engine-sim-cli src/engine_sim_cli.cpp -Lengine-sim-bridge/build -lenginesim_bridge -lOpenAL -framework CoreFoundation -framework AudioToolbox

# Run interactive mode
./engine-sim-cli --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --play
```

**Expected output (BUGGY)**:
```
==12345==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x...
WRITE of size 1600 at 0x... thread T0
    #0 0x... in engine_sim_cli.cpp:1168
    ...
0x... is located 0 bytes to the right of 288000-byte region [0x...,0x...)
allocated by thread T0 here:
    #0 0x... in operator new[]
    #1 0x... in engine_sim_cli.cpp:886
```

### Test Case 3: Non-Interactive Mode (Should Work)

```bash
# Run non-interactive with 10 second duration
./engine-sim-cli --script engine-sim-bridge/engine-sim/assets/main.mr --duration 10 --play
```

**Expected behavior (WORKS)**:
- Audio plays smoothly for all 10 seconds
- No dropouts
- No memory corruption

### Test Case 4: Verify Fix

After implementing the fix (see "The Fix" section):

```bash
# Run interactive mode for extended period
./engine-sim-cli --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --play
```

**Expected behavior (FIXED)**:
- Run for 10+ minutes
- No audio dropouts
- No AddressSanitizer errors

---

## Additional Notes

### Chunk Size

The chunk size (line 636) is 1 second:
```cpp
const int chunkSize = sampleRate;  // Queue in 1-second chunks
```

This is reasonable and not the cause of the issue.

### OpenAL Buffer Management

The OpenAL buffer management (lines 223-315) uses 2 buffers and handles queueing correctly. The issue is not with OpenAL but with the source data buffer.

### Non-Interactive Fallback Path (Lines 1232-1255)

This path has the same buffer overflow issue:
```cpp
else if (audioPlayer && !args.outputWav) {
    if (framesRendered >= chunkSize) {
        // ...
        if (!audioPlayer->playBuffer(audioBuffer.data() + chunkOffset * channels,
                                    chunkSize, sampleRate)) {
```

The `chunkOffset` calculation will also overflow the buffer after 3 seconds.

---

## Conclusion

**The dropouts in `--interactive --play` mode are caused by a buffer overflow.**

The `audioBuffer` is allocated with a fixed 3-second capacity, but the interactive loop runs indefinitely. After 3 seconds, the code writes beyond the buffer bounds, corrupting memory and causing audio dropouts.

**WAV export works correctly** because the buffer size matches the duration exactly, and the loop terminates before overflow can occur.

**Fix required**: Use a circular buffer or allocate a larger buffer for interactive mode.
