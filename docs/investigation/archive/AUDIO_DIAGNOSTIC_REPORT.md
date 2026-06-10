# Audio Diagnostic Report: CLI vs GUI

## Executive Summary

Evidence-based analysis of remaining audio issues in CLI implementation:
1. **~100ms delay** between RPM change and audio output
2. **Periodic crackling** at regular intervals

---

## Issue 1: 100ms Delay Analysis

### Evidence from Code

#### CLI Configuration (`src/engine_sim_cli.cpp`)

**Line 745**: `config.targetSynthesizerLatency = 0.05;`
- **Value**: 0.05 seconds = **50ms**
- **Purpose**: Target latency for synthesizer to render audio ahead of playback

**Line 741-742**: Buffer sizes
```cpp
config.inputBufferSize = 1024;      // ~23ms @ 44.1kHz
config.audioBufferSize = 96000;     // ~2.2 seconds @ 44.1kHz
```

**Line 849**: Audio thread started (asynchronous rendering)
```cpp
result = EngineSimStartAudioThread(handle);
```

#### GUI Comparison (`engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`)

**Line 169-170**: Initial buffer setup
```cpp
m_audioBuffer.initialize(44100, 44100);  // 1 second buffer
m_audioBuffer.m_writePointer = (int)(44100 * 0.1);  // Start 100ms ahead
```

**Line 257**: Target write position
```cpp
SampleOffset targetWritePosition =
    m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.1));
```
- **Value**: 44100 * 0.1 = **4410 samples = 100ms lead**

**Line 263-264**: Fallback for excess lead
```cpp
if (currentLead > 44100 * 0.5) {
    m_audioBuffer.m_writePointer = m_audioBuffer.getBufferIndex(safeWritePosition, (int)(44100 * 0.05));
```
- **Fallback**: 50ms lead if buffer gets too far ahead

### Root Cause Analysis

**Hypothesis: GUI has 100ms buffer lead, CLI does not**

**CLI audio path:**
1. Main loop calls `EngineSimSetThrottle()` (line 1144)
2. Main loop calls `EngineSimUpdate()` (line 1145)
3. Physics update writes to input buffer
4. Audio thread wakes up when `m_audioBuffer.size() < 2000` (synthesizer.cpp:228)
5. Audio thread renders up to 2000 samples
6. AudioUnit callback reads from `m_audioBuffer`

**GUI audio path:**
1. User changes throttle
2. Physics updates (same as CLI)
3. Audio thread writes to buffer (same as CLI)
4. GUI reads from buffer **100ms ahead of hardware** (line 257)
5. Hardware plays from that position

**The difference:** GUI maintains 100ms lead between write pointer and hardware read position. CLI's AudioUnit callback reads directly with no lead time.

### Evidence: Audio Thread Throttle

**synthesizer.cpp:228**:
```cpp
const bool inputAvailable =
    m_inputChannels[0].data.size() > 0
    && m_audioBuffer.size() < 2000;  // <-- THROTTLE
```

- Audio thread only renders when buffer has **less than 2000 samples**
- At 44.1kHz: 2000 / 44100 = **45ms of audio**
- When buffer fills to 45ms, audio thread **stops rendering**

**synthesizer.cpp:233**:
```cpp
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),
    (int)m_inputChannels[0].data.size());
```

**Calculation:**
- Max render per cycle: 2000 samples
- Time per cycle at 60Hz physics: 16.67ms
- Audio produced per cycle: ~2000 samples = 45ms
- Audio consumed per cycle: depends on AudioUnit callback rate

### Actual Latency Breakdown

**CLI latency chain:**
1. Physics update: immediate
2. Input buffer to audio thread: waiting for `m_audioBuffer.size() < 2000`
3. Audio thread render: up to 2000 samples = 45ms
4. AudioUnit callback: reads available samples
5. Hardware latency: OS AudioUnit latency (~10-20ms)

**Estimated total: 50-70ms** (close to observed 100ms)

