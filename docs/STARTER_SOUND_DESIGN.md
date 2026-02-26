# Starter Motor Sound Design - Comprehensive Session Log

## Purpose of This Document
This document captures the full conversation and decision-making process for adding starter motor sounds to engine-sim-cli. It includes verbatim user instructions, what was tried, what failed, what not to do, architectural decisions, and the current state of progress.

---

## 1. Initial Question

### User's Opening Question (verbatim):
> "does engine-sim by ange the great support starter motor sounds, or just the exhaust - ignition sounds? Could we blend a recorded starter during our CLI initialisation to sound like a starter"

### Finding:
Engine-sim generates **only exhaust and air intake sounds**. The starter motor is physically simulated (applies torque to crankshaft, has `m_enabled` flag, cranks at ~200 RPM) but produces **zero audio output**. The `writeToSynthesizer()` function in `piston_engine_simulator.cpp:371` only processes exhaust flow. The starter motor exists purely as a physics constraint.

### Key Engine-Sim Audio Components:
- **Exhaust pulses** - Pressure waves from cylinder combustion
- **Air intake breathing** - Low-frequency wind noise
- **Resonance** - Exhaust pipe modeling via impulse response convolution
- **Mechanical character** - Jitter + aliasing artifacts
- **NOT generated**: Starter motor, ignition sparks, fuel pump, intake valves

---

## 2. User's Follow-Up Instruction (verbatim):
> "or could you create a simulator that would generate starter sounds as the engine cranks? ideally, until it fires (which we could delay like on the mclaren f1)"

### Understanding:
The user wants:
1. Procedural starter motor sound generation (not just pre-recorded playback)
2. Sound continues while the engine cranks
3. Configurable ignition delay (McLaren F1 has a notable ~6 second crank before fire)
4. Sound responds to actual engine state

---

## 3. Three Approaches Identified

### Approach 1: Pre-recorded Starter Loop (Simplest - Do First)
- Take existing recordings of car startups
- Extract the starter motor segment
- Create a seamless loop
- Play at constant speed during startup
- Fade out when engine fires
- **Key limitation**: Pre-recorded WAVs already have load fluctuations baked in from the original engine

### Approach 2a: Hybrid with Recorded Electric Whine (Later)
- Record a clean, constant electric motor whine (no load fluctuations)
- Pitch-shift based on real-time RPM from engine-sim
- RPM variations come from actual physics simulation
- **Problem**: Hard to find a clean recording without existing load artifacts

### Approach 2b: Hybrid with Procedural Whine (Later - Possibly Easiest)
- Generate a pure constant electric whine procedurally
- Pitch-shift based on real-time RPM from engine-sim
- No WAV file needed, perfect loop by definition
- Easy to tune harmonics

### User's Clarification (verbatim):
> "there's actaully two options. Teh first is simply to splice a pre-recorded starter in to a loop we overlay into the sim as it warms up. the second (later) is to use the hybrid approach. the recorded wav may sound good enough. We would need a different wav for a clean starter noise, because all the pre-recorded wavs will already have fluctations based on the load fluctations of the engine they're starting. for the hybrid approach we'd need a plaing electric noise and we'd have to simulate load cycles, so it's still a way off"

### Understanding:
- Option 1 (NOW): Pre-recorded loop with natural fluctuations baked in. No pitch-shifting.
- Option 2 (LATER): Hybrid approach needing a CLEAN electric noise source. The user explicitly noted that all existing recordings have load fluctuations from the original engine, so they can't be pitch-shifted without double-applying the effect.

---

## 4. Procedural Source Material Discussion

### User's Suggestion (verbatim):
> "it might turn out that I can a) use a eletric whine from a commercial motor, or an aircraft turbine, or somethign, or perhaps as it's simpler we could generate the baseline procuedurallly?"

### Understanding:
For the hybrid approach's clean baseline, options are:
1. Commercial electric motor recording
2. Aircraft turbine recording
3. **Procedural generation** (potentially simplest)

