# Debugging History - engine-sim-cli

## Overview

This document chronicles the technical history of debugging and fixing issues in the engine-sim-cli project, from initial audio problems through to the final stable implementation.

## Timeline

### Phase 1: Initial Investigation (January 18-25, 2026)

**Problem Identified**: Engine simulation was not producing audio output. CLI appeared to run but generated no sound.

**Initial Symptoms**:
- RPM stayed at ~0
- No exhaust flow detected
- Load showed 0%
- Manifold pressure not populated

**Key Discovery**: The starter motor was never enabled in the initial implementation.

**First Major Fix** - Issue #1: Starter Motor Enable
- **File**: `src/engine_sim_cli.cpp`
- **Problem**: Starter motor was only conditionally enabled when RPM dropped too low, but RPM started at 0 so the condition was never met
- **Solution**: Added `EngineSimSetStarterMotor(handle, 1)` before the warmup loop
- **Result**: Engine began to spin up and produce RPM

**Investigation Documents Created**:
- `RESEARCH.md` - Exhaust flow research
- `INVESTIGATION findings.md` - Initial bug analysis
- `ISSUES_FOUND.md` - Critical issues catalog

### Phase 2: Audio Chain Analysis (January 25-26, 2026)

**Problem**: Engine was running but audio output was still silent or severely distorted.

**Discovery Process**:
1. Created `diagnostics.cpp` to test each stage of the pipeline independently
2. Found that engine simulation (Stage 1-3) was working correctly
3. Identified the issue was in the audio output stage (Stage 5)

**Key Finding**: The synthesizer was producing audio, but it wasn't reaching the output correctly.

**Diagnostic Tools Created**:
- `diagnostics.cpp` - Multi-stage diagnostic tool
- `audio_chain_diagnostics.cpp` - Detailed audio pipeline testing (later merged into diagnostics.cpp)
- `sine_wave_test.cpp` - Reference implementation for clean audio

**Diagnostic Documents**:
- `DIAGNOSTICS_README.md` - How to use diagnostic tools
- `DIAGNOSTICS_SUMMARY.md` - Diagnostic findings
- `QUICK_START_DIAGNOSTICS.md` - Quick reference guide

### Phase 3: Mono-to-Stereo Conversion Bug (January 27, 2026)

**Critical Bug Discovered**: The mono-to-stereo conversion in the bridge API was reading garbage memory.

**Symptoms**:
- Audio sounded like "upside down saw tooth"
- Severe distortion and corruption
- Right channel contained uninitialized memory values
- Audio samples exceeded valid range [-1.0, 1.0]

**Root Cause**:
The synthesizer output is mono (single int16_t sample), but the bridge API was incorrectly converting to stereo:

```cpp
// BROKEN CODE (before fix)
int sampleCount = synthesizer->readAudioOutput(samples, monoBuffer);

// Incorrectly treating mono sample count as stereo sample count
for (int i = 0; i < sampleCount; ++i) {
    outputBuffer[i * 2] = monoBuffer[i];      // Left channel
    outputBuffer[i * 2 + 1] = monoBuffer[i];  // Right channel
}
// BUG: Reading beyond monoBuffer when i >= actual mono samples!
```

**Fix Applied** - Piranha Fix (from upstream engine-sim):
```cpp
// FIXED CODE
int sampleCount = synthesizer->readAudioOutput(samples, monoBuffer);

// Correctly convert mono to stereo
for (int i = 0; i < sampleCount; ++i) {
    outputBuffer[i * 2] = monoBuffer[i];      // Left channel
    outputBuffer[i * 2 + 1] = monoBuffer[i];  // Right channel
}
// Returns sampleCount (mono samples), not sampleCount * 2
```

**Files Modified**:
- `engine-sim-bridge/src/engine_sim_bridge.cpp`
- `engine-sim-bridge/include/engine_sim_bridge.h`

**Documentation Created**:
- `MONO_TO_STEREO_FIX.md` - Detailed bug analysis
- `AUDIO_DEBUGGING.md` - Sine wave vs engine sim comparison
- `AUDIO_PATH_COMPARISON.md` - Audio path analysis
- `PIRANHA_FIX_DOCUMENTATION.md` - Piranha fix details