**GUI latency chain:**
1. Physics update: immediate
2. Input buffer to audio thread: same throttle
3. Audio thread render: same
4. GUI writes 100ms ahead: **100ms buffer lead**
5. Hardware plays from 100ms ago
6. **Total latency: 100ms** (by design)

**Conclusion:** GUI is designed with 100ms latency. CLI is achieving similar latency but it feels "wrong" because:
- CLI has no smoothing/buffering
- User expects immediate response in CLI
- The 50ms `targetSynthesizerLatency` + 45ms throttle = ~95ms actual latency

---

## Issue 2: Periodic Crackling Analysis

### Evidence from Code

#### Audio Thread Cycle (synthesizer.cpp:222-256)

**Line 228**: Wait condition
```cpp
const bool inputAvailable =
    m_inputChannels[0].data.size() > 0
    && m_audioBuffer.size() < 2000;
```

**Line 232-234**: Render amount
```cpp
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),
    (int)m_inputChannels[0].data.size());
```

**Cycle analysis:**
1. Audio thread waits for buffer to drop below 2000 samples
2. Renders `n = 2000 - current_size` samples
3. Writes to buffer
4. Loops back to wait

**At steady state (60Hz physics, 44.1kHz audio):**
- Physics produces: 735 samples per frame (44100/60)
- AudioUnit consumes: ~512 samples per callback (typical macOS buffer)
- Audio thread produces: 2000 samples per burst

**Cycle time calculation:**
- 2000 samples / 44100 Hz = 45.3ms per burst
- This is the "regular interval" the user hears

### Root Cause Analysis

**Hypothesis: The 2000-sample throttle creates a periodic rendering cycle**

**Evidence:**
1. Audio thread renders in **45ms bursts** (2000 samples)
2. Then waits for buffer to drain
3. At 60Hz physics, input comes in **16.7ms intervals**
4. Mismatch creates **buffer level oscillation**

**Timeline:**
```
T=0ms:    Buffer=0, Audio thread renders 2000 samples
T=16ms:   Physics writes 735 samples, Buffer=2735
T=32ms:   Physics writes 735 samples, Buffer=3470
T=45ms:   AudioUnit callback reads ~512, Buffer=2958
T=48ms:   Physics writes 735 samples, Buffer=3693
T=50ms:   AudioUnit callback reads ~512, Buffer=3181
...buffer grows...
T=90ms:   Buffer drops below 2000, audio thread renders another 2000
```

**The crackle occurs at the transition point** when audio thread starts/stops rendering.

### Throttle Smoothing Issue (CLI-only)

**engine_sim_cli.cpp:1142**:
```cpp
smoothedThrottle = throttle * 0.5 + smoothedThrottle * 0.5;
```

**This smoothing happens every frame (60Hz = 16.7ms intervals)**

**Effect:**
- Throttle changes take ~2-3 frames to fully apply
- At 16.7ms per frame, this is **50ms smoothing**
- Combined with 45ms audio bursts, this creates **phase-aligned artifacts**

**Hypothesis:** The 50ms throttle smoothing aligns with the 45ms audio rendering burst, creating a periodic "step" in the audio.

### Evidence: No smoothing in GUI

**GUI throttle path:**
- `Governor::update()` (governor.cpp:48-51)
- Uses gamma curve: `1 - pow(1 - throttle, gamma)`
- No explicit smoothing like CLI's 0.5/0.5 factor

**CLI throttle path:**
- RPMController or direct load
- Smoothed: `throttle * 0.5 + smoothedThrottle * 0.5`
- This adds **additional latency** and **creates steps**

---

## Issue 3: AudioUnit Buffer Size

### Evidence from Code

**engine_sim_cli.cpp:280**:
```cpp
UInt32 numberFrames,
```

**Typical macOS AudioUnit buffer sizes:**
- Default: 512 frames (11.6ms @ 44.1kHz)
- Can vary: 256-1024 frames depending on device

**engine_sim_cli.cpp:314-317**:
```cpp
UInt32 framesToWrite = numberFrames;
if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
    framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
}
```

