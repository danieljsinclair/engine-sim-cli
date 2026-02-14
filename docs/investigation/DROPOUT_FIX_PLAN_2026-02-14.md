# Dropout Fix Plan - Comprehensive Architecture Review

**Date:** 2026-02-14
**Author:** Architecture Planning Team
**Status:** PROPOSED - Awaiting user approval

## Executive Summary

Audio dropouts in the engine-sim CLI have been investigated across multiple sessions. This document synthesizes all findings and proposes a phased fix plan that addresses both the immediate blocking bug (engine mode deadlock) and the architectural issues that led to it.

## Current State of Affairs

### What Works
- **Sine mode (`--sine`)**: Fully functional, no dropouts, smooth real-time playback
- **CLI-side audio pipeline**: Circular buffer + AudioUnit callback is proven reliable

### What Does NOT Work
- **Engine mode (real synthesizer)**: Deadlocks immediately on startup
- **WAV export in engine mode**: Deadlocks (calls `renderAudio()` which has `cv0.wait()`)
- **The reported "10% throttle dropouts" cannot be reproduced** because engine mode never runs

### Root Causes Identified

**Bug #1: Buffer Pre-fill Deadlock (ALREADY FIXED in synthesizer.cpp)**
- `synthesizer.cpp:82-87` now pre-fills 2000 samples instead of 96000
- This fix was applied in a previous session but needs verification

**Bug #2: Missing `renderAudioSync()` for WAV Mode (NOT FIXED)**
- Real Synthesizer lacks `renderAudioSync()` method
- `EngineSimRender()` calls `renderAudio()` which uses `cv0.wait()`
- In WAV-only mode, no audio thread exists to wake the wait
- Result: deadlock when exporting to WAV without `--play`

**Bug #3: Architectural DRY Violation (ROOT CAUSE of recurring bugs)**
- Mock and real synthesizers are completely separate implementations
- Bug fixes applied to one are NOT propagated to the other
- The pre-fill fix was applied to mock months before real was fixed
- This WILL cause future bugs

## Research Findings

### GUI Analysis (Task #1)

The engine-sim GUI avoids dropouts through:

1. **Non-blocking consumer**: `readAudioOutput()` returns silence on underrun, never blocks
2. **Throttled producer**: Audio thread waits when `m_audioBuffer.size() >= 2000`
3. **Circular buffer**: `RingBuffer<int16_t>` with 44100-sample capacity (1 second)
4. **Single consumer**: Only one thread reads the audio buffer (the render callback)

The GUI does NOT use a "larger buffer" - it uses the same size buffer with better flow control.

### Historical Fixes Review (Task #2)

| Fix | Date | Issue | Root Cause |
|-----|------|-------|-----------|
| Double-thread read fix | Phase 3 | Crackling in `--play` | Main loop AND AudioUnit both reading synthesizer |
| 60Hz timing fix | 2026-02-10 | Main loop at 90kHz | No sleep in main loop |
| Fractional resampling | 2026-02-10 | writeInput() undersampling | 1 sample/step instead of ~4.41 |
| Pre-fill reduction (mock) | 2026-02-10 | Mock deadlock | 96000 pre-fill vs 2000 target |
| Pre-fill reduction (real) | 2026-02-13 | Real deadlock | Same bug, unfixed in real |
| Interactive buffer overflow | Phase 3 | Dropouts after 3s | Fixed-size WAV buffer used in interactive mode |
| Latency reduction | 2026-02-14 | 2.7s latency | Pre-fill reduced from 180 to 40 iterations |

**Pattern**: Most bugs stem from the mock/real divergence or from the CLI doing things differently from the GUI.

### Buffer Architecture Comparison (Task #3)

**Shared CLI-side pipeline (identical for sine and engine):**
```
Synthesizer.readAudioOutput() [int16 mono]
    -> EngineSimReadAudioBuffer() [float32 stereo conversion]
        -> AudioPlayer.addToCircularBuffer() [96000-sample circular buffer]
            -> AudioUnit callback reads from circular buffer
```

**Divergent library-side synthesis:**
- Mock: `MockSynthesizer` with `MockRingBuffer<T>`, own threading
- Real: `Synthesizer` with `RingBuffer<T>`, own threading
- Both follow the same cv0.wait() pattern but are separate code

## Proposed Fix Plan

### Phase 1: Unblock Engine Mode (Immediate)

**Goal:** Get engine mode running so we can actually test for dropouts.

**1a. Verify pre-fill fix in synthesizer.cpp**
- File: `engine-sim-bridge/engine-sim/src/synthesizer.cpp:82-87`
- Status: Code shows the fix IS applied (uses `std::min(2000, m_audioBufferSize)`)
- Action: Build with `USE_MOCK_ENGINE_SIM=OFF` and test playback

**1b. Add `renderAudioSync()` to real Synthesizer**
- File: `engine-sim-bridge/engine-sim/include/synthesizer.h` - add declaration
- File: `engine-sim-bridge/engine-sim/src/synthesizer.cpp` - add implementation
- Implementation adapts mock's `renderAudioSync()` for multi-channel:
```cpp
void Synthesizer::renderAudioSync() {
    std::lock_guard<std::mutex> lock(m_lock0);

    const int available = m_inputChannels[0].data.size();
    if (available <= 0) return;

    const int n = std::min(available, m_inputBufferSize);

    for (int i = 0; i < m_inputChannelCount; ++i) {
        m_inputChannels[i].data.readAndRemove(n, m_inputChannels[i].transferBuffer);
    }

    for (int i = 0; i < n; ++i) {
        m_audioBuffer.write(renderAudio(i));
    }

    m_processed = true;
}
```

