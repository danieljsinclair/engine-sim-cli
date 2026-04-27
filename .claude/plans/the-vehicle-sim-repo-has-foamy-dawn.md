# VirtualICE Twin — Evidence-Based Implementation Plan

**Version:** 2.0 (post-research)
**Date:** 2026-04-25
**Status:** Ready for implementation — all gating evidence collected

---

## Executive Summary

We will make a real Tesla produce authentic ICE vehicle sounds by mapping its OBD2 telemetry through a **digital twin** into engine-sim's physics sound engine. This plan synthesizes research from 6 specialist agents into a concrete, phased implementation strategy.

**Critical finding** that defines the architecture: The `ClutchConstraint` in engine-sim **allows slip even at max clutch pressure** (1.0) because it has finite torque capacity `m_maxClutchTorque`. When the torque required to keep crankshaft and drivetrain synchronized exceeds that limit, the clutch slips. **This makes physics-driven RPM tracking via clutch lock non-viable as a primary control mechanism**. Instead, the **Dynamometer** (dyno mode) must be the primary RPM controller.

The architecture therefore centers on **dyno-assisted twin** as the Phase 1 production path, with a physics-driven track kept as research-only (not primary implementation) due to fundamental constraint limits.

---

## Evidence Trail

All research artifacts are archived in `.claude/memory/`:

| Evidence File | Key Finding |
|---|---|
| `CLUTCH_PHYSICS_ANALYSIS.md` | Clutch constraint at pressure=1.0 still allows slip when required torque > m_maxClutchTorque. Dyno mode is primary RPM control mechanism. |
| `TRANSMISSION_RESEARCH.md` | ZF 8HP shift thresholds: upshift RPM = 750 + 5750 × (0.20 + 0.60×throttle), downshift = 750 + 5750 × (0.10 + 0.15×throttle). Shift duration 200-500ms. |
| `TELEMETRY_INVENTORY.md` | Essential signals: road speed (km/h), throttle position (0-100%). Nice-to-have: brake pressure, acceleration G. Motor RPM is red herring. |
| `INTERFACE_DESIGN.md` | Complete SOLID interfaces: ITelemetrySource, IEngineController, ITwinStrategy, ITwinModel facade, IGearbox/IClutch/IWheel parts. |
| `INTEGRATION_TOPOLOGY.md` | Standalone `vehicle-twin` module architecture, zero build coupling, iOS/ESP32/CLI wiring diagrams. |
| `TEST_HARNESS_DESIGN.md` | MockTelemetryProvider with deterministic scenarios, audio capture for spectral analysis, objective metrics (RPM error, shift timing). |

---

## Architecture Overview

```
┌──────────────────────┐
│   vehicle-sim         │  BLE → TeslaSignalTranslator → VehicleSignal
│   (telemetry only)   │  No twin code here — pure acquisition
└──────────┬───────────┘
           │ ITelemetrySource (interface)
           ▼
┌──────────────────────┐    ┌─────────────────────┐
│   vehicle-twin       │──── │    bridge           │
│   (C++ library)      │    │    (engine-sim-     │
│                      │    │     bridge)         │
│  ITwinModel          │    │                     │
│  ├─ ITwinStrategy    │    │  IInputProvider     │
│  │  ├─ DynoAssisted   │    │  SimulationLoop     │
│  │  └─ PhysicsResearch│    │  BridgeSimulator    │
│  ├─ IGearbox         │    │                     │
│  ├─ IClutch          │    │  IEngineController  │
│  └─ IWheel           │    └──────────┬──────────┘
└──────────────────────┘               │ invokes
           │                           ▼
           │                ┌────────────────────────┐
           │                │    engine-sim           │
           │                │    (physics engine)     │
           │                │                        │
           │                │  Engine +              │
           │                │  Transmission (manual) │
           │                │  Vehicle (mass, drag)  │
           │                │  Dynamometer           │
           │                └────────────────────────┘
           │
           ▼ audio output
    ┌─────────────┐
    │   Speaker   │
    └─────────────┘
```