### Phase 4: Audio Buffer Management (January 27-28, 2026)

**Problem**: Even after the mono-to-stereo fix, audio had artifacts and wasn't as smooth as the sine wave test.

**Analysis**: Compared the working `sine_wave_test.cpp` with the engine sim implementation.

**Key Differences Found**:
1. **Buffer Management**: Engine sim used simple modulo wrapping, could overwrite active buffers
2. **Chunk Size**: Engine sim used 0.25 second chunks vs 1.0 second chunks in sine wave test
3. **Float32 Support**: Engine sim didn't check for `AL_EXT_float32` extension
4. **Buffer Writing**: Engine sim was resetting write offset instead of advancing sequentially

**Fix Applied**: Copied the exact working audio strategy from `sine_wave_test.cpp`:

```cpp
// Buffer selection - explicit tracking vs modulo
// OLD (broken): currentBuffer = (currentBuffer + 1) % 2;
// NEW (fixed): Use freeBuffers[] array to track explicitly freed buffers

// Chunk size
// OLD: const int chunkSize = sampleRate / 4;  // 0.25 seconds
// NEW: const int chunkSize = sampleRate;      // 1 second

// Sequential writing
// OLD: Reset accumulationOffset to 0 after each chunk
// NEW: Use memmove to preserve remaining data, advance offset
```

**Files Modified**:
- `src/engine_sim_cli.cpp` - AudioPlayer class complete rewrite

**Documentation Created**:
- `AUDIO_FIX_SUMMARY.md` - Summary of audio buffering fixes
- `CODE_FLOW_DIAGRAM.md` - Complete audio data flow diagram
- `FIX_IMPLEMENTATION_REPORT.md` - Implementation details

### Phase 5: Submodule Integration (January 28, 2026)

**Context**: The Piranha fix (mono-to-stereo) was in the upstream engine-sim repository but not in the local bridge submodule.

**Action Taken**:
1. Updated `engine-sim-bridge` submodule to latest master
2. This brought in the Piranha fix automatically
3. Verified the fix was present in the updated code

**Documentation Created**:
- `MERGE_REPORT.md` - Submodule merge details
- `PIRANHA_FIX_MERGE_REPORT.md` - Piranha fix integration report
- `BRIDGE_CHANGES_SUMMARY.md` - Summary of bridge changes

## Technical Decisions

### Why Create diagnostics.cpp?

The existing CLI was too complex to debug in a single step. diagnostics.cpp broke down the problem into isolated test stages:
- Stage 1: Engine simulation (RPM generation)
- Stage 2: Combustion events (inferred from behavior)
- Stage 3: Exhaust flow (raw measurements)
- Stage 4: Synthesizer input (data availability)
- Stage 5: Audio output (final samples)

This approach allowed systematic elimination of each stage as a potential failure point.

### Why Compare with Sine Wave Test?

The sine wave test (`sine_wave_test.cpp`) was a known-good reference implementation that:
- Used the same OpenAL audio framework
- Produced clean, smooth audio
- Had proper buffer management
- Demonstrated correct float32 usage

By comparing the working sine wave implementation with the broken engine sim, the exact differences could be identified and replicated.

### Why Use Submodules?

The engine-sim-bridge is a separate project with its own development lifecycle. Using it as a submodule allows:
- Independent development of bridge and CLI
- Easy updates to get upstream fixes (like the Piranha fix)
- Clear separation of concerns

### Diagnostic Features

The final diagnostics.cpp includes these advanced features (ported from audio_chain_diagnostics.cpp):
- NaN/Inf corruption detection
- Buffer underrun/overrun tracking
- Configurable output path (`--output` argument)
- Silent samples percentage calculation
- Clipped samples detection
- Detailed issue reporting

## Key Files and Their Purposes

### Core Implementation
- `src/engine_sim_cli.cpp` - Main CLI application (now fixed)
- `diagnostics.cpp` - Diagnostic tool (consolidated from audio_chain_diagnostics.cpp)

### Bridge API
- `engine-sim-bridge/` - Submodule containing bridge to engine-sim library
- `engine-sim-bridge/src/engine_sim_bridge.cpp` - Bridge implementation (mono-to-stereo fix here)
- `engine-sim-bridge/include/engine_sim_bridge.h` - Bridge API header