**1c. Update bridge to use `renderAudioSync()` in WAV mode**
- File: `engine-sim-bridge/src/engine_sim_bridge.cpp:527`
- Change: `ctx->simulator->synthesizer().renderAudio()` to `renderAudioSync()`

**Estimated risk:** Low. Changes are isolated to the real synthesizer and match proven mock behavior.

### Phase 2: Test and Fix Real Dropouts (After Phase 1)

**Goal:** Once engine mode runs, reproduce and fix any actual dropouts.

**2a. Test engine mode playback**
```bash
./engine-sim-cli --default-engine --rpm 2000 --play --duration 10
./engine-sim-cli --default-engine --load 10 --play --duration 20
```

**2b. Test engine mode WAV export**
```bash
./engine-sim-cli --default-engine --rpm 2000 --duration 5 --output /tmp/test_engine.wav
```

**2c. Analyze with crackle detection**
```bash
python3 tools/analyze_crackles.py /tmp/test_engine.wav
```

**2d. If dropouts exist, instrument the pipeline**
- Add buffer level logging at synthesizer output
- Add buffer level logging at circular buffer input/output
- Compare timing between audio production and consumption rates

### Phase 3: Architecture Consolidation (Optional, DRY Fix)

**Goal:** Eliminate the divergent mock/real synthesizer implementations.

**Approach:** This is the most impactful but highest-risk change. Two options:

**Option A: Extract shared interface (recommended)**
- Define `ISynthesizerAudio` interface with: `readAudioOutput()`, `writeInput()`, `endInputBlock()`, `startAudioRenderingThread()`, `endAudioRenderingThread()`, `renderAudioSync()`
- Both mock and real implement this interface
- CLI code only depends on the interface
- This does NOT require changing the internal threading/buffer code

**Option B: Share implementation code**
- Extract the common cv0.wait() pattern, ring buffer, and threading into a shared base class
- Mock overrides only `renderAudio(int)` (sine wave vs convolution)
- Higher risk: changes to engine-sim submodule internals

**Recommendation:** Option A is safer and achieves the DRY goal at the API boundary. Option B is ideal but touches engine-sim internals more deeply.

## Decision Points for User

1. **Phase 1 only?** Fix the blocking bugs, test engine mode, see if dropouts exist. Minimal changes.

2. **Phase 1 + Phase 2?** Fix blocking bugs, then systematically investigate any remaining dropouts.

3. **Phase 1 + Phase 2 + Phase 3?** Full architectural consolidation. Prevents future divergence bugs but requires more work.

## Risk Assessment

| Phase | Risk | Mitigation |
|-------|------|-----------|
| 1a (verify pre-fill) | Very low | Already applied, just needs testing |
| 1b (renderAudioSync) | Low | Proven pattern from mock, isolated change |
| 1c (bridge update) | Low | Single line change, clear semantics |
| 2 (test/fix) | Medium | Unknown dropouts may have complex causes |
| 3A (interface) | Medium | Requires refactoring both implementations |
| 3B (shared impl) | High | Changes engine-sim submodule internals |

## Files That Need Changes

### Phase 1
- `engine-sim-bridge/engine-sim/include/synthesizer.h` (add `renderAudioSync()` declaration)
- `engine-sim-bridge/engine-sim/src/synthesizer.cpp` (add `renderAudioSync()` implementation)
- `engine-sim-bridge/src/engine_sim_bridge.cpp:527` (use `renderAudioSync()` in `EngineSimRender`)

### Phase 2
- Potentially `src/engine_sim_cli.cpp` if buffer management changes needed
- Potentially `engine-sim-bridge/src/engine_sim_bridge.cpp` if conversion issues found

### Phase 3 (if approved)
- New file: `engine-sim-bridge/include/i_synthesizer_audio.h` (interface)
- `engine-sim-bridge/src/mock_engine_sim.cpp` (implement interface)
- `engine-sim-bridge/src/engine_sim_bridge.cpp` (implement interface)
- `src/engine_sim_cli.cpp` (use interface if needed)

## Test Plan

### Phase 1 Verification
- [ ] Build with `USE_MOCK_ENGINE_SIM=OFF`: `cmake .. -DUSE_MOCK_ENGINE_SIM=OFF && make`
- [ ] Engine mode playback does not deadlock: `./engine-sim-cli --default-engine --play --duration 5`
- [ ] Engine mode WAV export does not deadlock: `./engine-sim-cli --default-engine --duration 5 --output /tmp/test.wav`
- [ ] Sine mode still works: `./engine-sim-cli --sine --play --duration 5`
- [ ] WAV file is non-empty and plays in external player

### Phase 2 Verification
- [ ] No dropouts in 60-second playback at various RPMs
- [ ] `analyze_crackles.py` reports clean audio
- [ ] Interactive mode runs for 5+ minutes without issues
- [ ] Buffer diagnostics show stable levels (no sustained underruns)

## Conclusion

The immediate priority is Phase 1 - unblocking engine mode. The pre-fill fix appears to already be applied (synthesizer.cpp:82-87), so the main remaining work is adding `renderAudioSync()` for WAV export mode and verifying engine mode actually runs. Once engine mode is functional, Phase 2 testing will determine if dropout issues actually exist or if they were a symptom of the deadlock.