The user suggested procedural might be simplest because it avoids sourcing, licensing, and extracting clean audio.

---

## 5. Procedural Sound Generation Attempts

### Attempt 1: Simple Sine Waves (FAILED)
**What was tried**: Basic sine wave oscillators with harmonics at a fixed frequency.

**Script**: `generate_starter_sound.py` (first version)
- Fundamental at 300 Hz + 3 harmonics
- Simple RPM ramp from 50 → 200 RPM
- 5% noise

**Result**: Sounded completely artificial and nothing like an electric motor.

**User feedback (verbatim):**
> "firstly, I want to hear it when I run the script. secondly it deosn't sound anything like an eletric motor. very artificial."

**Lessons learned**:
- Simple sine harmonics sound synthetic, not mechanical
- Script must auto-play the result (using `afplay` on macOS)
- Need much more complex harmonic structure

---

### Attempt 2: Commutator Ripple Model (PARTIALLY WORKED)
**What was tried**: Research-based DC motor acoustics with:
- Inharmonic commutator harmonics at irregular multiples (1.0x, 1.5x, 2.8x, 4.2x, 6.0x, 8.5x)
- Phase jitter for brush switching irregularity
- Multiple frequency bands (84 Hz bearing rumble, 160 Hz fundamental, 252 Hz housing, 508 Hz brush)
- 42% broadband mechanical noise (noise-dominant mix)

**Result**: The broadband noise was convincing but the harmonics still sounded artificial.

**User feedback (verbatim):**
> "there's a kind of air noise in teh background that's very convincing, but the main harmonics sound like some sort of twizzler that flies away"

**Lessons learned**:
- **42% broadband noise works well** - keep this
- Inharmonic commutator model is still too synthetic
- The tonal components need to be simpler and less "clean"
- Real motors are noise-dominated, not tone-dominated

---

### Attempt 3: Updated with Real 12V Specs (STILL TOO LOW)
**What was tried**: Added real automotive starter motor specifications:
- 12V DC system
- 250 RPM cranking speed (engine)
- 400A current draw
- Voltage sag simulation (11.5V → 10.8V)

**Result**: Sounded like a low RPM hum of a big engine, not a starter.

**User feedback (verbatim):**
> "sounds more realistic, but more like a low rpm hum of a big engine"

**Root cause**: Frequencies were scaled by ENGINE RPM (250 RPM) instead of MOTOR SHAFT RPM (~3,600 RPM). The starter motor shaft spins much faster than the engine crankshaft due to gear reduction.

**Lessons learned**:
- **Critical**: Must scale by starter motor shaft RPM, not engine RPM
- Gear ratio is approximately 14.4:1 (3,600 RPM motor / 250 RPM engine)
- This is why starter motors have a high-pitched whine, not a low rumble

---

### User's Reference Code (p5.js)
The user provided a p5.js reference implementation that sounded better:

```javascript
// User's p5.js reference (verbatim):
let mainOsc, gearOsc, humOsc;
let playing = false;

function setup() {
    createCanvas(400, 200);

    // 1. Main Motor Whine
    mainOsc = new p5.Oscillator('sine');
    // 2. Gear Teeth (Higher harmonic for 'metallic' sound)
    gearOsc = new p5.Oscillator('triangle');
    // 3. Sub-harmonic (Deep electric hum)
    humOsc = new p5.Oscillator('sine');

    mainOsc.amp(0);
    gearOsc.amp(0);
    humOsc.amp(0);
}

function mousePressed() {
    if (!playing) {
        mainOsc.start(); gearOsc.start(); humOsc.start();

        // --- THE STARTER MOTOR LOGIC ---
        let rampTime = 0.6; // Speed up time

        // Ramp Frequencies (The Whirr)
        mainOsc.freq(40, 0); mainOsc.freq(400, rampTime);
        gearOsc.freq(80.4, 0); gearOsc.freq(804, rampTime);
        humOsc.freq(20, 0); humOsc.freq(200, rampTime);

        // Ramp Volume (The Engagement)
        mainOsc.amp(0.3, 0.05); gearOsc.amp(0.1, 0.05); humOsc.amp(0.2, 0.05);

        playing = true;
    } else {
        // Shutdown sound
        mainOsc.amp(0, 0.2); gearOsc.amp(0, 0.2); humOsc.amp(0, 0.2);
        setTimeout(() => { mainOsc.stop(); gearOsc.stop(); humOsc.stop(); }, 200);
        playing = false;
    }
}

function draw() {
    background(playing ? '#2ecc71' : '#e74c3c');
    textAlign(CENTER);
    text(playing ? 'CLICK TO STOP' : 'CLICK TO START MOTOR', width / 2, height / 2);
}
```