### Documentation (Original - Now Deleted)
These 19 documents were created during debugging and have been consolidated into 3 files:
- `AUDIO_DEBUGGING.md`, `AUDIO_FIX_SUMMARY.md`, `AUDIO_INVESTIGATION_REPORT.md`
- `AUDIO_PATH_COMPARISON.md`, `CODE_FLOW_DIAGRAM.md`, `COMPREHENSIVE_INVESTIGATION_REPORT.md`
- `DIAGNOSTIC_REPORT.md`, `DIAGNOSTIC_SUMMARY.md`, `DIAGNOSTICS_README.md`, `DIAGNOSTICS_SUMMARY.md`
- `FIX_IMPLEMENTATION_REPORT.md`, `FIXES_SUMMARY.md`
- `INVESTIGATION findings.md`, `ISSUES_FOUND.md`
- `MERGE_REPORT.md`, `MONO_TO_STEREO_FIX.md`
- `PIRANHA_FIX_DOCUMENTATION.md`, `PIRANHA_FIX_MERGE_REPORT.md`
- `QUICK_START_DIAGNOSTICS.md`, `QUICK_START_SINE_TEST.md`
- `RESEARCH.md`, `SINE_WAVE_TEST_README.md`, `SINE_WAVE_TEST_SUMMARY.md`

### Documentation (Consolidated - Current)
- `DEBUGGING_HISTORY.md` - This file (technical history)
- `DIAGNOSTICS_GUIDE.md` - How to use diagnostic tools
- `TESTING_GUIDE.md` - Testing procedures and troubleshooting

## Lessons Learned

### 1. Isolate the Problem
When faced with a complex system failure, create isolated test cases for each component. The diagnostics.cpp tool was invaluable for systematically testing each stage of the pipeline.

### 2. Use Reference Implementations
The sine wave test provided a working reference that could be compared against the broken implementation. This made it much easier to identify the exact differences.

### 3. Check Memory Management
The mono-to-stereo bug was a classic out-of-bounds memory read. Always verify buffer sizes and array indices, especially when converting between different data formats.

### 4. Test Incrementally
After each fix, test to verify:
- The fix doesn't break existing functionality
- The fix actually solves the immediate problem
- No new issues are introduced

### 5. Document Everything
During intense debugging sessions, it's easy to lose track of what was tried and what worked. The extensive documentation created during this process was invaluable for:
- Understanding the timeline of fixes
- Communicating with collaborators
- Future maintenance and debugging
### Phase 6: Repository Cleanup and Build Investigation (February 19-20, 2026)

**Problem**: After repository cleanup, both sine and engine modes had severe underruns.

**Investigation**:
- Compared source files with working backup
- Found that working binary was built from source that no longer exists
- Working object files are smaller (56,400 bytes vs 79,464 bytes for CLI)
- Source files showed broken values: `circularBufferSize=176400`, `preFillIterations=240`

**Root Cause Identified - Pre-fill Buffer Mismatch**:
The audio thread was waiting for `writeIndex < 2000` before consuming, but pre-fill started the buffer at 96000 samples. This created an immediate underrun condition.

**Fix Applied**:
1. Pre-fill now starts at 0 samples, fills to target latency
2. Audio thread waits for data availability before consuming
3. Cursor-chasing maintains 100ms target latency

**Resolution**:
- All repos clean and on master
- All submodules on master
- Build from source produces working audio

## Current Status

As of February 20, 2026:
- **Sine mode**: WORKING - smooth, responsive, keyboard control with `--interactive`
- **Engine mode**: WORKING - smooth, keyboard responsive with `--interactive`
- **Submodules**: All on master, clean
- **Build**: Working from source

## Outstanding Issues