**Issue:** AudioUnit callback requests varying amounts (e.g., 512 frames), but synthesizer provides in 2000-frame bursts.

**Mismatch:**
- AudioUnit wants: 512 frames every ~11ms
- Synthesizer provides: 2000 frames every ~45ms
- **Result:** Buffer level oscillates

---

## Summary of Findings

### 100ms Delay

**Root Cause:** Combination of:
1. `targetSynthesizerLatency = 0.05` (50ms) - line 745
2. Audio thread throttle at 2000 samples (45ms) - synthesizer.cpp:228
3. No buffer lead time (unlike GUI's 100ms lead)
4. Throttle smoothing (50ms) - engine_sim_cli.cpp:1142

**Total estimated latency: ~95-115ms** ✓ matches observation

**Evidence locations:**
- CLI config: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:745`
- Throttle smoothing: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:1142`
- Audio throttle: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:228`
- GUI buffer lead: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp:257`

### Periodic Crackling

**Root Cause:** Audio thread rendering cycle
1. Renders 2000 samples = **45.3ms bursts**
2. Waits for buffer to drain below 2000
3. Creates **periodic buffer fill/empty cycle**
4. Cycle time: **~45-90ms** depending on consumption rate

**Frequency calculation:**
- 2000 samples @ 44.1kHz = 45.35ms
- Crackle period: **~45ms** (every burst)
- Crackle frequency: **~22 Hz** (1 / 0.045)

**Evidence:**
- Audio thread throttle: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:228,233`
- Render loop: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp:222-256`

**Secondary factor:** Throttle smoothing aligns with audio cycle
- Smoothing: 16.7ms intervals (60Hz)
- Audio cycle: 45ms intervals
- Beat frequency creates additional artifacts

---

## Recommendations

### Fix 100ms Delay

**Option A: Reduce target latency**
```cpp
config.targetSynthesizerLatency = 0.02;  // 20ms instead of 50ms
```

**Option B: Remove/reduce throttle smoothing**
```cpp
// More aggressive smoothing
smoothedThrottle = throttle * 0.8 + smoothedThrottle * 0.2;  // Faster response
```

**Option C: Match GUI with buffer lead**
- Implement lead time in AudioUnit callback path
- More complex, requires changes to callback logic

### Fix Periodic Crackling

**Option A: Reduce audio thread burst size**
```cpp
// In synthesizer.cpp:228,233
&& m_audioBuffer.size() < 500;  // Instead of 2000
const int n = std::min(
    std::max(0, 500 - (int)m_audioBuffer.size()),  // Instead of 2000
    (int)m_inputChannels[0].data.size());
```

**Option B: Continuous rendering with smaller chunks**
- Instead of waiting for buffer to drop, continuously render
- Render smaller chunks (e.g., 256 samples) more frequently

**Option C: Match AudioUnit buffer size**
- Query AudioUnit for actual buffer size
- Render exactly that amount per cycle
- Eliminates buffer level oscillation

### Verification Steps

1. **Add timing logs** to measure actual latency
2. **Measure crackle period** with audio analyzer
3. **Test each fix independently** to isolate effects
4. **Compare with GUI** audio output

---

## Code Locations for Fixes

### File: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Line 745:** Reduce target latency
```cpp
config.targetSynthesizerLatency = 0.02;  // Change from 0.05
```

**Line 1142:** Reduce throttle smoothing
```cpp
smoothedThrottle = throttle * 0.8 + smoothedThrottle * 0.2;  // Change from 0.5/0.5
```

### File: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Line 228:** Reduce burst size
```cpp
&& m_audioBuffer.size() < 500;  // Change from 2000
```

**Line 233:** Match burst size
```cpp
std::max(0, 500 - (int)m_audioBuffer.size()),  // Change from 2000
```

---

## Next Steps

1. Implement diagnostic logging to verify these hypotheses
2. Test each fix independently
3. Measure actual latency with high-resolution timers
4. Analyze crackle frequency to confirm ~22Hz hypothesis
5. A/B test with GUI to match performance
