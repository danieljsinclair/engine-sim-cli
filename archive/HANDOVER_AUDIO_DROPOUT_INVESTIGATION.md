# HANDOVER: Audio Dropout Investigation - CRITICAL

## Date
2026-01-29

## Problem Summary

The CLI has MASSIVE dropouts at 15% throttle despite implementing audio thread and matching buffer sizes. The GUI works perfectly at all throttle levels. We have NOT faithfully replicated the GUI.

**User Frustration**: "we're getting these continued issues and hacking around in the code so we don't even know what changes are relevant and which are speculative hacks that likely regress us"

**Requirement**: PROVEN hypothesis only. Facts. Clean SOLID code. Follow the GUI's working example exactly.

## Current State

### What Works
- Audio thread is running
- Buffer sizes changed to 48000 (matching GUI ratio)
- Silence-filling removed (fail fast)
- Low RPM (<10%) seems better
- **15% throttle: MASSIVE dropouts** (WORSE than before)

### What Doesn't Work
- **15%+ throttle: MASSIVE dropouts** (regression)
- We keep finding differences between bridge and GUI
- Speculative hacks that don't solve the root problem

## Root Cause

We have NOT faithfully replicated the GUI. We need to find EXACTLY what's different.

---

# MISSION: Three Parallel Technical Architect Investigations

## Assign Three Technical Architects

Each TA will do THE SAME WORK INDEPENDENTLY. Then a Senior Solution Architect will collate, arbitrate, and produce a composite report.

---

# TA INVESTIGATION SCOPE (All Three TAs Do This)

## TA1: Bridge vs GUI Code Comparison

**Objective**: Find EVERY difference between bridge and GUI implementation.

**Files to Compare**:

1. **GUI Code** (reference implementation):
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h`

2. **Bridge Code** (what we modified):
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`
   - `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h`

3. **CLI Code** (our usage):
   - `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

## Investigation Questions

### 1. Audio Thread Lifecycle

**GUI** (synthesizer.cpp):
- How does `startAudioRenderingThread()` work?
- When is it called? (exact line number)
- How does `audioRenderingThread()` main loop work?
- What does `renderAudio()` do in the thread?

**Bridge**:
- Does bridge call `startAudioRenderingThread()`?
- If not, why not?
- What's different from GUI?

**CLI**:
- Does CLI call `EngineSimStartAudioThread()`?
- When? (exact line number)
- Is there a code path difference?

### 2. Audio Buffer Reading

**GUI** (engine_sim_application.cpp, line ~274):
- How does GUI read audio?
- Function called? Parameters?
- How often?

**Bridge** (engine_sim_bridge.cpp):
- What function does CLI call to read audio?
- `EngineSimRender()` vs `EngineSimReadAudioBuffer()`?
- Are they equivalent to GUI's method?

**Evidence Required**:
- Copy the EXACT code from GUI's audio reading
- Copy the EXACT code from CLI's audio reading
- Side-by-side comparison

### 3. Configuration Parameters

**GUI Configuration** (find where GUI sets these):
- `inputBufferSize`
- `audioBufferSize`
- `sampleRate`
- `simulationFrequency`
- `fluidSimulationSteps`

**CLI Configuration** (engine_sim_cli.cpp lines 560-568):
- What values does CLI set?

**Comparison Table**:
| Parameter | GUI Value | CLI Value | Source Code Reference |
|-----------|-----------|-----------|----------------------|
| inputBufferSize | ? | 48000 | file:line |
| audioBufferSize | ? | 48000 | file:line |
| sampleRate | ? | 48000 | file:line |
| etc. | | | |

### 4. Control Flow Differences

**GUI Main Loop**:
- How does GUI process each frame?
- Sequence: simulate → read audio → play audio?
- Any timing control?

**CLI Main Loop**:
- How does CLI process each frame?
- Different sequence?

### 5. What Did We Change?

Search bridge and CLI for:
- Recent git commits
- Lines we modified
- Functions we added

List EVERY change with:
- File path
- Line numbers
- What we changed
- Why we changed it (if documented)

### 6. Underrun Handling

**GUI** (synthesizer.cpp):
- Does GUI fill underruns with silence?
- Check lines 150-153 in synthesizer.cpp

**Bridge** (engine_sim_bridge.cpp):
- Did we remove silence-filling?
- Should it be there or not?

**Evidence Required**:
- Copy EXACT code from GUI showing underrun handling
- Compare to bridge code

---

## TA2: Physics and Audio Flow Investigation

**Objective**: Trace the complete data flow from engine physics to audio output.

## Investigation Questions

### 1. Engine Simulation Flow

**GUI Flow**:
1. User input → controls
2. Controls → Governor (speed control)
3. Governor → Throttle
4. Throttle → Engine
5. Engine → Exhaust flow
6. Exhaust flow → Synthesizer input
7. Synthesizer → Audio output

**Bridge/CLI Flow**:
- Same or different?

**Evidence Required**:
- Trace function calls at each step
- Check if we're using Governor or direct throttle
- Check if we're calling the right functions in the right order

### 2. Audio Synthesis Process

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp`

