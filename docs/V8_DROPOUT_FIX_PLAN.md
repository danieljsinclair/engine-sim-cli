# V8 Engine 600 RPM Dropout Fix Plan

## Current State (v0.99%)
- ✅ Audio crackle issues resolved
- ✅ Mock mode working perfectly
- ❌ Engine mode has deadlock (real synthesizer pre-fills full 96k buffer)
- ❌ V8 engines show dropouts at 600 RPM with 600% throttle

## Critical Issues Blocking Final Release

### 1. Engine Mode Deadlock (High Priority)
**Root Cause:** Real Synthesizer pre-fills entire 96,000 sample buffer
- `synthesizer.cpp:82-84` fills buffer with zeros
- Audio thread waits for buffer size < 2000 (line 244)
- Deadlock: audio thread never runs because buffer is always full

**Fix Required:** Reduce real synthesizer pre-fill to match mock (2000 samples)
- Location: `synthesizer.cpp` around line 82-84
- Reference: `mock_engine_sim.cpp:147-150` (working implementation)

### 2. V8 Engine 600 RPM Dropout Investigation (Medium Priority)
**Symptoms:** Dropouts at 600 RPM with 600% throttle
**Possible Causes:**
- Throttle interpolation/resampling artifacts
- Engine simulation timing mismatches
- Buffer underruns during high-load conditions
- Sine wave generation precision issues

## Proposed Investigation Steps

### Phase 1: Fix Engine Mode Deadlock
1. Apply synthesizer pre-fill fix
2. Verify engine mode starts correctly
3. Compare audio output quality with mock mode
4. Confirm no new crackles introduced

### Phase 2: V8 Dropout Analysis
1. **Data Collection:**
   - Record WAV files at 600 RPM with 600% throttle
   - Use `tools/analyze_crackles.py` to detect dropouts objectively
   - Log buffer statistics during playback

2. **Root Cause Analysis:**
   - Check throttle mapping: `600% throttle → actual engine value`
   - Verify resampling consistency in `writeInput()`
   - Analyze engine state updates vs audio thread timing
   - Check for circular buffer starvation

3. **Targeted Fixes:**
   - Improve throttle interpolation algorithm
   - Adjust buffer sizes for high-load scenarios
   - Fine-tune engine simulation timing
   - Add buffer monitoring/underrun detection

## Success Criteria
- Engine mode works without deadlock
- V8 600 RPM/600% throttle plays without dropouts
- Audio quality matches mock mode
- No new crackles or timing issues introduced

## Decision Points
1. Do we need to commit to v1.0 before or after dropout fix?
2. Should we ship with engine mode disabled until deadlock is fixed?
3. Are dropouts acceptable at this specific RPM/throttle combination?

## Next Actions
1. [ ] Fix synthesizer deadlock (urgent)
2. [ ] Test engine mode functionality
3. [ ] Gather V8 dropout data
4. [ ] Analyze root cause
5. [ ] Implement targeted fix

Timeline: 1-2 days for deadlock fix, 2-3 days for dropout investigation