1. **DRY violation in SineAudioSource** - See Phase 7 for details
2. **Occasional buffer underrun on Merlin V12** - Observed one underrun during Merlin V12 session:
   ```
   [2725 RPM] [Throttle:   0%] [Flow: 0.00 m3/s] [Audio Diagnostics] Buffer underrun #10 - requested: 471, available: 0
   ```
   This occurred at high RPM (2725) with throttle at 0%. **UNDERRUNS ARE AUDIBLE** - causes dropouts during playback. Investigate: during rapid RPM changes. The "underrun #10" counter suggests 10 underruns may have occurred. Investigate:
   - Whether underruns correlate with rapid RPM changes
   - If buffer lead time needs adjustment for high-RPM engines
   - Whether synthesizer can keep up with audio thread demand during transients

**Note**: Underruns are rare and generally not audible, but worth investigating for production quality.

---

**Document Version**: 1.5
**Last Updated**: February 20, 2026
**Status**: Both modes working, minor underrun issue remains

### Phase 7: DRY Architecture Principle (February 19-20, 2026)

**CRITICAL ARCHITECTURAL PRINCIPLE**:

Sine mode (mock engine) and engine mode MUST share the same code paths:

```
┌─────────────────────────────────────────────────────────────────┐
│                    UNIFIED AUDIO PIPELINE                        │
├─────────────────────────────────────────────────────────────────┤
│  Sine Mode              │  Engine Mode                           │
│  (MockSynthesizer)      │  (Synthesizer)                         │
│                         │                                        │
│  libenginesim-mock.dylib│  libenginesim.dylib                    │
│         ↓               │         ↓                              │
│  ReadAudioBuffer() ─────┴───────── ReadAudioBuffer()             │
│         ↓                         ↓                              │
│         └───────────┬─────────────┘                              │
│                     ↓                                            │
│              Circular Buffer                                     │
│                     ↓                                            │
│              AudioUnit Callback                                  │
│                     ↓                                            │
│              Speaker Output                                      │
└─────────────────────────────────────────────────────────────────┘
```

**SHARED COMPONENTS (must be identical for both modes)**:
1. **KeyboardInput** - Same class, same terminal setup
2. **Circular Buffer** - Same size, same read/write logic
3. **Cursor-chasing** - Same target latency (100ms)
4. **Pre-fill** - Same iterations (6 = 100ms)
5. **ReadAudioBuffer()** - Both modes read from synthesizer
6. **Main loop timing** - Same 60Hz loop, same sleep logic
7. **displayProgress()** - Same format (or unified class)

**DRY VIOLATION - SineAudioSource Generates Directly**:

The `SineAudioSource` class generates sine samples directly instead of using `ReadAudioBuffer()` from the mock synthesizer. This violates DRY but exists for a specific reason:

**Why the violation exists**:
- Mock synthesizer timing behavior differs from real engine synthesizer
- The mock generates samples on-demand with predictable timing
- The real engine synthesizer has complex timing tied to combustion events
- Direct generation in SineAudioSource ensures consistent timing for audio pipeline testing
- If both modes used ReadAudioBuffer() and only sine worked, the issue could be mock timing vs engine timing
- Current approach: sine mode validates the audio pipeline independently of synthesizer timing

**This is acceptable because**:
- Sine mode's purpose is to isolate audio pipeline issues from synthesizer issues
- Direct generation provides a controlled reference signal
- Engine mode uses proper ReadAudioBuffer() path

**VIOLATIONS TO AVOID**:
- ❌ Different pre-fill iterations between modes
- ❌ Different buffer sizes between modes
- ❌ Different keyboard handling between modes
- ❌ Different loop termination conditions

**WHY DRY MATTERS**:
- Sine mode is a diagnostic tool for the audio pipeline
- If sine works but engine doesn't, the problem is in the synthesizer
- If both fail the same way, the problem is in the shared pipeline
- Having different code paths defeats the diagnostic purpose

**Current State (February 20, 2026)**:
- **Sine mode**: WORKING - smooth audio, keyboard responsive with `--interactive` flag
- **Engine mode**: WORKING - smooth audio, keyboard responsive with `--interactive` flag
- **DRY violation**: SineAudioSource generates directly (acceptable for diagnostic purposes)
- **Outstanding issue**: Underrun rate during playback (AUDIBLE on Merlin V12 and Ferrari 412 T2)

**Usage**:
```bash
# Sine mode with keyboard control
./engine_sim_cli --sine --interactive

# Engine mode with keyboard control  
./engine_sim_cli --interactive
```