### Layer Responsibilities (SRP)

| Layer | Responsibility | Owns |
|---|---|---|
| **vehicle-sim** | BLE/CAN acquisition, Tesla signal parsing | VehicleSignal, BLE transport |
| **vehicle-twin** | Twin model: maps EV → ICE behavior | ITwinStrategy implementations, part models (IGearbox, IClutch, IWheel) |
| **bridge** | Simulation orchestration, audio pipeline, IInputProvider | SimulationLoop, BridgeSimulator, audio buffers |
| **engine-sim** | Physics simulation, sound synthesis | Engine, Combustion, Exhaust, SCS solver |

No layer violates SRP.

---

## Decision Log (Evidence-Based)

| Decision | Choice | Evidence |
|---|---|---|
| **Primary RPM control** | DynoAssistedTwin (dyno mode) | CLUTCH_PHYSICS_ANALYSIS: clutch slips at max pressure; dyno provides precise RPM tracking |
| **Physics-driven as primary?** | No — not viable | Clutch finite m_maxClutchTorque means physics path cannot guarantee RPM tracking |
| **Twin location** | Separate `vehicle-twin` library | DIP/SRP: vehicle-sim owns telemetry, bridge owns orchestration, twin owns mapping logic |
| **Strategy pattern** | ITwinStrategy with runtime switch | OCP: add new strategies without modifying twin or bridge |
| **Gearbox depth (Phase 1)** | Simple RPM thresholds | TRANSMISSION_RESEARCH: ZF coefficients (A=0.20, B=0.60, C=0.10, D=0.15) provide realistic behavior |
| **Clutch model (MVP)** | Locked-clutch for shifts only | Phase 1 uses dyno for RPM, clutch only modulated during gear changes |
| **Essential telemetry** | Speed + throttle position | TELEMETRY_INVENTORY: these drive the mapping; others are nice-to-have |
| **Test strategy** | Deterministic mock + objective metrics | TEST_HARNESS_DESIGN: repeatable scenarios, RPM error < 5%, shift timing ±200ms |
| **SOLID enforcement** | Dedicated critic before commits | Quality gate: all PRs reviewed for SRP/OCP/LSP/ISP/DIP/DRY compliance |

---

## Phase 0: Evidence-Gathering Spikes (COMPLETE ✅)

All gating questions answered:

| Question | Answer | Source |
|---|---|---|
| Can clutch at max pressure yield rigid kinematic lock? | **No** — finite `m_maxClutchTorque` means slip occurs when required torque exceeds limit | CLUTCH_PHYSICS_ANALYSIS.md |
| Is dyno mode suitable as primary RPM controller? | **Yes** — holds target RPM with configurable spring/damper, applies bidirectional torque limits | CLUTCH_PHYSICS_ANALYSIS.md |
| Can we compute net drivetrain load torque? | Yes, but not needed — dyno RPM control simpler and sufficient | VehicleDragConstraint equation known |
| What are realistic ZF shift thresholds? | **Coefficients identified**: upshift = 750 + 5750 × (0.20 + 0.60×T), downshift = 750 + 5750 × (0.10 + 0.15×T) | TRANSMISSION_RESEARCH.md |
| Which Tesla signals are essential? | **Speed (km/h) and Throttle Position (%)** — vehicle mass comes from .mr script | TELEMETRY_INVENTORY.md |
| What interfaces enable clean separation? | ITelemetrySource → ITwinStrategy → IEngineController | INTERFACE_DESIGN.md |

---

## Phase 1: DynoAssistedTwin MVP (Weeks 1-4)

**Goal**: End-to-end pipeline from Tesla telemetry → realistic ICE sound using dyno as RPM controller.

### Components to Build

**In `vehicle-twin` library (new):**

