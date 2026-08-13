# Vehicle-Driven Start/Stop (VehicleStart API) — Implementation Plan

Repo: `~/vscode/engine-sim-app/engine-sim-cli` (submodule, branch `master` @ f7281be, synced with origin)

## Goal

When running `--live-telemetry` against the real car, the engine-sim's ignition and
starter are driven by the car's own signals instead of the UI:

- **START** — brake pressed, OR forward/reverse gear selected (not P)
- **STOP**  — any door open, OR (brake + PARK together)
- **Crank aesthetic** — starter engages immediately; ignition follows after a
  tunable delay (default 0.5 s, per-vehicle later). Selecting D/R while the delay
  is pending ignites immediately (safety: engine must not start after car moves).

Manual control (CLI keyboard / iOS UI) keeps today's strict ignition/starter
separation. Both paths coexist: the UI can still operate the sim while live
telemetry runs.

## Confirmed signal evidence (from real captures)

| Signal | Source | Status |
|---|---|---|
| `DI_gear` | 0x118 bit 21, len 3, `@1+`, DLC 8 | **PROVEN** — clean D/R/P transitions in GearProbe.raw.txt |
| `DI_brakePedalState` | 0x118 bit 19, len 2, `@1+`, DLC 8 | **PROVEN** in party DBC; same frame as gear (atomic read) |
| `DI_epbRequest` | 0x118 bit 44, len 2 | PROVEN fires PARK/UNPARK (redundancy only, not used v1) |
| door open | — | **NOT PROVEN — see below** |

### Door status: UNRESOLVED (v1 ships without it)

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

Rejected after investigation: `DI_gearRequest` (not on our tap — DI powertrain
bus only), `0x229 GearLever` (level-held; no repeat-press event exists),
throttle-blank-as-sleep (no such edge occurs in 1.6M frames).

### Data caveat

`brake_percent` in the decoded CSV shows `2.00`, which is `DI_brakePedalState`
enum **2 = INVALID**, not "2 %". The boolean `brakePressed` must be derived as
`== 1 (ON)`, treating 0 (OFF) and 2 (INVALID) as not-pressed.

## Design — two bits, no state machine

```
engineOn   : has the sim been started
stopLatch  : set when a P+B stop fires; blocks restart until brake releases
```

Rules, evaluated per telemetry frame (v1 — door omitted, see above):

```
if (brake && gear == PARK)           -> ignition off, stopLatch = true
else if (stopLatch && !brake)        -> stopLatch = false           [release]
else if (!engineOn && !stopLatch && (brake || gear == D/R)) -> VehicleStart()
```

Door is designed in as a **level inhibit** and left as a seam for when the signal
is proven:

```
if (doorOpen) -> ignition off, starter off    [level inhibit, NOT YET WIRED]
```

Level inhibit is simpler than edge detection (no previous-state tracking) and
matches the owner's preference that the engine never runs with a door open.
`IDoorSource` (or an `optional<bool>` on the signal) keeps the seam open without
speculative runtime branches — absent door data must not synthesise a value.

`stopLatch` is required: without it, holding the brake after a P+B stop
immediately re-satisfies the START condition and restarts the engine underfoot.

## VehicleStart() sequencing

```
VehicleStart():
    starter on
    schedule ignition on after crankDelayS (default 0.5)
    if gear becomes D/R while pending -> ignition on immediately, cancel timer
```

The twin already owns `crankingTimerS_`, `setIgnition/getIgnition` and a
`TwinState`; `BridgeSimulator`/`ManualTwin` expose `setStarterMotor(bool)`.
This is a sequencer over existing mechanism, not new engine behaviour.

## Work items

### 1. vehicle-sim — carry the new signals
- `VehicleSignal::Params`: add `std::optional<bool> doorOpen`, `std::optional<bool> brakePressed`.
  (`brakePercent` stays as-is for the CSV column; `brakePressed` is the honest
  boolean from the 2-bit `DI_brakePedalState`, avoiding the overclaim noted in
  `DefaultVehicleConfigs.cpp`.)
- Accessors + equality + `VehicleSignalFactory` mapping (unmapped fields are
  silently dropped today — see `VehicleSignalFactory.cpp:79`).
- `DefaultVehicleConfigs::teslaModel3()`: add `{"VCFRONT_anyDoorOpen", "doorOpen"}`.
  **Blocker to verify first:** `resources/dbc/Model3CAN.dbc` must define
  `VCFRONT_anyDoorOpen` on 0x2E1. If absent, add the message/signal definition
  (bit 28, len 1, DLC 8) — evidenced by SimpleDoor decode.
- CSV: new `door_open` column in `CsvRowFormatter` + `CsvTelemetryRow`.

### 2. bridge — carry them to the twin
- `UpstreamSignal`: add `bool doorOpen`, `bool brakePressed`, `int gear`.
- `LiveTelemetryParser`: parse `door_open`, `brake_pressed`, `gear` from JSON.
  Note: current parser discards `extractDoubleRaw`'s bool return, so absent and
  zero are indistinguishable. New fields must not repeat that — track presence.
- `CsvTelemetryParser`: already maps `gear`/`gear_selector`; add `door_open`.

### 3. bridge — the VehicleStart sequencer
- New `VehicleStartController` (SRP: owns the two bits + crank delay timer).
  Injected clock (`ILoopClock` pattern already used by `SimulationLoop`) so the
  0.5 s delay is testable without sleeping.
- `LiveTelemetryProvider` drives it per frame; calls existing
  `setIgnition(bool)` and the starter path.
- Manual path (`ManualTwinProvider::setIgnitionRequested` /
  `setStarterRequested`) is **untouched**.

### 4. Config
- `crankDelayS` default 0.5, overridable per vehicle profile (tune later).

## Testing (TDD, test-architect authors first)

Per project rules the implementer does NOT write the tests. Test scenarios:

- door open while running -> ignition off
- door open blocks start (brake pressed with door open -> stays off)
- brake+PARK -> ignition off, latch set
- latch blocks restart while brake held; clears on brake release; then brake starts
- brake alone (no door, no latch) -> VehicleStart
- D or R selected -> VehicleStart; P alone does NOT start
- crank delay: starter on at t=0, ignition still off at t=0.4, on at t=0.5 (fake clock)
- D/R during pending delay -> ignition immediately, timer cancelled
- manual path unaffected: setIgnitionRequested/setStarterRequested still independent

Real code under test, no truisms, no live-data dependence, injected clock.

## Open blocker before coding

`VCFRONT_anyDoorOpen` must exist in the loaded party DBC. Verify; if missing,
add the definition (evidence: SimpleDoor.raw.txt, 0x2E1 bit 28, 3 clean cycles).

## Out of scope for v1

- PARK-after-motion redundancy (needs motion history = real state machine)
- Driver-vs-passenger door discrimination (needs clean re-capture; nice-to-have)
- iOS UI wiring (API shape defined here; UI work separate)