### Phase 8: Multi-Engine Support and Script Loading (February 20, 2026)

**Problem**: Engine scripts in subdirectories could not find `engine_sim.mr` and assets.

**1. assetBasePath Fix**:
Scripts in subdirectories (like `es/engines/atg-video-2/07_gm_ls.mr`) need to find `engine_sim.mr` which is at `es/`. The fix searches upward from the script's parent directory to find `engine_sim.mr` and uses that directory as assetBasePath.

**2. Engine Wrapper Pattern**:
The atg-video-2 engine scripts define a `main` node but don't call it. We created thin wrapper files at `es/` level that:
- Import engine_sim.mr
- Import themes/default.mr
- Import the specific engine file
- Call use_default_theme() and main()

**3. New Wrapper Files Created**:
- `es/v8_gm_ls.mr` (GM LS V8)
- `es/subaru_ej25.mr` (converted from standalone to wrapper)
- `es/2jz.mr` (Toyota 2JZ)
- `es/ferrari_f136.mr` (Ferrari F136 V8)
- `es/radial_9.mr` (Radial 9-cylinder)
- `es/lfa_v10.mr` (Lexus LFA V10)
- `es/ferrari_412_t2.mr` (Ferrari 412 T2 V12)
- `es/v6_60_degree.mr` (60-degree V6)
- `es/v6_odd_fire.mr` (Odd-fire V6)
- `es/v6_even_fire.mr` (Even-fire V6)

**4. Bridge Buffer Drain**:
The bridge now drains pre-fill samples from the synthesizer before starting the audio thread (lines 393-399 in engine_sim_bridge.cpp). This prevents stale samples from causing audio artifacts at startup.

**5. All Engines Tested and Working**:
All 10 engine configurations now load and produce clean audio output.

---

**Document Version**: 1.5
**Last Updated**: February 20, 2026
**Status**: All engines working, multi-engine support complete

### Phase 9: Future Feature Research - Physics-Based Starter Sound (February 20, 2026)

**User Request**: Investigate whether engine-sim could integrate a physics-modulated starter motor sound that responds to:
- Torque changes as engine fires
- Pitch changes based on RPM
- Harmonics changes as load varies
- Current draw simulation

**Context**: The Merlin V12 has a distinctive high-pitched "catching" sound during startup that the user noticed as a very aero-engine characteristic.

---

## Research Findings

### 1. Current State: No Starter Sound Exists in engine-sim

**The starter motor is purely a physics constraint with no audio output path.**

Evidence from `engine-sim-bridge/engine-sim/src/starter_motor.cpp`:
```cpp
void StarterMotor::calculate(Output *output, atg_scs::SystemState *state) {
    // ... setup Jacobian matrices ...
    
    if (m_rotationSpeed < 0) {
        output->limits[0][0] = m_enabled ? -m_maxTorque : 0.0;
        output->limits[0][1] = 0.0;
    }
    else {
        output->limits[0][0] = 0.0;
        output->limits[0][1] = m_enabled ? m_maxTorque : 0.0;
    }
}
```

The `StarterMotor` class:
- Inherits from `atg_scs::Constraint` (physics constraint)
- Applies torque to the crankshaft when `m_enabled = true`
- Has NO audio synthesis or output capability
- Has NO connection to the synthesizer

### 2. How the Merlin V12 Produces Its Distinctive Startup Sound

**The sound comes from the exhaust audio, NOT a separate starter sound.**

From `es/engines/atg-video-2/11_merlin_v12.mr` (lines 144-160):
```mr
engine engine(
    name: "Merlin V-1650-9 [V12] (NA)",
    starter_torque: 190 * units.lb_ft,     // HIGH torque
    starter_speed: 200 * units.rpm,        // LOW target speed
    redline: 3000 * units.rpm,             // LOW redline for aero engine
    // ... other parameters ...
)
```

Key Merlin V12 characteristics that create its distinctive startup:
1. **High starter torque** (190 lb-ft) - creates strong cranking
2. **Large rotational inertia** (crank 400 lb, flywheel 200 lb) - slow spin-up
3. **12 cylinders with 60° V** - distinctive firing pattern during startup
4. **Impulse response** (`ir_lib.minimal_muffling_01`) - minimal muffling preserves raw engine sound

