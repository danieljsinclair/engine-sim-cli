# Vehicle-Driven Start/Stop (VehicleStart API) — Implementation Plan

Repo: `~/vscode/engine-sim-app/engine-sim-cli` (submodule, branch `feat/startStop` @ this commit)
Bridge submodule: `engine-sim-bridge`, branch `feat/startStop` @ 7a83baf (phase-2 semantics fixes — see below)

> **Doc status:** refreshed 2026-08-14 to reflect the **brake-light architecture** discovery.
> The earlier version of this doc predates it: it assumed a readable `DI_brakePedalState`
> level and a `brake_percent` CSV column, with door-open as the preferred stop signal.
> All of that is superseded below. Sections carried over unchanged (signal evidence,
> test matrix) are marked **[unchanged]**.
>
> **Phase-2 refresh (2026-08-29):** the three semantics gaps found by phase-1 E2E on the
> real captures are fixed in bridge `7a83baf` and reflected below — twin self-start (the
> twin now defaults ignition OFF and takes the level from the controller via
> `IVehicleControlSink`), the drive-since-start stop gate (the deferred PARK-after-motion
> item, resolved via gear history), and latch release on brake release ALONE. Door/exit-
> vehicle remains unwired in v1 by design (see the door section).
>
> **Phase-3 refresh (2026-08-30, owner request):** the STOP trigger is now
> **edge-triggered on brake RELEASE in park** instead of the brake-light level. Owner,
> verbatim: *"BRAKE(light)+PARK cuts ignition, however, I think it should be brake
> \*released\* in park, otherwise, it stops when you go to put your foot on the brake to
> select first gear."* The press (level) stop fired the instant a driver pressed the
> brake in PARK intending to select a gear and drive off; the cut now fires on the
> brake-light ON→OFF transition while RUNNING in PARK. Implemented on bridge branch
> `feat/startstop-release-cut`; all rules below are updated.

## Goal

When running `--live-telemetry` against the real car, the engine-sim's ignition and
starter are driven by the car's own signals instead of the UI:

- **START** — brake-light on, OR forward/reverse gear selected (not P)
- **STOP**  — brake-light **released** (ON→OFF edge) while in PARK and RUNNING, only
  after a drive gear (D/R) has been selected since the current engine run started
  (drive-since-start gate — see Rules). A brake **press** in PARK never cuts ignition
  (the driver may be about to select a gear); if the driver shifts out of PARK before
  releasing the brake, no stop occurs (that is a drive-off).
- **Crank aesthetic** — gear-initiated start is **instant** (starter + ignition same
  frame); brake-initiated start engages the starter immediately and ignites after a
  tunable delay (default 0.5 s, per-vehicle later). Selecting D/R while the delay is
  pending ignites immediately (safety: the engine must not finish cranking after the
  car is already moving).

Manual control (CLI keyboard / iOS UI) keeps a parallel path that feeds the *same*
decision layer — see "Brake signal architecture" below.

## Brake signal architecture (canonical design — 2026-08-14)

`brakeLight` is the **canonical** start/stop + display signal. It is **never** a
physics input.

- The real car **does** have a brake *level*, but it is **not readable** from our
  capture tap. So `--live-telemetry` gets the honest **degraded view**: the lamp,
  taken from the `brake_light` CSV column (`VCLEFT_brakeLightStatus`, 0x3E2,
  bit 0 len 2 — enum 0=LIGHT_OFF / 1=LIGHT_ON / 2=FAULT / 3=SNA; see
  [[brake-pedalstate-never-on]]).
- `--interactive` **simulates the full chain like the car**: keyboard `B` produces a
  `brakeLevel` (physics), and the **light is DERIVED from that level at ONE derivation
  point** — inside `SimulationLoop`, *before* `applyStartStopDecision`. There is exactly
  one place where level → light happens.