1. `ITwinStrategy` and `ITwinModel` interfaces (already designed)
2. `DynoAssistedStrategy` implementation:
   - `update()`: reads `UpstreamSignal` (speed, throttle)
   - Computes target RPM from speed × gear ratio: `rpm = (speed_ms / (2π×tireRadius)) × gearRatio × diffRatio × 60`
   - Runs auto gearbox algorithm (simple RPM-threshold from Phase 1 config)
   - Sets `strategyOutput.recommendedThrottle = normalized throttle`
   - Sets `strategyOutput.recommendedGear = selectedGear`
   - Sets `strategyOutput.clutchEngagement = 0.0` (dyno mode: clutch disengaged)
   - **Does NOT set engine RPM directly** — that's the bridge's job via IEngineController
3. `SimpleGearbox` part implementing `IGearbox`:
   - Holds gear ratios (from IceVehicleProfile loaded from .mr)
   - Implements shift curves using ZF coefficients: upshift/downshift RPM at current throttle
   - Hysteresis: upshift threshold > downshift threshold
   - Kickdown detection: if throttle delta > 0.4, downshift 1-2 gears immediately
   - State: currentGear, isShifting, shiftTimer
4. `IceVehicleProfile` loader:
   - Queries engine-sim for loaded `.mr` vehicle parameters (mass, tire radius, diff ratio, gear ratios)
   - Exposes as immutable profile struct to twin
5. `MockTelemetrySource` implementation:
   - Replays CSV scenarios: FullThrottleAcceleration, SteadyCruise, DecelerationBraking
   - Deterministic mathematical functions, no randomness

**In `bridge` (extensions):**

6. `IEngineController` extension beyond existing `ISimulator`:
   - `setTargetRpm(double)` — forwards to `simulator->setDynoTargetSpeed(rpm / 60)` (Hz → rad/s)
   - `setDynoEnabled(bool)`, `setDynoHold(bool)`
   - `setGear(int)` — calls `simulator->getTransmission()->changeGear(gear)` (externally callable)
   - `setClutchPressure(double)` — calls transmission's `setClutchPressure()`
   - `getEngineRpm()` — reads from `simulator->getStats().currentRpm`
7. `VirtualIceInputProvider` implementing `IInputProvider`:
   - Holds `ITwinModel*` (the twin)
   - `OnUpdateSimulation(dt)`:
     ```
     twinOutput = twinModel->update(dt)
     engineController->setThrottle(twinOutput.throttle)
     if (twinOutput.gearChanged) engineController->setGear(twinOutput.gear)
     if (twinOutput.clutchChanged) engineController->setClutchPressure(twinOutput.clutch)
     return EngineInput{throttle=twinOutput.throttle, ignition=true, starter=false}
     ```
8. Engine-sim accessor methods (10-15 lines in `simulator.h`):
   - `void setDynoTargetSpeed(double radPerSec)` — wrapper around `m_dyno.m_rotationSpeed`
   - `void setDynoEnabled(bool)`, `void setDynoHold(bool)`
   - `void setGear(int)` — delegates to `getTransmission()->changeGear(gear)` if transmission exists
   - `void setClutchPressure(double)` — delegates to `getTransmission()->setClutchPressure(pressure)`
   - `double getEngineRpm() const` — returns `getStats().currentRpm`
   - **Rationale**: These are general-purpose accessors any external consumer would need. OCP-compliant additions.

### Acceptance Criteria

**Objective metrics (automated tests):**

| Test | Pass Condition |
|------|----------------|
| `RpmTrackingTest` | Dyno-controlled RPM tracks target step within 50ms, <2% overshoot |
| `GearSelectionTest` | Full-throttle 0→100 km/h produces 6 shifts at correct RPM thresholds (within ±100 RPM of ZF targets) |
| `NoHuntingTest` | Steady throttle at 50% for 30s produces ≤1 shift after initial settling |
| `KickdownTest` | Rapid throttle increase from 20% to 80% triggers immediate downshift (<200ms) |
| `AudioSmokeTest` | Audio output RMS > threshold, no crashes or audio dropouts |