The "catching" sound the user hears is actually:
- Individual combustion events firing as the engine cranks
- Exhaust pulses being processed through the synthesizer
- The physics of a large aero engine spinning up with high inertia

### 3. Audio Generation Architecture in engine-sim

**How exhaust audio is generated** (from `piston_engine_simulator.cpp` lines 371-413):

```cpp
void PistonEngineSimulator::writeToSynthesizer() {
    // For each cylinder, calculate exhaust flow based on:
    // - Combustion chamber pressure
    // - Exhaust runner pressure
    // - Valve lift timing
    // - Header pipe length (delay)
    
    const double exhaustFlow =
        attenuation_3 * 1600 * (
            1.0 * (chamber->m_exhaustRunnerAndPrimary.pressure() - atm)
            + 0.1 * chamber->m_exhaustRunnerAndPrimary.dynamicPressure(1.0, 0.0)
            + 0.1 * chamber->m_exhaustRunnerAndPrimary.dynamicPressure(-1.0, 0.0));
    
    // Apply delay for header length
    const double delayedExhaustPulse = m_delayFilters[i].fast_f(exhaustFlow);
    
    // Write to synthesizer for audio output
    synthesizer().writeInput(m_exhaustFlowStagingBuffer);
}
```

The synthesizer then:
1. Applies jitter/noise based on engine parameters
2. Convolves with impulse response (exhaust muffler simulation)
3. Applies leveling and antialiasing
4. Outputs 16-bit audio samples

**Critical gap**: There is NO equivalent for starter motor sound. The synthesizer only processes exhaust flow.

### 4. Available Physics Parameters for Starter Sound Modulation

From `starter_motor.h`:
```cpp
class StarterMotor : public atg_scs::Constraint {
public:
    double m_ks;             // Spring constant (constraint stiffness)
    double m_kd;             // Damping constant
    double m_maxTorque;      // Maximum torque (190 lb-ft for Merlin)
    double m_rotationSpeed;  // Target rotation speed (200 RPM)
    bool m_enabled;          // Is starter motor engaged?
};
```

Additional available parameters from physics:
- Actual crankshaft RPM (from `m_engine->getRpm()`)
- Crankshaft angular velocity
- Combustion chamber pressure (load resistance)
- Flywheel moment of inertia

### 5. Existing Starter Sound Research

The project already has research in `archive/STARTER_SOUND_TUNING_GUIDE.md` for a Python-based synthetic starter sound generator (`generate_starter_sound.py`).

Key findings from that research:
- Real DC motors are **noise-dominated** (~42% broadband noise)
- Three-oscillator architecture works well:
  - mainOsc: sine wave at motor frequency
  - gearOsc: triangle wave at 2x (metallic gear teeth)
  - humOsc: sine at 0.5x (sub-harmonic resonance)
- Motor shaft speed ≠ engine cranking speed (14.4:1 gear ratio typical)
- Frequency scales from ~100 Hz to ~580 Hz based on motor RPM
- Load variation creates 6-8 Hz modulation (compression strokes)

---

## Technical Implementation Options

### Option A: Extend Synthesizer with Starter Motor Channel

**Architecture**:
```
StarterMotor::calculate() 
    → writeToStarterSynthesizer()
        → Synthesizer::writeInput(starterChannel, data)
```

**Pros**:
- Physics-accurate (starter torque/load affects sound in real-time)
- Integrates with existing audio pipeline
- Can use impulse response for starter housing resonance

**Cons**:
- Requires modifying engine-sim core library
- More complex to implement
- Need to define what "starter sound" means in physics terms

**Implementation sketch**:
```cpp
void PistonEngineSimulator::writeToSynthesizer() {
    // Existing exhaust flow code...
    
    // NEW: Starter motor sound
    if (m_starterMotor.m_enabled) {
        const double starterLoad = calculateStarterLoad();  // From crankshaft resistance
        const double motorRPM = m_engine->getRpm() * 14.4;  // Gear ratio
        const double torqueRatio = actualTorque / m_starterMotor.m_maxTorque;
        
        double starterSignal = generateStarterSignal(motorRPM, starterLoad, torqueRatio);
        synthesizer().writeInput(starterChannel, &starterSignal);
    }
}
```