- **The two paths are the same car observed at different fidelities, NOT redundancy.**
  The consumer (`VehicleStartController`) sees only `brakeLight + gearSelector`; it
  never knows or cares whether the light came from live telemetry or from the `B` key.
  Owner (verbatim, non-negotiable): *"Start/Stop is based off the brakeLight event. The
  brakeLight event is caused by EITHER brakeLevel (today only the B key in interactive)
  or by live telemetry. The consumer shouldn't know. No difference live vs interactive."*

### Console display

- Format: `[Gas: 0% B/-]` — a **red `B`** when the brake light is on, a **plain `-`**
  when off.
- **No numeric `B:x.x` anywhere.** Brake is **not** shown inside the Gear bracket.
- This matches the committed CLI behaviour (`b39b5b2`, `a0095e4`).

## Confirmed signal evidence (from real captures) — [unchanged]

| Signal | Source | Status |
|---|---|---|
| `DI_gear` | 0x118 bit 21, len 3, `@1+`, DLC 8 | **PROVEN** — clean D/R/P transitions in GearProbe.raw.txt |
| `DI_brakePedalState` | 0x118 bit 19, len 2, `@1+`, DLC 8 | **DEAD** — always INVALID(2) in every capture (see below); never use it |
| `VCLEFT_brakeLightStatus` | 0x3E2 (BO_ 994 "ID3E2VCLEFT_lightStatus"), bit 0 len 2, enum 0/1/2/3 | **PROVEN** — 46 ON / 99 OFF, 19 transitions in UpLeckHill decode; ~1 Hz pedal proxy |
| `DI_epbRequest` | 0x118 bit 44, len 2 | PROVEN fires PARK/UNPARK (redundancy only, not used v1) |
| door open | — | **NOT PROVEN — see below** |

### `DI_brakePedalState` is dead — [superseded]

The pre-discovery design relied on `DI_brakePedalState` (decoded `brake_percent`).
Investigation showed it is **always INVALID(2) / 2.00** across every real capture
(100% of 2540 UpLeck frames; never 1=ON). The `2.00` value is a wake/drive-mode flag,
not pedal position. **Brake level is therefore not readable from our tap**, which is
*why* the design pivoted to the brake-light signal (which IS present).

- `brake_percent` column + the `DI_brakePedalState` mapping were **dropped**, not
  renamed (owner decision). Old capture files keep the column; the header-driven CSV
  parser simply ignores it — no replay break.
- The awake/`drive_ready` state the enum actually carries was identified as an
  obtainable honest data point, but is deliberately **NOT wired** — YAGNI for now;
  wake continues to be observed via throttle going non-blank.

### Door status: UNRESOLVED (v1 ships without it) — [unchanged]

- `anyDoorOpen` IS defined in the loaded party DBC, but on message **785 (0x311)**
  as `28|1@0+` — note **`@0` = BIG-endian (Motorola)**, not little-endian.
- In every capture 0x311 arrives as **2-byte frames only**; the DBC expects a
  longer message. The signal is therefore not extractable from what we receive.
- 0x2E1 was briefly believed to be the door message. It is NOT: a changing-bits
  analysis of SimpleDoor.raw.txt shows **58 of 64 bits change** frame-to-frame
  with unrelated payloads (`394d17ff1f291800`, `fb196ecbb2580b00`, ...). That is
  multiplexed or encrypted VCSEC-range traffic, not door status. An earlier
  claim of "3 clean door cycles" on 0x2E1 bit 28 was an artefact of reading one
  bit out of a wholly-changing payload.
- SimpleDoor.raw.txt cannot settle it either way: with ~36 frames per message and
  **no recorded timestamps of the actual door events**, a search for binary
  door-like bits yields **1471 candidates**. Unfalsifiable without ground truth.
- The decoded `SimpleDoor.csv` schema has **no door column** — confirming the
  decoder never extracted door state.

**To resolve:** a capture with the door-event times written down, then correlate
transitions against those timestamps. Not a wiring problem — a ground-truth problem.

