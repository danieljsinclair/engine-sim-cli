# VirtualICE Twin — Architectural Decision Log

## Decision 1: Twin Location — Bridge-Layer with Split Possibility (2026-04-24)

**Context**: Where does the VirtualICE twin (the model that maps EV telemetry → ICE behavior) live?

**Options considered**:
1. vehicle-sim (telemetry-acquisition repo)
2. engine-sim-bridge (orchestration layer)
3. Split across both

**Decision**: **Separate `vehicle-twin` module** shared by both vehicle-sim and bridge (see Decision 11). The twin is neither in vehicle-sim nor bridge; it's a third module (`CMake` target) that defines interfaces only. vehicle-sim and bridge both compile against it. This gives maximum mobility and zero coupling.

**Rationale**:
- vehicle-sim ≠ bridge in purpose (acquisition vs orchestration)
- Twin needs signals from both, but shouldn't force either to own twin behavior
- SOLID: Each owns its concerns; `vehicle-twin` is pure domain modeling at the intersection
- Open/Closed: The module can be swapped or restructured without touching vehicle-sim or bridge

---

## Decision 2: Naming — Component-Based, Not Monolithic (2026-04-24)

**Context**: "VirtualIceTwin" was too monolithic.

**Decision**: Model as composition of parts with clear interfaces:
- `IVehicleModel` — overall vehicle behavior
- `IGearbox` — automatic transmission behavior
- `IClutch` — torque converter/clutch slip model
- `IWheel` — wheel/road interface (speed, radius, inertia)

Implementations named by type (e.g., `ZF8SpeedAuto`, `GenericAlloyWheel`, `TorqueConverterClutch`). Uses Strategy pattern, DI-ed.

**Rationale**: SOLID OCP — different vehicle types (sports sedan, truck, EV conversion) get different part implementations without modifying the model.

---

## Decision 3: Evidence-First, Dual-Track Validation (2026-04-24)

**Context**: Must choose between Dyno-driven (simpler, load-based) vs Physics-driven (clutch+drivetrain) approaches without subjective "sounds good" calls.

**Decision**: Run **parallel spikes** with objective metrics:
1. **Dyno spike**: Record audio sweeps at various throttle/gear, measure spectral characteristics
2. **Physics spike**: Lock clutch, set throttle, measure RPM equilibrium vs expected road-speed value
3. **Torque-equilibrium spike**: Can we compute net wheel torque from OBD2 and feed it to dyno to simulate load?

Acceptance criteria are **quantitative**:
- RPM error < 5% of road-speed-derived RPM
- Gear shifts occur within ±100 RPM of shift curve threshold
- No sustained hunting (>3 shifts in 10s same gear pair)
- Audio spectral centroid matches reference recordings (later)

---

## Decision 4: .mr Parameters Should Be Single Source (2026-04-24)

**Context**: `IceVehicleProfile` in bridge would duplicate parameters already in .mr files (gear ratios, mass, diff, tire).

**Decision**: **Don't duplicate**. The twin should either:
- Load the .mr script at runtime and extract parameters (Phase 2+), OR
- Have parameters generated at build-time from .mr parsing (iPhone/ESP32)

The bridge already knows which .mr script is loaded; the twin can query engine-sim for `Vehicle::getParameters()` or similar accessors to auto-configure.

**Rationale**: DRY — single source of truth for vehicle specs.

---

## Decision 5: Engine-Sim Modifications Should Be Generally Useful (2026-04-24)

**Context**: What changes are acceptable to the upstream engine-sim fork?

**Decision**: Only modifications that benefit general engine-sim consumers (not twin-specific). Examples of acceptable:
- Accessor methods for dyno, transmission, vehicle state (already identified)
- New constraint types (TorqueConverter, SpeedFollower) that are generic physics primitives
- New transmission class (AutomaticTransmission) as counterpart to manual Transmission

Unacceptable without discussion:
- Hardcoded twin behavior
- Special-case code paths for OBD2 data format
- .mr script syntax changes (defer — C++ API suffices)

---

## Decision 6: Testability via Mock Telemetry Harness (2026-04-24)

**Context**: How do we test without a real Tesla?

**Decision**: Build a `MockTelemetryProvider` that replays recorded Tesla drives (CSV files). This is part of the twin's test infrastructure, not production code. It allows deterministic, repeatable acceptance tests for acceleration, deceleration, cruising scenarios.

Additionally: exploit `SineSimulator` for predictable audio-based tests. No random noise in red phase tests.

---

## Decision 7: Dyno as Load Parameter, Not Just RPM Control (2026-04-24)

**Context**: User insight: dyno mode could take a **load torque** parameter rather than RPM target. Could we compute net drivetrain load torque from OBD2 data and feed it to dyno?

**Status**: **Needs physics analysis**. Hypothesis: If we know wheel torque (from acceleration + mass + drag) and gear/diff ratios, we can compute crankshaft torque. Dyno could be switched to "load mode" where it applies that torque instead of controlling RPM. This would let physics determine RPM naturally.