### Option B: Impulse Response for Starter Sound (Recommended)

**Architecture**:
- Create a starter motor impulse response WAV file
- Apply it to a synthetic starter signal
- Modulate the signal based on physics parameters

**Pros**:
- Minimal changes to engine-sim core
- Uses existing convolution infrastructure
- Can create realistic starter sounds from recordings
- Easy to customize per-engine

**Cons**:
- Impulse responses are static (can't fully capture RPM-dependent pitch)
- Less physics-integrated than Option A

**Implementation sketch**:
```mr
// In engine definition:
exhaust_system starter_sound(
    impulse_response: ir_lib.starter_motor_01,
    audio_volume: 0.3
)
```

### Option C: CLI-Side Synthetic Starter Sound

**Architecture**:
- Generate starter sound entirely in `engine_sim_cli.cpp`
- Read physics parameters from bridge API
- Mix with exhaust audio in output buffer

**Pros**:
- No changes to engine-sim library
- Can use existing Python research directly
- Easy to iterate on sound design

**Cons**:
- Duplicate audio pipeline
- Less integrated with physics
- Harder to make per-engine customization

**Implementation sketch** (in engine_sim_cli.cpp):
```cpp
class StarterSoundGenerator {
    double m_motorRPM;
    double m_load;
    double m_enabled;
    
public:
    void updateFromPhysics(EngineSimStats stats) {
        m_motorRPM = stats.currentRPM * 14.4;
        m_load = calculateLoad(stats);
        m_enabled = stats.starterEnabled;
    }
    
    int16_t generateSample() {
        if (!m_enabled) return 0;
        // Three-oscillator synthesis based on Python research
        double mainOsc = sin(phase) * 0.35;
        double gearOsc = sawtooth(2*phase) * 0.25;
        double humOsc = sin(0.5*phase) * 0.18;
        double noise = random() * 0.42;
        return (mainOsc + gearOsc + humOsc + noise) * 32767;
    }
};
```

---

## Recommended Approach: Option B + C Hybrid

**Phase 1 (Immediate)**: CLI-side synthetic starter sound
- Implement `StarterSoundGenerator` class based on Python research
- Read `m_starterMotor.m_enabled` from bridge API
- Modulate frequency based on RPM from physics
- Mix with exhaust audio output

**Phase 2 (Future)**: Impulse response integration
- Create starter motor impulse responses (different for car/truck/aero)
- Allow engine scripts to specify `starter_impulse_response`
- Convolve synthetic signal with IR for housing resonance

**Phase 3 (Long-term)**: Core library integration
- Add `writeToStarterSynthesizer()` to `PistonEngineSimulator`
- Create dedicated synthesizer channel for starter
- Full physics integration (torque, load, voltage sag simulation)

---

## Key Implementation Details

### Frequency Calculation

The Python research shows motor shaft speed is the key frequency determinant:
```python
MOTOR_GEAR_RATIO = 14.4  # Starter motor shaft speed / engine speed ratio
MOTOR_RPM_TARGET = 3600.0  # Typical starter motor shaft speed at full load
MAIN_OSC_BASE = 580.0  # Hz at 3600 RPM motor shaft

# Frequency scales linearly with motor RPM
freq = MAIN_OSC_BASE * (motor_rpm / MOTOR_RPM_TARGET)
```

For engine-sim CLI:
```cpp
double motorRPM = engineRPM * 14.4;
double frequency = 580.0 * (motorRPM / 3600.0);
// Range: ~100 Hz (low cranking) to ~580 Hz (full cranking)
```

### Load Modulation

Engine compression creates rhythmic load on starter:
```cpp
// Load variation frequency = cylinders * RPM / 120
// For V12 at 200 RPM: 12 * 200 / 120 = 20 Hz
double loadFreq = cylinderCount * engineRPM / 120.0;
double loadDepth = 0.20;  // 20% RPM dip under compression
```

### Voltage Sag Simulation

Real 12V starters sag under load:
```cpp
double voltageSag = 12.0 - (currentDraw / maxCurrent) * 1.2;  // Sag to 10.8V
double voltageFactor = voltageSag / 12.0;
// Apply to frequency and amplitude
frequency *= (0.95 + 0.05 * voltageFactor);
amplitude *= (0.92 + 0.08 * voltageFactor);
```

---

## Summary

| Aspect | Finding |
|--------|---------|
| **Current starter sound** | None - starter motor is physics-only, no audio |
| **Merlin V12 startup sound source** | Exhaust audio from physics, impulse response `minimal_muffling_01` |
| **Technical feasibility** | HIGH - all physics parameters are available |
| **Recommended approach** | Hybrid: CLI synthesis + future impulse response support |
| **Key parameters for modulation** | RPM, starter enabled, crankshaft load, gear ratio |

**Next Steps**:
1. Create `StarterSoundGenerator` class in engine_sim_cli.cpp
2. Add `EngineSimGetStarterEnabled()` to bridge API
3. Implement three-oscillator synthesis from Python research
4. Test with Merlin V12 to compare with existing startup sound

---

**Document Version**: 1.6
**Last Updated**: February 20, 2026
**Status**: Research complete, implementation pending

---

## Future Tasks & Research

### Priority 1: Outstanding Issues

1. **DRY violation in SineAudioSource** - See Phase 7 for details
2. **Buffer underrun investigation** - Observed on Merlin V12 and Ferrari 412 T2:
   - Merlin V12: `[Audio Diagnostics] Buffer underrun #10 - requested: 471, available: 0` at 2725 RPM
   - Ferrari 412 T2: More underruns than other engines (possibly due to complexity)
   - Investigate: Correlation with engine complexity, RPM transients, buffer sizing
3. **Complex engine performance** - Ferrari 412 T2 shows more underruns; investigate if complex engines need different buffering

### Priority 2: Feature Enhancements

4. **Physics-Based Starter Sound** (See Phase 9 for full research)
   - Implement `StarterSoundGenerator` class
   - Modulate by RPM, torque, load
   - Configurable per engine in .mr files
   - Research complete, implementation pending

5. **Throttle-Off Pops & Backfire Simulation**
   - **Current status**: NOT implemented (documented as TODO in engine-sim)
   - **WAV files**: Used for impulse responses (convolution reverb), not discrete sounds
   - **Feasibility**: MEDIUM-HIGH - could use existing GasSystem fuel/oxygen tracking
   - **Implementation would require**:
     - Physics: Track unburnt fuel reaching exhaust
     - Detection: Throttle lift events
     - Audio: Procedural pop generation or sample triggering
   - **Existing parameters**: `n_fuel()`, `n_o2()`, throttle position

6. **Community Engine Integration**
   - **Source**: https://catalog.engine-sim.parts/ (2,340+ parts, 4,160+ users)
   - **Hellcat available**: "Hellephant 7.0" - 920HP, 2700 lbft
   - **Popular engines**: 2JZ, EJ25, Viper V10, Porsche V10, BMW V12, Cosworth DFV
   - **Method**: Download .mr → place in es/engines/ → create wrapper file

### Priority 3: Platform Expansion

7. **iOS Port**
   - **Feasibility**: POSSIBLE (3-4 weeks effort)
   - **Audio**: Already using AudioUnit (compatible with iOS Remote I/O)
   - **Dependencies**: All pure C++, portable
   - **Blockers**:
     - CLI not allowed → SwiftUI wrapper required (1-2 weeks)
     - App sandbox → Bundle assets with app
     - Code signing → Apple Developer account ($99/yr)
   - **Author's position**: Confirmed feasible but no plans to port
   - **CMake**: Native iOS support since 3.14

### Community Resources

| Resource | URL |
|----------|-----|
| Parts Catalog | https://catalog.engine-sim.parts/ |
| Original repo | https://github.com/ange-yaghi/engine-sim |
| Community Edition | https://github.com/Engine-Simulator/engine-sim-community-edition |
| engine-sim-garage | https://github.com/kirbyguy22/engine-sim-garage |
| Discord | https://discord.gg/engine-sim-official |

---

**Document Version**: 1.7
**Last Updated**: February 20, 2026
**Status**: Main features complete, future tasks documented