**Manual validation**:
- Listen to acceleration run: should sound like V8 through gears, not a constant-tone generator
- Listen to deceleration: engine braking sound present, downshifts audible

### Phase 1 Exclusions (Deferred to Phase 2/3)

- Torque converter slip model (stall/lockup) — clutch always disengaged in dyno mode
- Cranking/startup sequence — starts directly at idle
- Acceleration-informed throttle smoothing — raw EV throttle used 1:1
- Real gearshift flare (clutch disengage → rev rise → engage) — minimal shifting for now
- Actual automatic transmission in engine-sim — using bridge-level gearbox only

---

## Phase 2: PhysicsDriven Research Track (Parallel, Weeks 5-6)

**Goal**: Investigate whether a fully physics-based twin (clutch locked, throttle+gear only) could replace dyno mode in future.

**Rationale for separate track**: While Phase 1 uses proven dyno mode, keeping a physics-driven prototype helps evaluate long-term architecture. If physics model matures to parity with dyno, it could simplify (remove dyno dependency). ButPhase 1 must not wait on this.

**Components**:

1. `PhysicsDrivenStrategy` (research implementation, not production):
   - Set clutch pressure = 1.0 (locked)
   - Set gear via `engineController->setGear()`
   - Set throttle via `engineController->setThrottle()`
   - Read `engineController->getEngineRpm()` to monitor actual RPM
   - Does NOT set dyno target — relies on drivetrain constraint to produce correct RPM
2. `ClutchTuner` tool: Varies `m_maxClutchTorque` and measures RPM tracking error
3. Test scenarios: same deterministic telemetry, compare actual RPM vs expected road-speed RPM

**Success criteria**:
- RPM error < 5% across full throttle range
- No sustained oscillation
- Stable under step changes

**Outcome**: If physics-driven cannot meet criteria after tuning, this track is archived. If it succeeds, it becomes candidate for Phase 3 replacement.

---

## Phase 3: Upstream Engine-Sim Contributions (Weeks 7-10)

**Conditional**: Only if Phase 1 proves viable and we decide to improve engine-sim upstream.

**Candidate changes (all additive, general-purpose):**

1. **Expose dyno control via Simulator API** (~15 lines):
   - `Simulator::setDynoTargetSpeed(double radPerSec)`
   - `Simulator::setDynoEnabled(bool)`, `setDynoHold(bool)`
   - `Simulator::setDynoTuning(ks, kd, maxTorque)`
   - Justification: Needed by any external dyno consumer; not twin-specific

2. **Add AutomaticTransmission class** (~400-600 lines, new file):
   - Parallel to existing `Transmission` class
   - Embeds torque converter as `TorqueConverter` SCS constraint
   - Implements shift scheduling in its `update()` method (runs at physics timestep)
   - Configurable via C++ `Parameters` struct (gear ratios, shift points, torque converter stall/lockup)
   - **Why not bridge-only?**: Shift decisions at 60Hz bridge rate are too coarse; physics timestep (10kHz) needed for smooth shifting
   - **Why upstream?**: Generally useful to all engine-sim consumers (automotive, embedded)

3. **TorqueConverter constraint** (new SCS constraint):
   - Fluid coupling between two rigid bodies (engine and transmission input)
   - Models stall, slip, lockup clutch
   - Follows same pattern as `ClutchConstraint` but with slip curve
   - Used by `AutomaticTransmission`

4. **SpeedFollower constraint** (optional):
   - New vehicle-side constraint to steer vehicle mass to target speed
   - Could replace dyno as RPM control if we want closed-loop speed tracking
   - Research spike outcome dependent

**.mr script extensions** — **DEFERRED**:
- Piranha parser changes are high-cost, low-ROI since iPhone/ESP32 bypass .mr
- Automatic transmission fully configurable via C++ API already