**Open question**: Does `Dynamometer` only mode-lock RPM, or can it apply a programmable torque curve? Requires reading `dynamometer.cpp` constraint formulation.

**User confirmed (2026-04-24)**: Build both strategies in parallel and A/B test with objective metrics. Dual-track validation, not sequential pivot. This de-risks the hypothesis.

---

## Decision 16: Gear Change Frequency — Event-Driven (2026-04-24)

**Context**: Call `setGear()` every 60Hz bridge tick, or only when gearbox algorithm decides a shift?

**Decision**: **Event-driven only**. The gearbox model tracks its state and emits a change only when shift decision is made. Bridge calls `setGear(newGear)` once per shift event.

**Rationale**: Sufficient, simpler, easier to test (assert exact call count). Harmless redundancy of 60Hz calls not needed.

---

## Decision 17: MVP Clutch Model — Locked Only (2026-04-24)

**Context**: Should Phase 1 model clutch slip (variable pressure) or just locked (pressure=1.0)?

**Decision**: **Locked clutch only** (`pressure=1.0` always). Clutch disengagement only during shift events. Torque converter slip modeling deferred to later phase.

**Rationale**: Simpler for proving concept. Distinguishes side effects. Can toggle via `--clutch-model=locked` flag for isolation testing.

---

## Decision 18: Phase 1 Gearbox Depth — Simple RPM Thresholds (2026-04-24)

**Context**: Simple shift curves vs torque-based ZF algorithm?

**Decision**: **Simple RPM-threshold gearbox** for Phase 1:
- Upshift: `rpm > idle + (redline-idle) × (0.45 + 0.55×throttle)`
- Downshift: `rpm < idle + (redline-idle) × (0.25 + 0.20×throttle)`
- Two separate curves (accel vs decel) for hysteresis
- Kickdown on rapid throttle increase (>0.4 delta)

**Rationale**: Measurable, debuggable, can be cleanly swapped for `AutomaticTransmission` later without changing twin interface.

---

## Decision 19: Dyno Role — Primary Control Mechanism, Not Load Torque (2026-04-24)

**Context**: Could dyno take a computed load torque instead of RPM target?

**Decision**: **Dyno used as RPM controller in DynoAssistedTwin** (speed-hold mode). Load torque input via dyno is attractive but depends on whether the constraint supports programmable torque vs velocity bias. Await physics analysis. If `Dynamometer` can't apply arbitrary torque without also steering velocity, the simpler model is: compute target RPM that corresponds to anticipated load, set that.

**Acceptance test**: Dyno-led RPM tracks a step-changed target within 50 ms with <2% overshoot (tunable via ks/kd).

---

## Decision 8: Gear Selection Works at 60Hz Bridge Rate (2026-04-24)

**Context**: Concern that 60Hz bridge might be too slow for 10kHz physics-based shifting.

**Resolution**: The bridge calls `simulator->update(dt)` 60 times/sec, which internally runs 100,000+ physics steps/sec. Calling `simulator->changeGear()` from the bridge at 60Hz is **fine** — it modifies transmission state immediately, which the physics solver uses on the next sub-step. No problem.

---

## Decision 9: Upstream engine-sim changes limited to additions (2026-04-24)

**Context**: Must not break existing engine-sim behavior.

**Decision**: All engine-sim modifications are **additive only**:
- Add new methods to `Simulator` (setDynoTargetSpeed, setGear, setClutchPressure, getRpm)
- Add new classes (AutomaticTransmission, TorqueConverter, SpeedFollower)
- Never modify existing class behavior or .mr semantics

---

## Decision 10: Quality Gate — SOLID/DRY Critic Agent (2026-04-24)

**Context**: User wants a dedicated critic before any commit.

**Decision**: An **Architecture Critic** agent is on the team. All implementation plans and code changes must be reviewed and approved by this agent before any commit. The critic enforces:
- SRP, OCP, LSP, ISP, DIP
- DRY (no duplication across layers)
- TDD compliance (red-green-refactor, tests compile in red phase)
- No YAGNI bloat

**Status**: Agent spawned; part of the review workflow.

---

## Decision 11: Separate `vehicle-twin` Module (2026-04-24, revised)

**Context**: Where does the twin code live? Initial thought was vehicle-sim or bridge.

**Decision**: Create a new `vehicle-twin` CMake target that both vehicle-sim and bridge depend on. The twin is pure C++ domain logic with zero platform dependencies. It defines interfaces: `ITwinModel`, `IGearbox`, `IClutch`, `IWheel`. Implementations live here too.

**Revised ownership**:
- vehicle-sim: BLE transport + signal parsing only. Outputs `UpstreamSignal`. No twin code.
- bridge: `IInputProvider` implementations, audio pipeline, simulation orchestration only. Consumes `ITwinModel`.
- vehicle-twin: Twin models that map EV telemetry → ICE commands. Shared by both.

**Rationale**: Cleanest SRP separation each project owns its layer, twin is reusable, swap-able OCP component.

---

## Decision 12: Dual-Mode Strategy Pattern (2026-04-24)