**Phase-1 E2E confirmation (2026-08-29):** neither real capture (coldStart,
UpLeckHillWithKickdown) carries **any 0x311 frames at all**, and the bridge ships
**no door seam** (`door_open` is decoded by vehicle-sim but nothing in the bridge
consumes it). Door/exit-vehicle therefore remains **unwired in v1 by design** — it
stays a documented future seam; do not implement it on the strength of these
captures.

Rejected after investigation: `DI_gearRequest` (not on our tap — DI powertrain
bus only), `0x229 GearLever` (level-held; no repeat-press event exists),
throttle-blank-as-sleep (no such edge occurs in 1.6M frames).

## Design — two bits, a gate bit and an edge bit, no state machine

```
engineOn   : has the sim been started
stopLatch  : set when a park release-edge stop fires; blocks restart until brake releases
driveSinceStart : a drive gear (D/R) has been selected during the current engine run
                 (seeded by a gear-initiated start; reset by every stop and new run)
brakeWas   : previous frame's brake light (its ON->OFF edge is the stop TRIGGER)
```

Rules (evaluated per frame in `VehicleStartController::update` from the canonical
`brakeLight` boolean + `gearSelector`):

```
brakeReleased = brakeWas && !brake          [edge; brakeWas advances every tick]
if (brakeReleased && gear == PARK && running && driveSinceStart)
                                       -> ignition off, stopLatch = true
else if (stopLatch && !brake)          -> stopLatch = false            [release]
else if (!engineOn && !stopLatch && (brake || gear == D/R)) -> VehicleStart()
```

`running` above is `engineOn && !crankPending` — a brake release during the
brake-initiated crank delay is part of the start, not a stop.

**Why the edge trigger (phase-3):** the level rule (`brake && PARK`) cut ignition the
instant a driver pressed the brake in PARK *intending to select a gear and drive off* —
the engine died under their foot before the selector ever left P. The release edge is
the human "I am parked and done" event: hold the brake, select PARK, release, walk
away. Pressing the brake in PARK (to select D/R) is now invisible to the stop rule,
and a shift out of PARK before the release lands the edge outside PARK — a drive-off,
no stop.

`stopLatch` is retained: the stop still sets it and START stays blocked while it
holds, preserving the restart discipline across the trigger change.

**Release is brake release ALONE** (`stopLatch && !brake`) — NOT "brake released AND
out of a drive gear". While a drive gear is selected, the very tick that releases the
latch also satisfies START (gear trigger) and restarts the engine: releasing the brake
in DRIVE is a drive-off, not a stay-off. (Phase-1's coldStart proved the stricter rule
wrong: the brake was released while already in D, the latch never cleared, and the
engine stayed dead for the rest of the capture.)

**The drive-since-start gate** resolves the deferred *PARK-after-motion* work item
using gear-selection history instead of motion history (no speed/odometry state
machine). It is what keeps the release edge from firing on the brake that just
PERFORMED a start: a brake-initiated start in PARK (0.5 s crank) followed by the
pedal being released IS an ON→OFF edge in PARK while running — without the gate it
would stop the engine it just cranked (the phase-3 twin of the phase-2 self-stop).
With the gate: the release-edge cut applies only once the driver has selected D/R
since the engine run started, which is the PARK-after-drive intent. A gear-initiated
start seeds the gate (the selector IS in D/R), so the release cut is armed from the
first drive-off.

Door is designed in as a **level inhibit** and left as a seam for when the signal
is proven (NOT yet wired):

```
if (doorOpen) -> ignition off, starter off    [level inhibit, NOT YET WIRED]
```

`driverPresent` is likewise an unproven future seam — **STOP = brake release in
PARK only** in v1 (phase-3; was brake+P level through phase-2).

## VehicleStart() sequencing

```
VehicleStart(driveSelected):
    starter on
    if driveSelected:
        ignition on SAME FRAME (instant gear start)
    else:                                  [brake-only start]
        schedule ignition on after crankDelayS (default 0.5, accumulated dt)
        if gear becomes D/R while pending -> ignition on immediately, cancel timer
```