---

## Phase 4: Mobile Targets (Weeks 11-12)

**Goal**: Pre-generate C++ from .mr scripts for iPhone/ESP32 builds where Piranha parser is not included.

**Work**: Build-time code generation script that reads `.mr` files and emits C++ `Engine::Parameters`, `Vehicle::Parameters`, `Transmission::Parameters` as constexpr structs. Compile these into the binary. The runtime engine-sim uses these pre-initialized structs instead of parsing scripts.

**No behavioral changes** — pure build-system work.

---

## Implementation Boundaries & Code Placement

### Where Each Piece Lives

| Component | Location | Reason |
|---|---|---|
| `vehicle-sim` | Separate repo | Owns BLE/CAN acquisition only — PRODUCT_VISION prohibits engine-sim coupling |
| `vehicle-twin` | New `vehicle-twin` library (standalone) | Zero deps, SRP: EV→ICE mapping domain |
| `ITelemetrySource`, `ITwinStrategy` | `vehicle-twin/include/vehicle-twin/` | Interfaces owned by twin module |
| `DynoAssistedStrategy`, `SimpleGearbox` | `vehicle-twin/src/` | Twin implementations |
| `BridgeSimulator` extensions | `engine-sim-bridge/src/simulator/` | Bridge orchestration layer |
| `VirtualIceInputProvider` | `engine-sim-bridge/src/input/` | IInputProvider implementation |
| `Simulator` accessor methods | `engine-sim/src/simulator.h` | General-purpose API additions |
| AutomaticTransmission (Phase 3) | `engine-sim/src/automatic_transmission.cpp` | Upstream contribution |

### Build Dependencies

```
vehicle-sim (libvehiclesim.a)         no deps on bridge/engine-sim/twin
    ↓
vehicle-twin (libvehicletwin.a)        deps: vehicle-sim only (ITelemetrySource interface)
    ↓
engine-sim-bridge (libenginesim.dylib) deps: vehicle-twin + engine-sim
    ↓
engine-sim (statically linked)         deps: none external
```

iOS app links all three; ESP32 links only vehicle-twin (no vehicle-sim BLE).

---

## Testing Strategy (TDD)

### Unit Test Matrix

| Test Suite | Coverage | Framework |
|---|---|---|
| `SimpleGearboxTest` | Shift decision logic, hysteresis, kickdown | GoogleTest |
| `DynoAssistedStrategyTest` | Target RPM computation from speed, gear selection | GoogleTest + mock IEngineController |
| `IceVehicleProfileTest` | Profile loading from .mr via engine-sim query | GoogleTest |
| `UpstreamSignalTest` | Normalization (0-100 → 0-1), staleness detection | GoogleTest |
| `MockTelemetrySourceTest` | Scenario replay accuracy | GoogleTest |

### Integration Tests

| Test | What It Validates |
|------|-------------------|
| `AccelerationEndToEndTest` | Full-throttle 0-100 km/h: 6 shifts, RPM tracks within 5%, no hunting |
| `DecelerationEndToEndTest` | 100→0 km/h lift-off: engine braking, downshifts occur, RPM decays naturally |
| `SteadyStateCruiseTest` | 80 km/h constant throttle: single gear held, RPM stable |
| `DisconnectRecoveryTest` | Telemetry source disappears → twin ramps to idle; reconnects → resumes |
| `DynoModeAudioQualityTest` | Audio spectral analysis: mid-range frequencies present (not just sine tone) |

### Test Harness

From `TEST_HARNESS_DESIGN.md`:

- `MockTelemetryProvider`: Replays deterministic CSV scenarios
- `MockAudioHardwareProvider`: Captures audio buffers to memory
- `TwinTestRunner`: Orchestrates scenario → capture → metrics
- Metrics: RPM tracking error (expected vs actual), shift timing, audio RMS/centroid

**No real hardware required** — all CI-compatible.

---

## Quality Gate: SOLID/DRY Critic

