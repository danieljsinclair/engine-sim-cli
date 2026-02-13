# Audio Investigation Documentation Index

**Last Updated:** 2025-02-03
**Project:** engine-sim-cli Audio Investigation
**Status:** Investigation in progress - root cause unknown

## Quick Start

**New to the investigation?** Start here:
1. Read **AUDIO_INVESTIGATION_HANDOVER.md** (5 minutes) - Current state summary
2. Read **AUDIO_THEORIES_TRACKING.md** (15 minutes) - Complete investigation history
3. Examine the code diagnostics output (run the CLI and observe)

**Have a theory to test?**
1. Check AUDIO_THEORIES_TRACKING.md to see if it's been tried
2. If not, add it to the document with "HYPOTHESIS" status
3. Implement diagnostics to test it
4. Update document with evidence and conclusion

## Documentation Files

### Primary Documentation

#### 1. AUDIO_INVESTIGATION_HANDOVER.md
**Purpose:** Executive summary and current state
**Audience:** Anyone joining the investigation
**Contents:**
- Executive summary
- Current state (what works/doesn't work)
- Architecture overview
- Testing commands
- Next steps
- Quick reference

**When to read:** First document to read when joining investigation

#### 2. AUDIO_THEORIES_TRACKING.md
**Purpose:** Complete investigation history with evidence
**Audience:** Investigators contributing to the project
**Contents:**
- Chronological record of all theories tested
- Evidence collected for each theory
- Conclusions (DISPROVEN, CONFIRMED, INCONCLUSIVE)
- Code locations
- Lessons learned

**When to read:** Before proposing or testing any new theory

**When to update:** AFTER EVERY implementation step or test

**Critical Principle:** NO SPECULATION - only document actual evidence

### Analysis Documents

#### 3. AUDIO_DIAGNOSTIC_REPORT.md
**Purpose:** Deep analysis of CLI vs GUI differences
**Audience:** Technical investigators
**Contents:**
- 100ms delay analysis with code evidence
- Periodic crackling analysis with calculations
- Audio thread throttle analysis
- Root cause analysis with code locations
- Fix recommendations

**When to read:** When understanding specific issues (latency, crackling)

#### 4. AUDIO_FIX_IMPLEMENTATION_GUIDE.md
**Purpose:** Step-by-step fix implementation instructions
**Audience:** Implementers
**Contents:**
- Quick reference for fixes
- Code changes with line numbers
- Before/after snippets
- Testing procedures
- Expected results
- Troubleshooting

**When to read:** When implementing proposed fixes

**Status:** NOT YET IMPLEMENTED - These are proposed fixes waiting for testing

### Historical Documents

#### 5. AUDIO_CRACKLING_FIX_REPORT.md
**Purpose:** Early crackling investigation results
**Status:** Historical - superseded by later findings

#### 6. AUDIO_EVIDENCE.md
**Purpose:** Collected evidence from early testing
**Status:** Historical - see AUDIO_THEORIES_TRACKING.md for current state

#### 7. AUDIO_FIX_SUMMARY.md
**Purpose:** Summary of attempted fixes
**Status:** Historical - see AUDIO_THEORIES_TRACKING.md for complete history

## Investigation Status

### What We Know

**Verified Facts:**
- Position tracking is accurate (hardware position matches manual tracking)
- Update rate is 60Hz (matches GUI)
- Buffer architecture matches GUI (circular buffer, 44100 samples, 10% lead)
- AudioUnit is correct choice for macOS
- Underruns occur only at startup (expected without pre-fill)

**Documented Issues:**
- Periodic crackling in audio output
- ~100ms latency between parameter changes and audio output
- Startup underruns (2-3 during first second)

### What We've Ruled Out

**Theories DISPROVEN with evidence:**
- Position tracking errors (hardware feedback proves accuracy)
- Update rate differences (CLI already at 60Hz)
- Audio library choice (AudioUnit is correct)
- Double buffer consumption (fixed)
- Underruns as primary cause (crackles occur without underruns)

### What We Don't Know

**Open questions:**
- Exact source of synthesizer output discontinuities
- Why GUI doesn't exhibit crackles with same synthesizer
- Whether buffer lead target (10%) is optimal for pull model
- Root cause of periodic crackling pattern
- Why audio API difference (AudioUnit vs DirectSound) matters

## How to Use This Documentation

### For New Investigators

1. **Start with handover document**
   ```bash
   cat AUDIO_INVESTIGATION_HANDOVER.md
   ```

2. **Read tracking document**
   ```bash
   cat AUDIO_THEORIES_TRACKING.md
   ```

3. **Run the CLI to observe diagnostics**
   ```bash
   ./build/engine-sim-cli --sine --rpm 2000 --play
   ```

4. **Propose theory only after checking tracking document**
   - Search AUDIO_THEORIES_TRACKING.md for similar theories
   - If already tested, read the evidence
   - If not, add new entry with "HYPOTHESIS" status

### For Testing Theories

1. **Add entry to AUDIO_THEORIES_TRACKING.md:**
   ```markdown
   ## Theory: [Your Theory Name]

   **Status:** HYPOTHESIS
   **Date:** 2025-02-03

   ### Hypothesis
   [Brief description]

   ### Test
   [How you will test it]

   ### Expected Result
   [What you expect to find]
   ```

2. **Implement diagnostics or code changes**

3. **Run tests and collect evidence**

4. **Update AUDIO_THEORIES_TRACKING.md with results:**
   ```markdown
   ### Evidence Collected
   [Diagnostic output, measurements, observations]

   ### Conclusion
   **Status:** DISPROVEN / CONFIRMED / INCONCLUSIVE

   [What this means]
   ```

### For Implementing Fixes

1. **Read AUDIO_FIX_IMPLEMENTATION_GUIDE.md**

2. **Choose fix based on priority:**
   - PRIORITY HIGH: Reduces crackling by 3x
   - PRIORITY HIGH: Reduces latency by ~30ms
   - PRIORITY MEDIUM: Reduces baseline latency by ~30ms

3. **Follow implementation guide:**
   - Exact file locations
   - Before/after code
   - Testing procedure
   - Expected results

4. **Update AUDIO_THEORIES_TRACKING.md:**
   - Document implementation
   - Record test results
   - Note any deviations from expected results

## Key File Locations

### Source Code
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
  - Main implementation with diagnostics
  - Lines marked with "PHASE 1 DIAGNOSTICS" contain position tracking
  - Lines marked with "CRITICAL" contain key architectural elements

### GUI Reference
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
  - Working reference implementation
  - Use for comparison

### Synthesizer
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
  - Audio thread implementation
  - Throttle and burst rendering logic

## Testing Commands

### Build
```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
make clean
make
```

### Sine Mode Test (reproducible test case)
```bash
./build/engine-sim-cli --sine --rpm 2000 --play
```

### Engine Mode Test (realistic engine audio)
```bash
./build/engine-sim-cli --default-engine --rpm 2000 --play
```

## Diagnostic Output Reference

### Position Tracking Diagnostic
```
[POSITION DIAGNOSTIC #0 at 0ms] HW:0 (mod:0) Manual:0 Diff:0
```
- **Diff:0** means hardware position matches manual tracking
- Any non-zero value indicates position tracking error

### Underrun Diagnostic
```
[UNDERRUN #1 at 95ms] Req: 470, Got: 176, Avail: 176, WPtr: 4410, RPtr: 4234
```
- **Req:** Frames requested by AudioUnit
- **Got:** Frames actually available
- **Avail:** Total frames in buffer
- **WPtr:** Write pointer position
- **RPtr:** Read pointer position

### Buffer Wrap Diagnostic
```
[BUFFER WRAP #1 at 991ms] RPtr: 43748 -> 118 (Jump: 470)
```
- Read pointer wrapping around circular buffer
- Jump distance should match frames requested
- Large discontinuities here indicate problems

### Periodic Summary
```
[DIAGNOSTIC #100] Time: 1055ms, HW:46570 (mod:2470) Manual:2940 Diff:-470,
WPtr: 7700, Avail: 4760 (108.0ms), Underruns: 2, ReadDiscontinuities: 0,
Wraps: 1, PosMismatches: 0
```
- **PosMismatches: 0** is critical - means position tracking is accurate
- **Underruns:** Should be low (2-3 at startup only)
- **ReadDiscontinuities:** Should be zero
- **Wraps:** Expected periodic occurrence

## Contribution Guidelines

### DO:
- ✅ Update AUDIO_THEORIES_TRACKING.md after EVERY test
- ✅ Use diagnostics to gather evidence before concluding
- ✅ Document both successful AND failed theories
- ✅ Include code snippets showing exact changes
- ✅ Record diagnostic output as evidence
- ✅ Be honest about inconclusive results

### DON'T:
- ❌ Speculate without evidence
- ❌ Skip updating documentation
- ❌ Implement changes without testing theories first
- ❌ Repeat theories already disproven (check tracking doc!)
- ❌ Delete or obscure failed theories (they're valuable!)
- ❌ Make assumptions about GUI vs CLI (verify with code)

## Success Criteria

The investigation is successful when:
1. Root cause of crackling is identified with evidence
2. Fix is implemented and verified to eliminate crackling
3. Latency is reduced to acceptable levels (<50ms)
4. Solution is documented in AUDIO_THEORIES_TRACKING.md
5. Solution can be replicated by others

## Glossary

**AudioUnit:** Core Audio API for real-time audio on macOS (pull model)
**DirectSound:** Windows audio API used by GUI (push model)
**Pull model:** Consumer (audio hardware) requests data from producer
**Push model:** Producer writes data, consumer reads from buffer
**Buffer lead:** Amount of audio data ahead of playback position (100ms target)
**Underrun:** Buffer doesn't have enough data when audio hardware requests it
**Crackling:** Audible pops/clicks in audio output
**Latency:** Delay between parameter change and hearing the result in audio
**Position tracking:** Monitoring where audio hardware is reading from buffer
**mSampleTime:** Hardware position feedback from AudioUnit callback
**Circular buffer:** Fixed-size buffer that wraps around when full

## Contact and Contribution

This is a technical investigation. Contributions should:
- Be evidence-based
- Include diagnostic output
- Update tracking documentation
- Follow scientific method (hypothesis → test → conclusion)

**Remember:** Failed theories are progress. They rule out possibilities and narrow the search. Document them thoroughly.

---

**End of Documentation Index**

**Next step:** Read AUDIO_INVESTIGATION_HANDOVER.md for current state summary