The twin already owns `crankingTimerS_`, `setIgnition/getIgnition` and a
`TwinState`; `BridgeSimulator`/`ManualTwin` expose `setStarterMotor(bool)`.
This is a sequencer over existing mechanism, not new engine behaviour.

### Twin ignition authority (phase-2, bridge 7a83baf)

The twin's `ignitionOn_` now **defaults OFF**: an uncommanded twin never
self-starts. On live paths `VehicleStartController` owns EVERY start, and its
ignition LEVEL is pushed into twin-based providers each frame through the new
`input::IVehicleControlSink` seam (`LiveTelemetryProvider` implements it;
`SimulationLoop::applyStartStopDecision` is the only writer). The twin gates all
of its processing — throttle smoothing, gearbox, cranking lifecycle — on that
level, so the push is what lets a legally started engine process throttle, and
its absence before the first vehicle-control opinion is what stops the old
t=0.44s self-start (UpLeckHill ran its engine before the first brake frame at
t=10.04 s). Providers that own their ignition (demo keyboard, manual twin) and
non-twin providers (keyboard, replay) do not implement the seam and are
untouched.

## DRY rework — single invocation site (bridge df32804, 2026-08-14)

Before this rework, start/stop was split across a `StartStopInputAdapter` plus a
live-only wrapper in `CLIMain`, with the interactive `B` key feeding a *separate*
path — a hack that made live and interactive genuinely different.

The DRY rework **collapses all of that into ONE invocation site**:

- `StartStopInputAdapter` and its `CLIMain` live-only wrap are **deleted**.
- `SimulationLoop::step()` calls `applyStartStopDecision(...)` exactly once, after
  the **canonical light derivation** (level → `brakeLight`) and **before**
  `applyCrankingDecision`. Every mode (keyboard, demo, replay, live) traverses this
  single site — proving "no difference live vs interactive".
- `CrankingController` **remains the sole actuator authority** downstream. The
  start/stop layer is observer-only: `VehicleStartController` writes ignition/starter
  LEVELS onto an `ObserverActuator`; the loop flattens those into `EngineInput`
  (`.ignition` / `.starterButton`) which `CrankingController` then consumes.
- The starter LEVEL is flattened to a **single-frame pulse** via edge detection
  (`SimulationLoop::starterPulseFromLevel` + `prevStarterLevel_`), because
  `CrankingController::engageStarter` toggles on a held-high button.

**Proven full cycle** (real UpLeckHillWithKickdown capture through gate-built
binaries): Stopped → Cranking (ignition on same frame for gear start) → Running →
Stopping → Stopped-latched on brake-light+P. Gate 40/40 CLI + 76/76 bridge green,
CLI Sonar 0 new issues.

**Phase-2 E2E re-proof (2026-08-29, bridge 7a83baf, both real captures end to end):**
- *coldStart*: no start before the brake; brake+P at t=1.45 s cranks 0.5 s, ignition
  at t=1.95 s, **Running continues while brake+P is still held** (previously the
  engine self-stopped 2 frames after ignition and stayed dead 135 s); P→D drive-off
  runs the engine continuously until the capture's real final brake+P stop.
- *UpLeckHill*: **no start before real input** (previously self-started at t=0.44 s);
  first start is the instant gear start on the real P→D shift; final brake+P stops
  same-frame and stays latched while the brake is held.
76/76 bridge suites green, coverage 83.1%, Sonar clean.

**Phase-3 note (2026-08-30):** the phase-1/phase-2 E2E narratives above describe the
LEVEL stop (`brake+P`), which phase-3 supersedes. Two consequences on the captures:
the final stop now lands on the brake RELEASE in PARK (not the press), and a capture
that ends with the park brake still held and never released no longer stops the engine
before end-of-input — that is the requested semantics, not a regression. Capture-level
E2E re-proof is pending the owner's next road test; the unit + loop suites
(`VehicleStartControllerTest`, `SimulationLoopVehicleControlsTest`) cover the new
trigger matrix.