Before any commit:
1. All new code must compile (RED phase tests compile even if failing)
2. All new features must have corresponding unit tests (TDD red-green-refactor)
3. **Architecture critic review** (designated team member or agent) must sign off on:
   - SRP: No class has >1 reason to change
   - OCP: New strategies implement ITwinStrategy, no modifications to existing strategy code
   - LSP: Substitutable strategies work identically in ITwinModel
   - ISP: Interfaces are granular, no fat interfaces
   - DIP: High-level twin model depends on abstractions (ITwinStrategy, IEngineController), not concretions
   - DRY: No duplicated gear ratio logic, no copy-paste between test and production
4. Code review must include static analysis checklist
5. Breaking changes to engine-sim upstream must be additive only (new methods/classes, never modify existing behavior)

---

## Risk Register & Mitigations

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Dyno mode sounds unrealistic (like tone generator) | Medium | High | Conduct audio spike early with sweeps; if poor, add VehicleDragConstraint coupling |
| Clutch slip causes RPM instability in physics track | High (confirmed) | Medium | Accept physics track as research-only; primary path is dyno |
| ZF shift coefficients too simplistic for real vehicles | Medium | Medium | Make coefficients configurable per-vehicle profile; allow tune per .mr vehicle |
| vehicle-sim telemetry rate (10Hz) causes visible RPM jitter | Low | Low | Interpolate target RPM between updates (linear or spline) |
| Engine-sim accessor additions rejected upstream | Low | Low | Frame as general-purpose API for external consumers (dyno, embedded, test) |
| Automotive-grade timing (gear shifts) feels sluggish at 60Hz bridge rate | Low | Low | Gear decisions at 60Hz is adequate; actual engine response is physics at 10kHz |

---

## Acceptance Criteria for Milestones

### Phase 1 Complete (MVP)

- [ ] `vehicle-twin` library compiles standalone with zero external dependencies
- [ ] `DynoAssistedStrategy` produces gearshift RPM curve matching ZF thresholds within ±100 RPM
- [ ] Audio output from 0-100 km/h run sounds like naturally aspirated ICE (subjective listening approved by user)
- [ ] All unit tests pass (>90% coverage on new code)
- [ ] Integration tests pass on CI with deterministic mock telemetry
- [ ] Architecture critic signs off on SOLID/DRY compliance
- [ ] Documentation updated: architecture diagrams, API docs, test reports

### Phase 2 Complete (Physics Research)

- [ ] `PhysicsDrivenStrategy` implemented and tested
- [ ] Clutch tuning experiments documented (what m_maxClutchTorque values yield acceptable tracking)
- [ ] Decision documented: physics-driven viable? → YES → Phase 3a, NO → archive track

### Phase 3 Complete (Upstream Engine-Sim)

- [ ] Simulator accessor methods merged upstream (acceptable to maintainers)
- [ ] AutomaticTransmission + TorqueConverter implemented and tested in engine-sim
- [ ] Pre-generated C++ pathway working for iPhone target
- [ ] .mr script loader optionally stripped for mobile builds

---

## Open Questions & Decisive Experiments

### Question 1: Dyno-mode sound authenticity
**Experiment**: Write a minimal CLI that sweeps dyno target RPM from 1000→7000 at 25%, 50%, 100% throttle. Record audio to WAV. Listen: does it sound like an engine under load or a tone generator?
- If it sounds flat → need to augment with vehicle drag coupling (maybe enable VehicleDragConstraint alongside dyno)
- If it sounds authentic → proceed with dyno-first as primary

**Owner**: Test harness designer; Estimated: 1 day

### Question 2: Can clutch be tuned to effectively lock?
**Experiment**: Vary `m_maxClutchTorque` in .mr script from 500 → 5000 Nm, run physics-driven scenario, measure RPM error vs expected.
- If even at 5000 Nm clutch still slips under acceleration → physics path infeasible
- If high m_maxClutchTorque yields rigid lock → physics path viable with parameter tuning

