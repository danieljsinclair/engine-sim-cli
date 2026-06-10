# Audio Investigation Documentation

**Last Updated:** 2025-02-03
**Status:** Investigation in progress - comprehensive documentation in place

## Documentation Overview

This investigation has been thoroughly documented to prevent repeating mistakes and to provide a complete evidence trail. All documentation follows the principle: **NO SPECULATION - ONLY EVIDENCE**.

## Quick Start (Read in This Order)

### 1. Start Here: DOCUMENTATION_INDEX.md (5 minutes)
- Map of all documentation
- How to use the documentation
- Quick reference guide
- Glossary of terms

### 2. Current State: AUDIO_INVESTIGATION_HANDOVER.md (10 minutes)
- Executive summary
- What works/doesn't work
- Architecture overview
- Testing commands
- Next steps

### 3. Complete History: AUDIO_THEORIES_TRACKING.md (30 minutes)
- Chronological record of all theories tested
- Evidence collected for each theory
- Conclusions (DISPROVEN, CONFIRMED, INCONCLUSIVE)
- **READ THIS BEFORE PROPOSING ANY NEW THEORY**

## Documentation Structure

```
Documentation/
├── DOCUMENTATION_INDEX.md (START HERE)
│   └── Map of all documentation + how to use it
│
├── AUDIO_INVESTIGATION_HANDOVER.md
│   ├── Executive summary
│   ├── Current state
│   ├── Architecture overview
│   ├── Testing commands
│   └── Next steps
│
├── AUDIO_THEORIES_TRACKING.md (PRIMARY DOCUMENT)
│   ├── PHASE 1: Position tracking (DISPROVEN)
│   ├── Complete investigation history
│   ├── Evidence for every theory tested
│   ├── Lessons learned
│   └── Current implementation details
│
├── AUDIO_DIAGNOSTIC_REPORT.md
│   ├── 100ms delay analysis
│   ├── Periodic crackling analysis
│   ├── Audio thread throttle analysis
│   └── Root cause analysis
│
└── AUDIO_FIX_IMPLEMENTATION_GUIDE.md
    ├── Fix 1: Reduce audio thread burst size (PRIORITY: HIGH)
    ├── Fix 2: Reduce throttle smoothing (PRIORITY: HIGH)
    ├── Fix 3: Reduce target latency (PRIORITY: MEDIUM)
    ├── Testing procedures
    └── Expected results
```

## Key Findings Summary

### What We Know (Verified Facts)
- ✅ Position tracking is accurate (hardware confirms)
- ✅ Update rate is 60Hz (matches GUI)
- ✅ Buffer architecture matches GUI
- ✅ AudioUnit is correct choice for macOS
- ✅ Underruns only at startup (expected)

### What We've Ruled Out (DISPROVEN Theories)
- ❌ Position tracking errors
- ❌ Update rate differences
- ❌ Audio library choice
- ❌ Double buffer consumption
- ❌ Underruns as primary cause

### What We Don't Know (Open Questions)
- ❓ Exact source of synthesizer output discontinuities
- ❓ Why GUI doesn't exhibit crackles
- ❓ Optimal buffer lead target for pull model
- ❓ Root cause of periodic crackling

## Current Issues

1. **Periodic crackling** in audio output
2. **~100ms latency** between parameter changes and audio
3. **Startup underruns** (2-3 during first second)

## How to Contribute

### Testing a New Theory

1. **Check AUDIO_THEORIES_TRACKING.md first**
   - Search for similar theories
   - Read evidence from previous tests
   - Don't repeat disproven theories

2. **Add your theory to AUDIO_THEORIES_TRACKING.md:**
   ```markdown
   ## Theory: [Name]

   **Status:** HYPOTHESIS
   **Date:** 2025-02-03

   ### Hypothesis
   [Description]

   ### Test
   [How you'll test it]

   ### Expected Result
   [What you expect]
   ```

3. **Implement test with diagnostics**

4. **Update with results:**
   ```markdown
   ### Evidence Collected
   [Diagnostic output, measurements]

   ### Conclusion
   **Status:** DISPROVEN / CONFIRMED / INCONCLUSIVE
   ```

### Implementing Proposed Fixes

See `AUDIO_FIX_IMPLEMENTATION_GUIDE.md` for:
- Step-by-step instructions
- Code locations and changes
- Testing procedures
- Expected results

## Documentation Statistics

| File | Lines | Size | Purpose |
|------|-------|------|---------|
| DOCUMENTATION_INDEX.md | 338 | 10K | Navigation and quick reference |
| AUDIO_INVESTIGATION_HANDOVER.md | 296 | 11K | Current state summary |
| AUDIO_THEORIES_TRACKING.md | 1307 | 47K | Complete investigation history |
| AUDIO_DIAGNOSTIC_REPORT.md | 371 | 11K | Deep analysis of issues |
| AUDIO_FIX_IMPLEMENTATION_GUIDE.md | 283 | 6.8K | Implementation instructions |
| **TOTAL** | **2595** | **85.8K** | Comprehensive documentation |

## Critical Principles

### DO
- ✅ Update documentation after EVERY test
- ✅ Use diagnostics to gather evidence
- ✅ Document successful AND failed theories
- ✅ Include code snippets and diagnostic output
- ✅ Be honest about inconclusive results

### DON'T
- ❌ Speculate without evidence
- ❌ Skip updating documentation
- ❌ Repeat disproven theories
- ❌ Make assumptions without verification
- ❌ Delete or obscure failed theories

## Testing Commands

### Build
```bash
cd /Users/danielsinclair/vscode/engine-sim-cli
make clean
make
```

### Sine Mode Test
```bash
./build/engine-sim-cli --sine --rpm 2000 --play
```

### Engine Mode Test
```bash
./build/engine-sim-cli --default-engine --rpm 2000 --play
```

## Key Files

### Implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`
  - Lines 71-95: AudioUnitContext structure
  - Lines 192-205: Circular buffer initialization
  - Lines 320-420: Write discontinuity detection
  - Lines 400-750: AudioUnit callback with diagnostics
  - Lines 1100-1200: Main loop audio generation

### Reference
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
  - GUI reference implementation (works perfectly)

## Success Criteria

The investigation succeeds when:
1. Root cause of crackling is identified with evidence
2. Fix is implemented and verified
3. Latency is reduced (<50ms target)
4. Solution is documented and replicable

## Remember

**Failed theories are progress.** They rule out possibilities and narrow the search. Document them thoroughly.

**Evidence over speculation.** Every theory must be tested with diagnostics before concluding.

**Update frequently.** The tracking document is only valuable if it's current.

---

**Start reading:** DOCUMENTATION_INDEX.md

**Next step:** Read AUDIO_INVESTIGATION_HANDOVER.md for current state