**Capture-end note (2026-08-30, same branch):** end-of-input now terminates the whole
CLI, not just the ignition: on the live stdin path the provider disconnects as soon as
the stream and its lookahead buffer drain (owner decision — "EOF = immediate
termination", no grace window; a start still mid-crank in the last ~0.5s of a capture
is consciously cut short). `--end-at` runs report the bound as their stop reason
(`StopReasonReporter`), not the full trace length.

## Work items — [status: implemented on feat/startStop]

### 1. vehicle-sim — carry the new signals  — **DONE (escli.vehicle-sim 7237c8f)**
- `VCLEFT_brakeLightStatus` (0x3E2 overlay in `resources/dbc/Model3CAN.dbc` +
  joshwardell submodule at `external/model3dbc`) added declaratively.
- `brake_light` CSV column **replacing** the dead `brake_percent`.
  (`brakePercent`/`DI_brakePedalState` dropped, not renamed.)
- `door_open` column retained as an unproven seam (not yet consumed by the
  start/stop controller).

### 2. bridge — carry the light to the twin  — **DONE (2788213, 7d75e77, df32804)**
- `UpstreamSignal::brakeLight` is `std::optional<bool>` (absent = no signal seen yet).
- `LiveTelemetryParser` maps `brake_light` from JSON; old headers still parse the
  legacy fields without break.
- `CsvTelemetryParser` maps `brake_light` (and `gear`/`gear_selector`).

### 3. bridge — the VehicleStart sequencer  — **DONE (2788213 — df32804)**
- `VehicleStartController` (SRP: owns the two bits + crank delay timer + injected
  `ILoopClock` so the 0.5 s delay is testable without sleeping).
- `applyStartStopDecision` in `SimulationLoop::step()` is the sole caller — drives
  existing `setIgnition(bool)` and the starter path via the observer + pulse helper.
- Manual path (`ManualTwinProvider`) is **untouched**.

### 4. Config — **DONE**
- `SimulationConfig::startStopCrankDelayS` default 0.5, overridable per profile.

## Live telemetry gear contract (2026-08-14)

`SimulationLoop::applyStartStopDecision` is the **single** decision site and it reads
`state.engineInput.gearSelector` (`SimulationLoop.cpp:51`) together with the canonical
`brakeLight`. For gear-initiated **instant** starts to work, live mode MUST populate
`engineInput.gearSelector` from the decoded gear.

- The **CSV `--stdout-csv` live pipe** already satisfies this: `LiveTelemetryProvider`
  sets `signal.gearSelector = csvGearSelector()` and `signal.brakeLight =
  currentSample_.brakeLight` (`LiveTelemetryProvider.cpp:126-127`), which the loop
  flattens into `engineInput`.
- The **JSON network live path** currently does **NOT**: `LiveTelemetryParser::parse`
  extracts only throttle/speed/acceleration/brake/motor_torque and never writes
  `gearSelector` or `brakeLight` into `UpstreamSignal` (`LiveTelemetryParser.cpp:13-43`).
  On that path `signal.gearSelector` stays at its default `NEUTRAL` and `brakeLight`
  is unset — so **gear-initiated instant starts are dead on the JSON network path**.
  This is a **known regression being fixed in parallel** (bridge commit pending).

**Contract (non-negotiable):** the consumer (`VehicleStartController`) must never
distinguish live vs interactive. *Both* paths must supply `brakeLight + gearSelector`
identically. If a live path fails to populate `gearSelector`, gear-start silently
degrades to "never starts from gear" until the parser is fixed — that is a provider
bug, not a design ambiguity.

## Spec decisions (owner, 2026-08-14)

- **gear-initiated start = instant** (starter + ignition same frame).
- **brake-initiated start = 0.5 s delayed ignition** (McLaren crank aesthetic).
- **STOP = brake RELEASE (ON→OFF edge) in PARK, gated on drive-since-start**
  (a D/R selection in the current engine run — see Rules). Owner, 2026-08-30:
  the press must not cut, or the engine dies when the driver brakes to select
  first gear. Door (unproven) and `driverPresent` (unproven) are future seams,
  deliberately not wired in v1.