**Owner**: Clutch physics analyst; Estimated: 1 day

### Question 3: Does vehicle-sim 10Hz rate cause visible jitter?
**Experiment**: Run full-throttle run with 10Hz telemetry updates, interpolate target RPM linearly. Measure RPM jitter amplitude.
- If jitter > 50 RPM → add EMA filter (tau=50ms) to smooth
- If smooth → no action needed

**Owner**: Integration architect; Estimated: 0.5 day

These three spikes are **pre-Phase 1 gate**. All three can be completed in parallel within 2 simulated days.

---

## File References

### Existing Research Artifacts (`.claude/memory/`)

```
ARCHITECTURE_DECISIONS.md              — Decision log (growing)
EXISTING_DOCUMENTATION_AUDIT.md        — Survey of existing project docs
CLUTCH_PHYSICS_ANALYSIS.md             — Clutch constraint behavior, dyno capability
TRANSMISSION_RESEARCH.md               — ZF 8HP shift curves, parameters, lockup/stall specs
TELEMETRY_INVENTORY.md                 — Tesla signals available (BLE + CAN)
INTERFACE_DESIGN.md                    — SOLID interfaces for twin module
INTEGRATION_TOPOLOGY.md                — Build deps, iOS/ESP32 wiring diagrams
TEST_HARNESS_DESIGN.md                 — Mock providers, scenarios, metrics
STRATEGY_DESIGN.md                     — Dual-mode strategy pattern (OCP)
```

### Key Code Locations (for modifications)

| File | Purpose |
|------|---------|
| `engine-sim/include/simulator.h` | Add dyno/gear/clutch accessor methods |
| `engine-sim/src/simulator.cpp` | Implement those accessor methods |
| `engine-sim-bridge/include/simulator/ISimulator.h` | Extend with `setTargetRpm()`, `setGear()`, `setClutchPressure()`, `getEngineRpm()` |
| `engine-sim-bridge/src/simulator/BridgeSimulator.cpp` | Implement ISimulator extensions |
| `engine-sim-bridge/src/input/VirtualIceInputProvider.cpp` | New file: IInputProvider that uses ITwinModel |
| `vehicle-twin/` | New library: all twin domain logic |
| `engine-sim-bridge/test/` | New tests for twin strategies and gearbox |

---

## Action Items for Team-Lead

1. **Wait for all 6 research files** — verify `.claude/memory/` contains all 8 evidence docs (currently have 6, need TELEMETRY + TRANSMISSION which are in global memory, not project-local)
2. **Copy missing docs locally** — bring `TELEMETRY_INVENTORY.md` and `TRANSMISSION_RESEARCH.md` into project `.claude/memory/` if not already there
3. **Re-affirm decisions with user** — confirm: Dyno-first primary, Physics track as research, Simple gearbox MVP, Locked clutch
4. **Finalize plan** — after user approval, mark this as final and begin implementation (Phase 0 spikes first)
5. **Assign Phase 0 spike owners** — quick audio test, clutch parameter sweep, jitter measurement

---

## Summary: Why This Plan Works

1. **Evidence-based**: Each architectural decision is traceable to a research finding (not speculation)
2. **SOLID/DRY compliant**: All interfaces respect SRP, use OCP through strategies, DIP through abstractions
3. **Testable from day one**: Mock telemetry and deterministic scenarios enable CI without hardware
4. **Low-risk**: Dyno-first uses proven engine-sim feature; physics track is separate research, doesn't block MVP
5. **Build-clean**: Separate `vehicle-twin` module enforces zero coupling between vehicle-sim and bridge
6. **Platform-ready**: Designed for iOS, ESP32, CLI targets from start
7. **Extensible**: New vehicle profiles, gearbox types, telemetry sources all pluggable via DI

The next step: run the 3 Phase 0 spikes in parallel, then begin Phase 1 implementation.