### User's instruction about this code (verbatim):
> "still sounds like a strange whizzing whistle, something about the sine wave being super fast and unreastic. this harminic is about th eright phase; can you change ours to suit this but keep our realistic params and background noise"

### Understanding:
- The p5.js three-oscillator approach has the right **phase/character**
- Key insight: **triangle wave** for gear teeth creates metallic character
- Simple harmonic ratios: 0.5x (hum), 1.0x (main), 2.0x (gear)
- Smooth frequency ramping via oscillator objects (not per-sample sine generation)
- Keep our 42% broadband noise and 12V specs

---

### Attempt 4: p5.js-Style Three Oscillators (BETTER BUT STILL LOW)
**What was tried**: Rewrote to match p5.js architecture:
- mainOsc: sine wave (fundamental)
- gearOsc: triangle wave (2x metallic character)
- humOsc: sine wave (0.5x sub-harmonic)
- Phase accumulation for smooth sweeps
- 42% broadband noise retained

**Result**: Still sounded like engine rumble, not starter whine.

**Root cause**: Same as before - scaling by engine RPM (250) instead of motor shaft RPM (3,600).

---

### Attempt 5: Motor Shaft RPM Scaling (OPTION B - CURRENT BEST)
**What was tried**: Applied 14.4:1 gear ratio to frequency scaling:
- mainOsc: 83 → 580 Hz (sine)
- gearOsc: 166 → 1,160 Hz (triangle)
- humOsc: 41 → 290 Hz (sine)

**Result**: More realistic starter motor pitch.

**User feedback (verbatim):**
> "sounds better, but its oscillating like it's cranking an engine. is it supposed to sound like that?"

**Root cause**: The 8 Hz load pulses (simulating 4-cylinder compression) were creating pitch wobble during the pure cranking phase.

---

### User's Instruction About Load Oscillation (verbatim):
> "sounds better, but its oscillating like it's cranking an engine. is it supposed to sound like that?"

When presented with three options:
1. Remove load pulses entirely
2. Reduce to 5% depth
3. **Load pulses only happen when engine starts firing**

**User chose: Option 3**

### Understanding:
- The starter motor itself produces a **smooth, constant pitch** as it spins
- Load variations from engine compression should NOT be baked into the baseline starter sound
- In the final integration, RPM variations will come from **engine-sim's actual physics** driving the pitch in real-time
- The load oscillation is a result of engine compression, not the motor itself

---

### Attempt 6: Smooth Cranking (No Load Pulses - CURRENT STATE)
**What was tried**: Removed 8 Hz load modulation, smooth whine throughout.

**Result**: Cleaner, but still needs work.

**User feedback (verbatim):**
> "sounds better. takes too long to start up. sounds too big"

### Understanding:
- Ramp-up time (2.5 seconds) is too long
- User requested **0.5 second ramp-up**
- "Sounds too big" means amplitude/pitch range is too dramatic

---

## 6. User's Key Architectural Instructions

### Separating Starter Sound from Engine Crank (verbatim):
> "0.5s ramp up, need to simulate the engine crank separately. Probably spins fast and slow as the engine turns over do you think?"