- **Latch release = brake release alone**; in a drive gear the same tick restarts
  via the gear trigger (drive-off).
- `drive_ready` (what the old `brake_percent` enum actually encoded) documented
  **YAGNI** — not wired.
- `brake_percent` **dropped, not renamed.**
- Display: `[Gas: 0% B/-]` (red `B`, plain `-`); no numeric `B:x.x`; no brake
  character in the Gear bracket.

## Testing (TDD, test-architect authors first) — [matrix updated for phase-3]

Per project rules the implementer does NOT write the tests. Test scenarios:

- door open while running -> ignition off  *(seam, not yet wired)*
- door open blocks start (brake pressed with door open -> stays off)  *(seam)*
- brake-light press-and-hold in PARK **with drive-since-start** -> does NOT stop
  (the press is not the trigger; Running continues indefinitely while held)
- brake-light **released** in PARK **with drive-since-start** while RUNNING ->
  ignition off on the release frame, latch set, **exactly once** (later idle
  frames neither restart nor re-fire)
- brake-light **released** in PARK **with no drive-since-start** -> does NOT stop
  (the brake that just performed the brake-initiated start in P must survive its
  own release; also the still-held brake: Running continues)
- shift out of PARK before the brake release (incl. the gear change and the
  release arriving on the SAME tick) -> no cut, still RUNNING
- drive-since-start gate re-arms per engine run (a fresh brake-start in P cannot be
  stopped by its own release until D/R is selected again)
- restart path unchanged: after the release-edge stop, a brake press cranks a new
  start and a D/R selection restarts instantly; latch **clears on brake release
  ALONE** and the clearing+gear tick restarts via the gear trigger (drive-off)
- brake-light alone (no door, no latch) -> VehicleStart (0.5 s delay)
- D or R selected -> VehicleStart **instant** (same-frame ignition); P alone does NOT start
- crank delay: starter on at t=0, ignition still off at t=0.4, on at t=0.5 (fake clock)
- D/R during pending delay -> ignition immediately, timer cancelled
- twin does NOT self-start without an ignition command (any duration of valid
  telemetry); first command starts it
- loop commands twin ignition through `IVehicleControlSink`, and NOTHING is
  commanded before the first vehicle-control opinion; the commanded level stays
  true through a PARK brake press and drops only on the release
- manual path unaffected: setIgnitionRequested/setStarterRequested still independent

Real code under test, no truisms, no live-data dependence, injected clock.
See `test/input/VehicleStartControllerTest.cpp`,
`test/simulation/SimulationLoopVehicleControlsTests.cpp`.

## Open blocker before coding — [RESOLVED]

The original blocker (`VCFRONT_anyDoorOpen` DBC definition) is moot: door is no
longer in the v1 start/stop path. The live brake signal uses `VCLEFT_brakeLightStatus`
(0x3E2), which **is** present in the joshwardell DBC and our captures.

## Out of scope for v1

- Door-open stop (needs proven door signal — ground-truth capture problem, not
  wiring; phase-1 confirmed no capture carries 0x311 and the bridge has no door
  seam — it stays a documented future seam).
- `driverPresent` stop (unproven).
- ~~PARK-after-motion redundancy (needs motion history = real state machine).~~
  **Resolved in phase-2 (bridge 7a83baf)** as the drive-since-start gate —
  gear-selection history, not motion history; see Design.
- Driver-vs-passenger door discrimination (needs clean re-capture; nice-to-have).
- iOS UI wiring (API shape defined here; UI work separate).
- Aggregate-DBC generator: build-time merge from submodules with conflict=hard-error
  (joshwardell splits `DI_torqueActual` and renames steering, so wholesale switch is
  blocked; the 0x3E2 overlay is the interim).
