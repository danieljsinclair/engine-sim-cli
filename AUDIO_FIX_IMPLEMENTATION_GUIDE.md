# Audio Fix Implementation Guide

## Quick Reference for Fixes

### Fix 1: Reduce Audio Thread Burst Size (PRIORITY: HIGH)

**Impact:** Reduces periodic crackling by 3x

**Files to modify:**
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Changes:**

```cpp
// Line 228: Change throttle threshold
// BEFORE:
const bool inputAvailable =
    m_inputChannels[0].data.size() > 0
    && m_audioBuffer.size() < 2000;

// AFTER:
const bool inputAvailable =
    m_inputChannels[0].data.size() > 0
    && m_audioBuffer.size() < 500;  // Reduced from 2000
```

```cpp
// Line 232-234: Change burst size
// BEFORE:
const int n = std::min(
    std::max(0, 2000 - (int)m_audioBuffer.size()),
    (int)m_inputChannels[0].data.size());

// AFTER:
const int n = std::min(
    std::max(0, 500 - (int)m_audioBuffer.size()),  // Reduced from 2000
    (int)m_inputChannels[0].data.size());
```

**Expected results:**
- Burst size: 500 samples (11.3ms) instead of 2000 samples (45ms)
- Burst frequency: ~3x more often
- Smoother audio, less crackling

---

### Fix 2: Reduce Throttle Smoothing (PRIORITY: HIGH)

**Impact:** Reduces latency by ~30ms

**Files to modify:**
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Changes:**

```cpp
// Line 1142: Change smoothing coefficients
// BEFORE:
smoothedThrottle = throttle * 0.5 + smoothedThrottle * 0.5;

// AFTER:
smoothedThrottle = throttle * 0.8 + smoothedThrottle * 0.2;
```

**Expected results:**
- 50% response time: 4ms instead of 17ms
- Total latency reduction: ~30ms
- More responsive throttle changes

---

### Fix 3: Reduce Target Latency (PRIORITY: MEDIUM)

**Impact:** Reduces baseline latency by ~30ms

**Files to modify:**
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Changes:**

```cpp
// Line 745: Change target latency
// BEFORE:
config.targetSynthesizerLatency = 0.05;

// AFTER:
config.targetSynthesizerLatency = 0.02;
```

**Expected results:**
- Target input buffer latency: 20ms instead of 50ms
- Total latency reduction: ~30ms
- May need to increase input buffer size slightly

---

## Testing Procedure

### 1. Build and Test

```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
make clean
make

# Test with default engine
./build/engine-sim-cli --default-engine --rpm 2000 --play
```

### 2. Evaluate Latency

**Manual test:**
1. Start engine at idle (~1000 RPM)
2. Quickly increase RPM to 3000
3. Count seconds until you hear the change
4. Expected: < 50ms (previously ~100ms)

**Automated test:**
```bash
# Run diagnostic tool (if implemented)
./build/audio_diagnostics
# Review throttle_latency.csv
```

### 3. Evaluate Crackling

**Manual test:**
1. Run at steady RPM (e.g., 2000)
2. Listen for periodic crackling
3. Expected: No crackling or very faint

**Record audio:**
```bash
# Record 10 seconds of audio
./build/engine-sim-cli --default-engine --rpm 2000 --play --output test.wav
# Analyze test.wav in audio editor
# Look for periodic spikes every ~90ms (should be gone)
```

### 4. Compare with GUI

```bash
# Run GUI
cd engine-sim-bridge/engine-sim
./bin/engine-sim

# Test same RPM changes
# Compare perceived latency and crackling
```

---

## Expected Results Summary

### Before Fixes

| Metric | Value |
|--------|-------|
| Throttle-to-audio latency | ~100ms |
| Periodic crackling | Every ~90ms |
| Crackle loudness | Noticeable |
| Throttle response | Sluggish |

### After Fixes (Fix 1 + Fix 2)

| Metric | Value | Improvement |
|--------|-------|-------------|
| Throttle-to-audio latency | ~50ms | 50% reduction |
| Periodic crackling | Every ~30ms | 3x less frequent |
| Crackle loudness | Barely audible | Significant reduction |
| Throttle response | Crisp | Much better |

### After All Fixes (Fix 1 + Fix 2 + Fix 3)

| Metric | Value | Improvement |
|--------|-------|-------------|
| Throttle-to-audio latency | ~30ms | 70% reduction |
| Periodic crackling | None | Eliminated |
| Crackle loudness | None | Perfect audio |
| Throttle response | Instant | Best possible |

---

## Troubleshooting

### Issue: Audio sounds distorted after reducing burst size

**Cause:** 500 samples may be too small for your CPU

**Solution:** Increase to 750 or 1000
```cpp
&& m_audioBuffer.size() < 750;  // Try 750
```

### Issue: Throttle changes are too aggressive

**Cause:** 0.8/0.2 smoothing may be too fast

**Solution:** Use intermediate value
```cpp
smoothedThrottle = throttle * 0.7 + smoothedThrottle * 0.3;  // 0.7/0.3
```

### Issue: Buffer underruns (audio glitches)

**Cause:** targetSynthesizerLatency too low

**Solution:** Increase slightly or increase input buffer size
```cpp
config.inputBufferSize = 2048;  // Double the input buffer
config.targetSynthesizerLatency = 0.03;  // 30ms (compromise)
```

---

## Verification Checklist

- [ ] Build succeeds without errors
- [ ] Engine starts and runs smoothly
- [ ] Latency is noticeably reduced
- [ ] Periodic crackling is eliminated or greatly reduced
- [ ] Throttle changes feel responsive
- [ ] No new audio artifacts introduced
- [ ] Performance is acceptable (CPU usage reasonable)
- [ ] Works with various RPM settings (idle, mid, high)
- [ ] Works with throttle ramping
- [ ] Comparable or better than GUI performance

---

## Implementation Order

**Phase 1: Quick Wins (Do First)**
1. Fix 1: Reduce burst size (synthesizer.cpp)
2. Fix 2: Reduce throttle smoothing (engine_sim_cli.cpp)

**Phase 2: Fine Tuning (If needed)**
3. Fix 3: Reduce target latency (engine_sim_cli.cpp)

**Phase 3: Advanced (If still not perfect)**
4. Implement buffer lead management (like GUI)
5. Match AudioUnit buffer size dynamically

---

## Code References

**Key files:**
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` - Main loop, throttle smoothing
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp` - Audio thread, burst rendering
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` - GUI reference

**Key lines:**
- CLI config: line 745 (targetSynthesizerLatency)
- CLI smoothing: line 1142 (throttle smoothing)
- Audio throttle: synthesizer.cpp:228 (buffer size check)
- Burst size: synthesizer.cpp:233 (samples to render)
- GUI buffer lead: engine_sim_application.cpp:257 (100ms lead)

---

## Next Steps After Fixes

1. **Document final performance**
   - Measure actual latency with diagnostic tool
   - Record before/after audio samples
   - Create comparison table

2. **Optimize further**
   - Experiment with different burst sizes
   - Tune throttle smoothing for best feel
   - Adjust target latency for your system

3. **Consider advanced fixes**
   - Implement buffer lead management
   - Match AudioUnit buffer size
   - Add dynamic burst sizing based on CPU load

4. **Share results**
   - Update documentation
   - Share findings with project
   - Consider submitting PR to main repo