**Key Functions**:
- `renderAudio()` - What does it do exactly?
- What inputs does it need?
- What does it output?
- How many samples can it generate per call?

**Critical Question**: At 15% throttle, is renderAudio() being called enough times?

### 3. Input Data Flow

**File**: `/Users/danielsinclair/vsicode/engine-sim-cli/engine-sim-bridge/engine-sim/src/piston_engine_simulator.cpp`

**Questions**:
- How does exhaust flow get written to synthesizer?
- Function name? Line numbers?
- How often is it called per simulation step?
- What controls the rate?

**Evidence Required**:
- Copy the EXACT code that writes to synthesizer input
- Trace the call path

### 4. Audio Thread vs Synchronous

**GUI**:
- Uses audio thread
- How does it coordinate?
- What prevents race conditions?

**CLI**:
- Uses audio thread (supposedly)
- Are we sure it's actually running?
- How do we know?

**Evidence Required**:
- Add diagnostic output to PROVE audio thread is running
- Show thread ID, status
- Measure how often it generates audio

### 5. Throttle vs Speed Control

**GUI**:
- Uses `setSpeedControl()` → Governor
- Governor adjusts throttle automatically
- What's the exact mechanism?

**CLI**:
- What are we using? `SetThrottle()` or `SetSpeedControl()`?
- Check line 797, 941 in engine_sim_cli.cpp
- Is this different from GUI?

**Evidence Required**:
- Copy EXACT code showing what CLI calls
- Compare to GUI's exact code path

---

## TA3: Threading and Synchronization Investigation

**Objective**: Deep dive into threads, locks, condition variables, timing.

## Investigation Questions

### 1. Thread Creation and Lifecycle

**GUI**:
- When is audio thread created? (exact line)
- What's the thread entry point?
- How is it destroyed?

**CLI**:
- When is audio thread created? (exact line)
- What's the thread entry point?
- Is it actually running?

**Evidence Required**:
- Side-by-side code comparison
- Thread lifecycle comparison

### 2. Condition Variable Usage

**GUI** (synthesizer.cpp):
- `m_cv0` - when is it notified? who notifies?
- `m_cv0.wait()` - what's the wait condition?
- What's `m_processed` - when is it set/cleared?

**CLI**:
- Do we use condition variables?
- Are we using them correctly?

**Evidence Required**:
- Copy EXACT code showing CV usage
- Compare GUI vs CLI

### 3. Mutex Lock Points

**GUI**:
- `m_lock0` - what does it protect?
- `m_inputLock` - what does it protect?
- When are they acquired/released?

**CLI**:
- Do we use these mutexes?
- Any additional locks we added?

### 4. Buffer Management

**RingBuffer** (ring_buffer.h):
- How is it used in GUI?
- Is it thread-safe?
- Are there race conditions?

**CLI AudioPlayer**:
- How do we manage OpenAL buffers?
- 2 buffers - is this correct?
- Buffer queuing logic

### 5. Timing Analysis

**Frame Timing**:
- 60fps = 16.67ms per frame
- How long does simulation take?
- How long does audio rendering take?
- Are we missing real-time deadlines?

**Evidence Required**:
- Add timing measurements to CLI
- Measure simulation time per frame
- Measure audio render time per frame
- Show where we're missing deadlines

### 6. Deadlock Detection

Check for:
- Potential deadlock scenarios
- Lock ordering issues
- Thread starvation
- Priority inversion

---

# SENIOR SOLUTION ARCHITECT ROLE (Fourth Person)

## Responsibilities

1. **Collate Results**: Receive reports from all three TAs
2. **Arbitrate Discrepancies**: Where do TAs disagree? What's the truth?
3. **Identify Root Cause**: What's THE ACTUAL difference causing dropouts?
4. **Prioritize Fixes**: What's most critical? What can wait?
5. **Produce Composite Report**: Single source of truth

## Report Structure

### Section 1: Executive Summary
- 3-5 bullet points of key findings
- Root cause statement

### Section 2: Comparative Analysis
- Table format comparing GUI vs CLI
- Highlight ALL differences found

### Section 3: Root Cause
- Specific code location causing issue
- Why it causes dropouts at 15% throttle
- Evidence from all 3 TAs

### Section 4: Recommended Fix
- Specific code changes needed
- File paths and line numbers
- WHY this will work (reasoning)
- Testing strategy

### Section 5: Implementation Priority
1. What MUST be fixed first
2. What SHOULD be fixed
3. What CAN wait (refactoring)