**Context**: Must evaluate both Dyno-driven and Physics-driven RPM control without runtime conditionals.

**Decision**: Define `ITwinStrategy` interface with `TwinOutput update(const UpstreamSignal&, double engineRpm, double dt)`. Two implementations: `PhysicsDrivenTwin` (uses clutch+drivetrain), `DynoAssistedTwin` (uses dyno speed-hold). The bridge injects the strategy via DI. Switch via config file, not `if` statements.

**Rationale**: OCP — adding a new strategy never modifies existing code. Testable in isolation.

---

## Decision 13: Throttle Input Semantics — Instant On/Off (2026-04-24)

**Context**: CLI keyboard throttle should mimic a spring-loaded pedal: hold key = pedal at position, release = instantly 0%. No artificial ramp.

**Decision**: For interactive CLI testing, keys `1-0` set throttle to 10%-100% instantly while held; `Space` is 0%. Releasing any key snaps throttle to 0%. The flywheel inertia will determine RPM rise/fall rate naturally. This is testing ergonomics only; the EV twin will receive un-ramped throttle from vehicle-sim.

---

## Decision 14: Acceptance Criteria — Objective First, Subjective Second (2026-04-24)

**Context**: "How does it sound?" is not a testable criterion.

**Decision**: Primary acceptance is **quantitative**:
- RPM error vs road-speed-derived RPM < 5%
- Gear shift timing within ±200 ms of reference data
- No hunting (>3 shifts in 10s)
- Audio spectral fingerprinting match >85% to reference recording (later)

Subjective listening is a **secondary validation** after objective pass. Not a gating criterion.

---

## Decision 15: Test Isolation via Mode Switches (2026-04-24)

**Context**: To isolate side effects during testing.

**Decision**: Provide CLI/runtime flags to individually enable/disable features:
- `--clutch-model=locked|variable|off` (locked = pressure=1.0 always, variable = dynamic slip, off = disengaged)
- `--shift-model=simple|torque-based` (simple = RPM thresholds, torque-based = ZF algorithm)
- `--rpm-mode=physics|dyno` (select ITwinStrategy)

These keep test matrices independent and deterministic.

---

## Outstanding Unknowns (Requiring Research)

| Unknown | Why it matters | Research needed |
|---------|----------------|-----------------|
| Clutch constraint behavior at max pressure | Determines if physics-driven RPM tracking works | Read `clutch_constraint.cpp` behavior at limit |
| Does dyno mode produce realistic loading sound? | Determines if simple dyno path is viable | Audio spike: sweep dyno-controlled RPM, record WAV, listen |
| Can wheel torque be computed from OBD2? | For load-based dyno control | Research Tesla torque/power signals, OBD2 PID standards |
| How does ZF automatic transmission decide shifts? | determines gear box model complexity | Web search for ZF shift algorithm papers, SAE docs |
| What telemetry does Tesla BLE provide? | Determines twin input richness | Read vehicle-sim BLE signal parser, Tesla CAN DBC research |
| What are typical auto transmission shift curves? | Determines if simple RPM thresholds suffice or need torque-based | Gearbox researcher gathers shift point data by vehicle class |

---

## Conversation Summary (2026-04-24)

**Initial Request**: Map real Tesla EV telemetry → realistic ICE vehicle sound via engine-sim. Need to understand what to model, what engine-sim provides, and where to place code.

**Key Constraints**:
- Only modify engine-sim if changes are generally useful
- SOLID, DRY, TDD throughout
- Keep .claude/ archives of research
- Not to start coding yet — finish planning first

**Discoveries Made**:
- engine-sim has no automatic transmission (all manual)
- Dyno mode exists: spring/damper constraint that steers crankshaft RPM
- Clutch constraint exists, disengaged by default in bridge
- Vehicle mass body models drivetrain inertia
- Sound comes from combustion physics, not just RPM value
- Throttle directly controls torque production

**Strategy Evolved**:
Initial thought: "Set RPM directly via dyno."
Refined: Let RPM emerge from throttle + clutch + drivetrain physics if possible. Dyno as fallback or load supplement.

**Architecture Direction**:
Components (to be interface-defined):
- `ITwinModel` (overall) composed of `IGearbox`, `IClutch`, `IWheel`
- `IVehicleModel` in vehicle-sim OR `ITwinController` in bridge
- `TelemetrySource` abstraction (OBD2, mock, recorded)
- `IEngineController` adapter (bridges twin → engine-sim)

**Testing Approach**:
- Real-time acceleration scenario: 0-100 km/h with gear shifts
- Deceleration scenario: lift-off, engine braking, downshifts
- Steady-state cruising
- Objective metrics: RPM error, shift timing, no hunting

**Next Research Spikes Needed**:
1. Physics spike: Does clutch-lock produce correct equilibrium RPM?
2. Torque computation: Can we derive wheel torque from OBD2?
3. ZF autobox algorithm research
4. Tesla OBD2/BLE signal inventory
5. Audio spike: Does dyno-controlled engine sound realistic under load?