### Understanding:
The starter motor sound and the engine crank effect are **two separate things**:
1. **Starter motor sound**: Clean 0.5s ramp to steady whine (what we generate procedurally)
2. **Engine crank variations**: Come from engine-sim's actual RPM output modulating the pitch

The starter sound generator should produce a **clean, idealized baseline**. The realism of load-induced pitch variations comes from the engine simulation driving it in real-time.

---

### Integration Architecture (verbatim):
> "how do you plan to integrate it into engine-sim, could we make it a separate add on?"

Then:
> "maybe a starter sound repo and we control it from the bridge"

Then:
> "I don't mind modifying the bridge but I'd rather not modify engine-sim"

### Understanding:
The user explicitly wants:
1. **Engine-sim (submodule)**: UNCHANGED - pure physics, no modifications
2. **Engine-sim-bridge**: CAN be modified - this is where starter sound integration happens
3. **Starter sound**: Separate module/repo, controlled by the bridge
4. Bridge reads state from engine-sim (RPM, starter enabled, combustion) and feeds it to the starter sound synthesizer
5. Bridge mixes starter audio with exhaust audio

---

## 7. What NOT To Do

### DO NOT:
1. **Use simple sine waves** for motor sound - sounds completely artificial
2. **Scale frequencies by engine RPM** - must use motor shaft RPM (14.4:1 gear ratio)
3. **Bake load pulses into the baseline sound** - these come from real-time engine-sim physics
4. **Use per-sample sine generation** without phase accumulation - causes aliasing artifacts
5. **Make the ramp-up too long** - should be ~0.5 seconds, not 2.5
6. **Modify engine-sim directly** - only modify the bridge
7. **Pitch-shift pre-recorded WAVs that already have load fluctuations** - double-applies the effect
8. **Use too many harmonics** - simple 3-oscillator model (0.5x, 1x, 2x) works better than complex inharmonic stacks
9. **Make harmonics dominate the mix** - real motors are noise-dominated (~42% broadband noise)
10. **Forget to auto-play** - script must call `afplay` so the user can hear it immediately

### DO:
1. **Use triangle waves** for gear/mechanical character (not sine)
2. **Scale by motor shaft RPM** (~3,600 RPM at full crank, not 250 RPM engine speed)
3. **Keep 42% broadband noise** - this sounds convincing
4. **Use phase accumulation** for smooth, alias-free frequency sweeps
5. **Keep the starter sound as a clean baseline** - engine physics drives variation
6. **Use 0.5s ramp-up** for solenoid engagement
7. **Detect engine fire** via `CombustionChamber::m_lit` flag
8. **Cross-fade** starter to exhaust when engine fires

---

## 8. Current State of the Generator

### File: `generate_starter_sound.py`
**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/generate_starter_sound.py`

### Current Parameters:
- Three-oscillator architecture (mainOsc sine, gearOsc triangle, humOsc sine)
- Motor shaft RPM scaling (14.4:1 gear ratio)
- mainOsc: 83 → 580 Hz, gearOsc: 166 → 1160 Hz, humOsc: 41 → 290 Hz
- 42% broadband mechanical noise
- 12V voltage sag simulation (11.5V → 10.8V)
- No load pulses during cranking (clean baseline)
- Auto-plays via `afplay`

### Still TODO:
- [ ] Shorten ramp-up to 0.5 seconds (user explicitly requested)
- [ ] Reduce "bigness" (amplitude/pitch range)
- [ ] Try Options A and C for frequency range comparison
- [ ] Create C++ `StarterSoundSynthesizer` class for bridge integration
- [ ] Design bridge API
- [ ] Create separate starter-sound repo/module

---

## 9. Frequency Scaling Options (User Asked to Try All Three)

### User instruction (verbatim):
> "try each one, start with B"

### Option A: Shift frequency range up
- 300 Hz → 1200 Hz base instead of 100 Hz
- Status: NOT YET TRIED

### Option B: Scale by actual starter motor speed (CURRENT)
- 14.4:1 gear ratio applied
- mainOsc: 83 → 580 Hz
- Status: TRIED - "sounds better" but needs faster ramp and less oscillation

### Option C: Match p5.js ranges more directly
- 40-400 Hz for mainOsc with offset
- Status: NOT YET TRIED

---

## 10. Integration Architecture (Final Design)

```
engine-sim/                          (UNCHANGED - pure physics)
├── include/
│   ├── starter_motor.h              (physics only, no audio)
│   ├── ignition_module.h            (has m_enabled flag for delay)
│   └── combustion_chamber.h         (has m_lit flag for fire detection)