### Section 6: Testing Strategy
- How to verify the fix works
- Test cases for all throttle levels
- Regression testing plan

---

# INSTRUCTIONS FOR NEXT SESSION

## For Each Technical Architect

1. **Read this handover document**
2. **Accept the role** (TA1, TA2, or TA3)
3. **Perform investigation** assigned to your role
4. **Write your findings** in a markdown file:
   - `/Users/danielsinclair/vscode/engine-sim-cli/TA1_FINDINGS.md`
   - `/Users/danielsinclair/vscode/engine-sim-cli/TA2_FINDINGS.md`
   - `/Users/danielsinclair/vscode/engine-sim-cli/TA3_FINDINGS.md`

## Format for TA Findings

Each TA should produce:

### Section 1: Executive Summary
- Your top 3-5 findings
- What's broken vs GUI

### Section 2: Detailed Evidence
- Code snippets (EXACT copies from files)
- File paths and line numbers
- Side-by-side comparisons

### Section 3: Root Cause Hypothesis
- What do you think is causing the issue?
- What evidence supports this?
- What evidence contradicts this?

### Section 4: Recommended Fix
- Specific changes needed
- File paths and line numbers
- Why this will work

### Section 5: Testing Strategy
- How to verify your hypothesis
- What measurements to take

## For Senior Solution Architect

1. **Read all three TA findings**
2. **Compare and contrast** - Where do they agree? Disagree?
3. **Arbitrate** - What's the ACTUAL root cause?
4. **Produce composite report**:
   - `/Users/danielsinclair/vscode/engine-sim-cli/SOLUTION_ARCHITECT_REPORT.md`

## Critical Constraints

1. **NO SPECULATION** - Only facts from code
2. **NO ASSUMPTIONS** - Verify everything in GUI code
3. **NO GUESSWORK** - If you don't know, say "need to investigate further"
4. **COPY EXACT CODE** - Show file:line:code evidence
5. **MEASURE DON'T GUESS** - Add diagnostics, test, verify
6. **GUI IS REFERENCE** - GUI works, CLI doesn't. Find the difference.

## Files to Investigate (All TAs)

### Primary Files
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` (GUI)
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/synthesizer.cpp` (GUI audio)
- `/Users/danielsinclair/vsisco/engine-sim-cli/engine-sim-bridge/engine-sim/include/synthesizer.h` (GUI audio)
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp` (Bridge)
- `/Users/danielsinclair/vsico/engine-sim-cli/engine-sim-bridge/include/engine_sim_bridge.h` (Bridge API)
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` (CLI)

### Search Patterns
```bash
# Audio thread lifecycle
grep -r "startAudioRenderingThread\|audioRenderingThread" engine-sim-bridge/engine-sim/src/ --include="*.cpp" -n

# Configuration
grep -r "inputBufferSize\|audioBufferSize\|sampleRate" engine-sim-bridge/engine-sim/ --include="*.h" -n

# Control flow
grep -r "setSpeedControl\|setThrottle" engine-sim-bridge/engine-sim/ --include="*.cpp" -n -A3 -B3

# Synchronization
grep -r "m_cv0\|m_lock0\|condition_variable" engine-sim-bridge/engine-sim/ --include="*.cpp" -n
```

## Session Setup Commands

```bash
cd /Users/danielsinclair/vscode/engine-sim-cli

# Build current state
cd build
cmake ..
make

# Test to observe issue
./engine-sim-cli --default-engine --load 15 --duration 10 --play
```

---

# SUCCESS CRITERIA

When done, we should have:

1. ✅ Three independent TA investigations (same work, different people)
2. ✅ Composite solution architect report identifying root cause
3. ✅ Specific code fix with file paths and line numbers
4. ✅ Testing strategy to verify fix works
5. ✅ No more speculation - only facts and evidence
6. ✅ Clean SOLID code that matches GUI's proven working example

---

# KEY CONTEXT FOR NEXT SESSION

## What's Been Done So Far

1. ✅ Audio thread added to CLI
2. ✅ Buffer sizes changed to 48000 (matching GUI ratio)
3. ✅ Silence-filling removed (fail fast)
4. ✅ BUT: 15% throttle still has MASSIVE dropouts (regression)

## What Changed Recently

Check git log for recent commits to see what we've modified.

## What Must Happen Next

1. Systematic comparison of GUI vs CLI
2. Find EVERY difference
3. Correct ALL differences to match GUI exactly
4. Test to verify GUI-like performance

## User's Frustration

"we're getting these continued issues and hacking around in the code so we don't even know what changes are relevant and which are speculative hacks that likely regress us"

This must end. No more hacks. Faithful replication of GUI or nothing.

---

# END OF HANDOVER

Copy and paste this entire document into a new session to continue the investigation.
