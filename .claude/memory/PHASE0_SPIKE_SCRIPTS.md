# Phase 0 Spikes — Implementation & Run Instructions

**Status:** Source code complete, build integration added
**Test Architect Deliverables:** `../../.claude/memory/TEST_STRATEGY.md`, `INTERFACE_CONTRACTS.md`, `PR_CHECKLIST.md`

---

## Quick Start (when Ninja is available)

```bash
cd engine-sim-bridge

# Option A: Using existing build-test (Ninja)
cmake -B build-test -GNinja     # only if ninja is installed
cmake --build build-test --target audio_sweep_spike clutch_sweep_spike jitter_test_spike
ctest -R "audio_sweep_spike|clutch_sweep_spike|jitter_test_spike" -VV

# Option B: Create fresh build with your available generator
cmake -B build -G "Unix Makefiles"  # or "Xcode"
cmake --build build --target audio_sweep_spike clutch_sweep_spike jitter_test_spike
```

**Output artifacts:**
- `build[-test]/spikes/audio_sweep/audio_sweep.wav` — listen for authentic ICE sound
- `build[-test]/spikes/clutch_sweep/clutch_sweep_results.csv` — max torque vs RPM error
- `build[-test]/spikes/jitter_test/jitter_data.csv` — mean/max RPM jitter per rate

---

## Spike 1: Dyno-Mode Audio Sweep

**File:** `engine-sim-bridge/test/spikes/AudioSweepSpike.cpp`

**Purpose:** Verify that dyno mode (primary RPM control strategy) produces
authentic ICE engine sounds rather than a flat tone generator.

**Method:** Enable dyno mode, sweep target RPM from 1200→6500 at constant 50% throttle,
capture all generated audio into a WAV file.

**Key observation:** Does the sound have realistic harmonic richness and
frequency sweep? A pure tone generator would produce a constant-timbre sine;
real ICE combustion produces evolving harmonics.

**Expected output:** `audio_sweep.wav` with RMS > 0.01. Listen: should sound like
an engine revving, not a beep.

**Evidence needed to proceed:**
- ✅ PASS: Audio has clear fundamental sweep 100–300 Hz with harmonic comb
- ⚠️ FAIL: Audio is mostly sine-wave, lacks harmonic richness → may need to
  augment with `VehicleDragConstraint` coupling

---

## Spike 2: Clutch Parameter Sweep

**File:** `engine-sim-bridge/test/spikes/ClutchParameterSweepSpike.cpp`

**Purpose:** Determine whether increasing `m_maxClutchTorque` can ever yield a
rigid kinematic lock between engine and drivetrain. Tests physics-driven path viability.

**Method:** Build inline-6 piston engine + vehicle + transmission. Disable dyno.
Set clutch pressure = 1.0 (maximum). Apply step throttle 0→100% at t=2s.
Record actual engine RPM and compare to naive linear model `800 + 5200 × throttle`.
Compute RMS error percentage across valid range.

**Test matrix:** maxClutchTorque = {500, 1000, 2000, 4000, 8000, 16000} Nm

**Expected output:** `clutch_sweep_results.csv` (torque_Nm, maxRpmErrorPct)

**Evidence needed to proceed:**
- ✅ Physical-driven viable: error < 5% at some torque level → try `m_maxClutchTorque` tuning
- ⚠️ Physics-driven infeasible: error > 10% even at 16,000 Nm → confirms
  `CLUTCH_PHYSICS_ANALYSIS.md` conclusion (clutch allows slip at finite capacity);
  **dyno mode must be primary**

---

## Spike 3: Telemetry Rate Jitter Test

**File:** `engine-sim-bridge/test/spikes/TelemetryJitterSpike.cpp`

**Purpose:** Verify that 10 Hz BLE telemetry rate does NOT cause >50 RPM visible
jitter in the dyno-controlled twin (sufficient for smooth sound).

**Method:** Simulate 0→80 km/h ramp in 8 seconds. Drive dyno target using telemetry
updated at 10 Hz. Sample actual RPM at ~100 Hz. Compute mean absolute error
and maximum absolute error.

**Expected output:** `jitter_data.csv` (timeSec, targetRpm, actualRpm, errRpm, absPctErr)

**Evidence needed to proceed:**
- ✅ Mean abs error < 30 RPM → no smoothing needed, proceed
- ⚠️ 30–60 RPM → add EMA filter (τ=50ms) to throttle/dyno target
- ⚠️ > 60 RPM → investigate integration (likely interpolation issue)

---

## Integration Status

**Code additions:**
- `engine-sim-bridge/include/simulator/BridgeSimulator.h` — added `getInternalSimulator()` accessor (test seam; justified for evidence-gathering)
- `engine-sim-bridge/test/spikes/AudioSweepSpike.cpp` — new
- `engine-sim-bridge/test/spikes/ClutchParameterSweepSpike.cpp` — new
- `engine-sim-bridge/test/spikes/TelemetryJitterSpike.cpp` — new
- `engine-sim-bridge/CMakeLists.txt` — added `BUILD_PHASE0_SPIKES` option with 3 targets

**Build state:** Source files compile without errors in IDE. The CI/make build
requires Ninja generator which is not present on this host. Spike targets
are registered and will build when Ninja or an alternative fresh generator
is used.

---

## Decision Gates (from Phase 0 plan)

| Question | Answer After Spikes | Source |
|---|---|---|
| Can clutch yield rigid lock at max pressure? | TBD (clutch sweep data) | Spike 2 CSV |
| Is dyno mode suitable as primary? | TBD (audio sweep quality) | Spike 1 WAV |
| Does 10 Hz rate cause visible jitter? | TBD (jitter test statistics) | Spike 3 CSV |

**Plan reference:** `.claude/plans/the-vehicle-sim-repo-has-foamy-dawn.md`