engine-sim-bridge/                   (MODIFIED - orchestration layer)
├── engine-sim/                      (submodule, unchanged)
├── include/
│   ├── engine_sim_bridge.h          (add EngineSimSetIgnitionDelay API)
│   └── starter_sound_synthesizer.h  (NEW - procedural sound class)
├── src/
│   ├── engine_sim_bridge.cpp        (add ignition delay, starter sound mixing)
│   └── starter_sound_synthesizer.cpp (NEW - three-oscillator + noise)

engine-sim-cli/
├── src/
│   └── engine_sim_cli.cpp           (add --ignition-delay flag)
```

### Data Flow:
1. Engine-sim computes: starter enabled, crankshaft RPM, combustion state
2. Bridge reads these values from engine-sim (already exposed)
3. Bridge passes RPM + starter state to `StarterSoundSynthesizer`
4. Synthesizer generates audio samples (procedural whine)
5. Bridge mixes starter + exhaust audio
6. CLI plays combined audio

### Bridge Controls:
- `EngineSimSetIgnitionDelay(handle, seconds)` - configurable delay
- Ignition module disabled during delay (prevent combustion)
- Starter sound plays while `m_starterMotor.m_enabled == true`
- Cross-fade when `CombustionChamber::m_lit` goes true

---

## 11. Real 12V Automotive Starter Motor Specifications

### From Research:
- **Voltage**: 12V DC (standard automotive)
- **Engine cranking RPM**: 200-300 RPM (optimal for ignition)
- **Motor shaft speed**: ~3,600 RPM (with gear reduction)
- **Gear ratio**: ~14.4:1 (motor shaft / engine speed)
- **Current draw**: Up to 400A at startup
- **Minimum loaded speed**: ≥1,350 RPM @ 11.5V
- **Torque required**: 8.0 Nm/L (4-cyl), 6.5 Nm/L (6-cyl), 6.0 Nm/L (8-cyl)
- **SPL**: 70-90 dB at 1 meter
- **Manufacturers**: Bosch, Nippondenso/Denso, Hitachi

### Electrical Effects on Sound:
- Battery voltage sags under 400A load (12V → ~10.8V)
- Lower voltage = slightly lower frequency and amplitude
- Recovery as load stabilizes (~1 second)

---

## 12. OSS Resources and Research Found

### Open Source Implementations:
- [DasEtwas/enginesound](https://github.com/DasEtwas/enginesound) - Rust procedural engine sound (GUI/CLI)
- [Antonio-R1/engine-sound-generator](https://github.com/Antonio-R1/engine-sound-generator) - JavaScript/Web Audio
- [ATG-Simulator/VehicleNoiseSynthesizer](https://github.com/ATG-Simulator/VehicleNoiseSynthesizer) - Unity granular synthesis

### Research Papers:
- [Procedural Engine Sounds Using Neural Audio Synthesis](https://kth.diva-portal.org/smash/get/diva2:1465597/FULLTEXT01.pdf) (KTH, 2021)
- [Physically Informed Car Engine Sound Synthesis](https://www.researchgate.net/publication/280086598_Physically_informed_car_engine_sound_synthesis_for_virtual_and_augmented_environments)
- [Physical Modeling and Synthesis of Motor Noise](https://www.academia.edu/31481122/Physical_Modeling_and_Synthesis_of_Motor_Noise_for_Replication_of_a_Sound_Effects_Library) - DC motor modeling
- [A High Fidelity Starter Model for Engine Start Simulations](https://www.academia.edu/16563569/A_high_fidelity_starter_model_for_engine_start_simulations)

### Key Finding from Research:
A complete and exhaustive model of the combined engine-starter system has not appeared to date in the open literature. This is novel work.

---

## 13. Engine-Sim Physics Hooks Available

### Already Exposed (No Engine-Sim Modification Needed):
| Hook | Location | Purpose |
|------|----------|---------|
| `StarterMotor::m_enabled` | starter_motor.h | Is starter engaged? |
| `Crankshaft::getRPM()` | crankshaft.h | Current engine RPM |
| `CombustionChamber::m_lit` | combustion_chamber.h | Is combustion active? |
| `CombustionChamber::m_litLastFrame` | combustion_chamber.h | Did ignition just happen? |
| `IgnitionModule::m_enabled` | ignition_module.h | Master ignition switch (for delay) |
| `SparkPlug::enabled` | ignition_module.h | Per-cylinder ignition control |
| `Engine::getCylinderCount()` | engine.h | Number of cylinders |
| `StarterMotor::m_maxTorque` | starter_motor.h | Motor torque spec |
| `StarterMotor::m_rotationSpeed` | starter_motor.h | Target motor RPM |

### For Ignition Delay:
Set `IgnitionModule::m_enabled = false` during delay period, re-enable after timer expires.

---

## 14. Available WAV Files

### User's Recording:
- **File**: `~/Downloads/cars-specific-lamborghini-countach-start-sound-effect-066853627_nw_prev.mp4`
- **Format**: MP4 (156KB)
- **Content**: Lamborghini Countach startup (includes starter + engine fire)
- **Tool available**: ffmpeg (for extraction)

### User's Note on WAV Recordings:
All pre-recorded WAVs already contain load fluctuations from the original engine. These cannot be pitch-shifted for the hybrid approach without double-applying the load effect. A clean electric whine is needed for hybrid mode - either from a different source or procedurally generated.

---

## 15. Environment Notes

- **Platform**: macOS (Apple Silicon M4 Pro, 26GB)
- **Audio playback**: `afplay` (built-in macOS)
- **Python**: Requires venv for numpy/scipy (`/tmp/starter_venv/`)
- **Build system**: CMake with Release mode default
- **Audio tools available**: ffmpeg (installed via Homebrew)
- **Audio tools NOT available**: sox (not installed)

---

## 16. Conversation Timeline

1. **Question**: Does engine-sim support starter sounds?
2. **Finding**: No - only exhaust audio, starter motor is physics-only
3. **Discussion**: Three approaches identified (pre-recorded, hybrid recorded, hybrid procedural)
4. **User clarification**: Two real options - pre-recorded loop NOW, hybrid LATER
5. **User suggestion**: Procedural baseline might be simplest for hybrid
6. **Attempt 1**: Simple sine waves → FAILED (artificial)
7. **Attempt 2**: Commutator ripple model → PARTIAL (noise good, harmonics bad)
8. **Attempt 3**: Real 12V specs → TOO LOW (engine hum, not starter whine)
9. **User provides p5.js reference** → Three-oscillator approach adopted
10. **Attempt 4**: p5.js-style oscillators → STILL LOW (engine RPM scaling)
11. **Attempt 5 (Option B)**: Motor shaft RPM scaling → BETTER but oscillating
12. **User chose Option 3**: Load pulses only when engine fires
13. **Attempt 6**: Smooth cranking → needs 0.5s ramp, less "big"
14. **Architecture discussion**: Separate module, modify bridge not engine-sim
15. **Current state**: Generator needs 0.5s ramp, Options A and C still to try
