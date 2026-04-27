# Phase 0: Evidence-Gathering Spikes — Results & Findings

**Date:** 2026-04-25
**Status:** IN PROGRESS — implementing spike scripts

---

## Spike 1: Audio Sweep Test (PRODUCTION)

**Purpose:** Does dyno-mode engine sound authentic, or like a flat tone generator?

**Approach:** Sweep dyno target RPM from 1000 → 7000 at various throttle positions (25%, 50%, 100%). Record audio output to WAV files. Perform spectral analysis to verify harmonic richness (not pure tone).

**Output:** `build-test/spikes/audio_sweep/` — WAV files + spectrum PNGs/scalar reports.

**Hypothesis before test:** Dyno mode + VehicleDragConstraint will produce realistic engine harmonics because combustion physics is active. If flat, may need vehicle-speed coupling.

---

## Spike 2: Clutch Parameter Sweep Test

**Purpose:** Can clutch m_maxClutchTorque be tuned to prevent slip? Is physics-driven RPM tracking viable?

**Approach:** Iterate clutch torque capacity (500 Nm → 5000 Nm in doubling steps). For each, simulate a 0→100% step throttle at fixed gear. Measure maximum RPM tracking error. If error remains >5% even at 5000 Nm, physics-driven path is infeasible.

**Output:** `build-test/spikes/clutch_sweep/` — CSV of (torque_Nm, max_rpm_error_pct, slipped_flag)

**Hypothesis before test:** Even at very high m_maxClutchTorque, acceleration torque will exceed capacity → slip. Confirms CLUTCH_PHYSICS_ANALYSIS finding.

---

## Spike 3: Telemetry Rate Jitter Test

**Purpose:** Does 10Hz BLE telemetry rate cause visible RPM jitter in dyno-controlled twin?

**Approach:** Simulate full-throttle 0→100 km/h run. Feed telemetry at 10Hz and at 60Hz. Compare target RPM vs actual RPM jitter amplitude (std dev in Hz). If jitter > 50 RPM, smooth with EMA (tau=50ms).

**Output:** `build-test/spikes/jitter_test/` — CSV timestamps of (target_rpm, actual_rpm) at both rates.

**Hypothesis before test:** 60Hz bridge rate is sufficient; 10Hz upstream updates interpolated smoothly will not cause >50 RPM jitter.

---

## Implementation Files

Scripts are being authored in:
- `engine-sim-bridge/src/spikes/AudioSweep.cpp`
- `engine-sim-bridge/src/spikes/ClutchParameterSweep.cpp`
- `engine-sim-bridge/src/spikes/TelemetryJitterTest.cpp`

Build integration added via `engine-sim-bridge/spikes/CMakeLists.txt`.
