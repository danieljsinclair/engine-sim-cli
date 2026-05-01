# VirtualICE Twin — Interface Contracts

## Purpose

This document defines **unambiguous acceptance criteria** for every module and inter-module interface in the VirtualICE Twin system. Each contract specifies method preconditions, postconditions, invariants, error handling, and latency budgets. These serve as the definitive reference for implementers, reviewers, and the architecture critic.

---

## 1. ITelemetrySource

**Responsibility**: Abstract acquisition of upstream vehicle data (OBD2, mock, recorded) and normalize to `UpstreamSignal`.

### 1.1 Interface Definition

```cpp
struct UpstreamSignal {
    double   timestamp;           // seconds since simulation start (monotonic)
    double   vehicleSpeedKmh;     // 0.0+ (ground speed)
    double   wheelSpeedKmh;       // 0.0+ (for slip calculation)
    double   throttlePercent;     // 0.0–100.0
    double   brakePedalPercent;   // 0.0–100.0
    double   clutchPedalPercent;  // 0.0–100.0 (0 = fully engaged, 100 = fully disengaged)
    int32_t  gear;                // -2=reverse, -1=neutral, 0=N, 1+=forward gears
    double   steeringAngleDeg;    // -540..+540
    double   lateralAccelG;       // G-force lateral
    double   longitudinalAccelG;  // G-force forward/brake
    bool     absActive;           // ABS intervention detected
    bool     tractionControlActive;
    double   batteryVoltage;      // 12V system voltage
    double   coolantTempC;        // °C
    std::vector<std::pair<std::string, double>> extendedMetrics;
};

struct TelemetrySourceError {
    enum class Code { None, DeviceDisconnected, ParseError, TraceFileCorrupt, Timeout, HardwareUnavailable };
    Code code = Code::None;
    std::string message;
};

class ITelemetrySource {
public:
    virtual ~ITelemetrySource() = default;

    // Blocking read; returns false on non-recoverable error (device unplugged, EOF)
    // Returns true + fills signal on success.
    virtual bool read(UpstreamSignal& signal) = 0;

    // Current error state (read-only, thread-safe query)
    virtual TelemetrySourceError getLastError() const = 0;

    // Reset error state after recovery
    virtual void clearError() = 0;

    virtual const char* getName() const = 0;

    // Non-blocking poll; if no data available yet, returns false
    virtual bool tryRead(UpstreamSignal& signal) = 0;
};
```

### 1.2 Contract — `read(UpstreamSignal&)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - Source is initialized (`Initialize()` returned `true`)<br>- `signal` is default-constructed (caller responsibility) |
| **Postconditions** | - On `true`: `signal` is fully populated with valid values<br>- On `false`: `signal` is unchanged; `getLastError()` returns non-`None` |
| **Invariants** | - `timestamp` is monotonic non-decreasing across calls (within same source instance)<br>- `vehicleSpeedKmh` ∈ [0, 500]<br>- `throttlePercent` ∈ [0, 100]<br>- `brakePedalPercent` ∈ [0, 100]<br>- `clutchPedalPercent` ∈ [0, 100]<br>- `gear` ∈ {-2, -1, 0} ∪ [1, 10] |  
| **Rate Guarantees** | - Target: 10 Hz (100 ms nominal)<br>- Tolerance: ±20 ms jitter acceptable<br>- Staleness: signal older than 500 ms is considered invalid (source error) |
| **Error Conditions** | - `DeviceDisconnected`: Physical device unplugged or connection lost<br>- `ParseError`: Malformed data from CAN/BLE<br>- `Timeout`: No data received within 1000 ms<br>- `HardwareUnavailable`: Driver/device not ready<br>→ All errors set `getLastError()` and return `false` on next `read()` |
| **Thread Safety** | - Concurrent calls to `read()` are serialized (implementation responsibility)<br>- `getLastError()` is lock-free read-safe |

### 1.3 Contract — `tryRead(UpstreamSignal&)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | Same as `read()` |
| **Postconditions** | - If data available: returns `true`, populates `signal`<br>- If no data: returns `false`, `signal` unchanged, no error set |
| **Latency Budget** | **Must not block** — return within 1 ms |
| **Use Case** | UI loops, high-frequency polling without blocking |

### 1.4 Concrete Implementations

#### OBD2TelemetrySource
- Communicates via serial/CAN adapter
- Implements OBD-II PID queries (0x0D = speed, 0x11 = throttle, etc.)
- **Failure mode**: Returns `DeviceDisconnected` on adapter removal
- **Buffering**: Internal 10-sample FIFO to smooth jitter

#### MockTelemetrySource (test seam)
- Deterministic function of time (no randomness)
- `vehicleSpeedKmh = f(timestamp)` from `ScenarioDefinition`
- **Guarantee**: Bitwise reproducible across architectures

#### RecordedTelemetrySource
- Reads CSV with header: `timestamp,speed,throttle,gear,...`
- **Error**: `TraceFileCorrupt` on malformed row
- **EOF handling**: Returns `false` after last row, sets `Timeout` error

#### DynoTelemetrySource
- TCP client to dyno controller
- Reads roller speed, load cell, throttle position
- **Rate**: 100 Hz capability, down-sampled to 10 Hz for output

---

## 2. IEngineController

**Responsibility**: Drive the engine-sim (`ISimulator`) — set throttle/gear/clutch and query engine RPM. Exclusive gateway to engine-sim from the twin.

### 2.1 Interface Definition

```cpp
struct DynoParams {
    double rollingResistanceCoeff = 0.015;
    double inertiaKgM2 = 50.0;
    double targetLoadN = 0.0;
    bool   isAbsorbing = true;
};

struct EngineSimStats {
    double currentRPM;
    double currentLoad;       // 0.0–1.0
    double exhaustFlow;       // m³/s
    double intakePressure;    // kPa
    double coolantTemp;
    double oilTemp;
    // ... additional fields
};

class IEngineController {
public:
    virtual ~IEngineController() = default;

    // ── Control primitives ─────────────────────────────────────────
    virtual void setThrottle(double position) = 0;          // 0.0–1.0
    virtual void setGear(int gear) = 0;                     // -2(R), -1(N), 0+ (1..6)
    virtual void setClutchPressure(double pressure) = 0;    // 0.0–1.0 (0 = disengaged)
    virtual void setDynoParams(const DynoParams& params) = 0;

    // ── Queries ─────────────────────────────────────────────────
    virtual double getEngineRpm() const = 0;            // instantaneous RPM
    virtual double getEngineLoad() const = 0;           // 0.0–1.0
    virtual double getExhaustFlow() const = 0;          // m³/s
    virtual EngineSimStats getStats() const = 0;        // full snapshot

    // ── Lifecycle ────────────────────────────────────────────────────────
    virtual bool initialize(const ISimulatorConfig& config) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    // ── Diagnostics ─────────────────────────────────────────────────────
    virtual const char* getName() const = 0;
    virtual void registerTelemetry(class ITelemetryWriter* telemetry) = 0;
};
```

### 2.2 Contract — `setThrottle(double)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `position` ∈ [0.0, 1.0] (clamped by implementation)<br>- Controller is initialized (`initialize()` returned `true`) |
| **Postconditions** | - Engine throttle input is updated within 1 ms<br>- Change is reflected in next physics tick (≤ 0.1 ms at 10 kHz sim rate) |
| **Invariants** | - No persistent state beyond throttle value<br>- Does not affect gear or clutch state |
| **Error Conditions** | - None (silent clamp to [0, 1] if out of range) |
| **Thread Safety** | - Safe to call from real-time audio thread (lock-free) |

### 2.3 Contract — `setGear(int)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `gear` ∈ {-2, -1, 0, 1, 2, 3, 4, 5, 6} (or as defined by vehicle)<br>- Transmission exists in simulator (not all engine configs have transmission) |
| **Postconditions** | - Transmission gear is changed within 2 ms<br>- If clutch is disengaged (`pressure < 0.1`), change is smooth<br>- If clutch is engaged, change may cause RPM discontinuity (realistic shift shock) |
| **Invariants** | - Gear state is queryable via `getStats().currentGear` (if exposed) |
| **Error Conditions** | - Throws `std::invalid_argument` if gear is out of supported range<br>- No-op (with warning) if no transmission present |
| **Latency** | **≤ 5 ms** for mechanical shift simulation (configurable) |

### 2.4 Contract — `setClutchPressure(double)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `pressure` ∈ [0.0, 1.0] (clamped)<br>- Transmission with clutch exists |
| **Postconditions** | - Clutch hydraulic pressure is updated<br>- Torque transfer capacity scales with `pressure × m_maxClutchTorque` (see `CLUTCH_PHYSICS_ANALYSIS.md`) |
| **Invariants** | - At `pressure = 1.0`, clutch may still slip if required torque > `m_maxClutchTorque` (finite capacity) |
| **Error Conditions** | - None (silent clamp) |

### 2.5 Contract — `getEngineRpm() const`

| Aspect | Requirement |
|--------|-------------|
| **Latency Budget** | **≤ 100 μs** (must be lock-free for audio thread) |
| **Freshness** | Value reflects state at last physics tick (≤ 0.1 ms old at 10 kHz) |
| **Thread Safety** | Atomic read, no blocking |

### 2.6 Contract — `setDynoParams(const DynoParams&)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `inertiaKgM2` > 0<br>- `rollingResistanceCoeff` ≥ 0 |
| **Postconditions** | - Dyno load model parameters updated within 1 ms<br>- Affects engine-sim's `m_dyno` constraint behavior |
| **Note** | Primary RPM control mechanism for twin (see evidence) |

### 2.7 Error Handling Policy

- **Initialization failures** (`initialize()`): Return `false`, set internal error state, do not throw
- **Invalid arguments** (`setGear` out of range): Throw `std::invalid_argument` **immediately** (programming error)
- **Runtime errors** (simulator not running): Log internally, no throw from getters (return last known value or 0)
- **Contract**: Never swallow errors silently; never disguise failure as success

---

## 3. ITwinStrategy

**Responsibility**: Core twin update algorithm — given current vehicle telemetry and engine RPM, compute next throttle/gear/clutch setpoints.

### 3.1 Interface Definition

```cpp
struct TwinOutput {
    double recommendedThrottle;    // 0.0–1.0
    int    recommendedGear;        // -2..N (desired gear)
    double clutchEngagement;       // 0.0–1.0 (0=fully engaged)
    bool   shiftRequested;         // true if gear change should occur
    double derivedRpm;             // predicted next-tick RPM (diagnostics)
    double estimatedWheelTorque;   // Nm (post-torque-converter)
};

class ITwinStrategy {
public:
    virtual ~ITwinStrategy() = default;

    // update — compute control outputs for dt seconds ahead
    // Guarantees: returns within 100 μs, allocates no heap, never blocks
    virtual TwinOutput update(const UpstreamSignal& signal,
                              double currentRpm,
                              double dt) = 0;

    virtual const char* getName() const = 0;
    virtual const char* getDescription() const = 0;
    virtual void resetToInitialState() = 0;    // clear integrators, filters

    // clone — produce identical independent instance (Prototype pattern)
    virtual std::unique_ptr<ITwinStrategy> clone() const = 0;
};
```

### 3.2 Contract — `update(const UpstreamSignal&, double, double)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `dt` > 0 and ≤ 1.0 (reasonable timestep)<br>- `currentRpm` ≥ 0<br>- `signal` fields are valid (per `ITelemetrySource` contract) |
| **Postconditions** | - Returns `TwinOutput` with all fields initialized<br>- `recommendedThrottle` ∈ [0, 1]<br>- `recommendedGear` is valid gear number<br>- `clutchEngagement` ∈ [0, 1] |
| **Real-Time Guarantee** | **Must complete within 100 μs** (audio thread context)<br>- Zero heap allocation (`new`/`malloc`, `std::vector::push_back`, etc.)<br>- No system calls that may block<br>- No locks/mutexes |
| **Determinism** | - Given same inputs, returns same output (pure function of inputs + internal state)<br>- Internal state changes only via `update()` and `resetToInitialState()` |
| **Error Handling** | - **Never throws** (would crash audio thread)<br>- If invalid state detected, return safe defaults (throttle=0, clutch=1) and log internally |

### 3.3 DynoAssistedStrategy Contract

**Implementation**: `DynoAssistedStrategy`

| Requirement | Specification |
|------------|---------------|
| **Target RPM calculation** | `targetRpm = (vehicleSpeedKmh / (2π × tireRadius)) × gearRatio × diffRatio × 60` (RPM from road speed) |
| **Gear selection** | Uses `SimpleGearbox` with ZF 8HP coefficients:<br>- Upshift RPM = 750 + 5750 × (0.20 + 0.60 × throttle)<br>- Downshift RPM = 750 + 5750 × (0.10 + 0.15 × throttle)<br>- Hysteresis enforced (downshift threshold < upshift) |
| **Clutch engagement** | Always `0.0` (disengaged) — dyno controls RPM, not clutch |
| **Throttle mapping** | `recommendedThrottle = signal.throttlePercent / 100.0` (may apply smoothing filter) |
| **Latency budget** | ≤ 50 μs (simple arithmetic, no trigonometry) |
| **Memory** | Fixed-size arrays only; no dynamic allocation in `update()` |

### 3.4 PhysicsDrivenStrategy Contract

**Implementation**: `PhysicsDrivenStrategy` (research track)

| Requirement | Specification |
|------------|---------------|
| **Tractive force** | `F = (engineTorque × gearRatio × finalDrive × efficiency) / wheelRadius - dragForce` |
| **Acceleration** | `a = F / vehicleMass` (integrated by caller or internally) |
| **Clutch model** | Uses `IClutch` interface; slip computed as `|engineRpm - shaftRpm| / engineRpm` |
| **Torque limit** | Respects `m_maxClutchTorque` — slip occurs when torque demand exceeds limit |
| **Latency budget** | ≤ 100 μs (more complex math but still lock-free) |
| **Note** | Not production-primary; research comparison only |

### 3.5 Contract — `clone() const`

| Aspect | Requirement |
|--------|-------------|
| **Semantics** | Returns deep copy with identical internal state (filters, integrators, last-known values) |
| **Use case** | `ITwinModel::switchStrategy()` swaps strategies without losing state |
| **Performance** | May allocate (called rarely, not in hot path) |
| **Exception safety** | Strong guarantee — if clone fails, original is unchanged |

### 3.6 Contract — `resetToInitialState()`

| Aspect | Requirement |
|--------|-------------|
| **Effect** | Clears all integrators, filters, PID history; resets to startup state |
| **When called** | On twin reset, before new scenario, after error recovery |
| **Thread safety** | Not thread-safe (called from control thread only) |

---

## 4. IGearbox

**Responsibility**: Model transmission behavior — gear selection, shift timing, torque conversion.

### 4.1 Interface Definition

```cpp
struct GearDefinition {
    int    gear;          // -2=reverse, -1=neutral, 1..N forward
    double ratio;         // gear ratio (<1 overdrive, >1 reduction)
    double maxRpm;        // redline while in this gear
    double minRpm;        // downshift threshold
};

struct ShiftCurve {
    std::vector<double> upshiftRpmByGear;   // index 0 unused or gear 1+
    std::vector<double> downshiftRpmByGear;
};

struct GearStatus {
    int    currentGear = 0;
    bool   neutralEngaged = true;
    bool   reverseEngaged = false;
    bool   shiftInProgress = false;
    double shiftProgress = 0.0;  // 0.0→1.0
};

class IGearbox {
public:
    virtual ~IGearbox() = default;

    virtual void setGearRatios(const std::vector<GearDefinition>& gears) = 0;
    virtual void setShiftCurve(const ShiftCurve& curve) = 0;
    virtual void setShiftDurationMs(double duration) = 0;

    virtual void selectGear(int targetGear) = 0;   // request shift
    virtual void abortShift() = 0;                 // cancel in-progress shift

    virtual int getCurrentGear() const = 0;
    virtual double getCurrentRatio() const = 0;
    virtual GearStatus getStatus() const = 0;

    virtual double convertEngineTorqueToWheel(double engineTorqueNm) const = 0;

    virtual const char* getName() const = 0;
};
```

### 4.2 Contract — `selectGear(int)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `targetGear` is in configured gear list or -1 (neutral), -2 (reverse) |
| **Postconditions** | - Shift begins asynchronously (if not already shifting)<br>- `getStatus().shiftInProgress` becomes `true`<br>- After `shiftDurationMs`, gear change completes |
| **Shift Duration** | Default 300 ms (configurable via `setShiftDurationMs`)<br>- During shift, torque transfer is reduced (simulate clutch disengagement) |
| **Hysteresis** | Implementation must enforce downshift RPM < upshift RPM for same gear to prevent hunting |
| **Concurrent Shifts** | If `selectGear()` called during shift, new request is queued or ignored (implementation-defined, must document) |
| **Error** | Throws `std::invalid_argument` if gear not in ratio list |

### 4.3 Contract — `convertEngineTorqueToWheel(double)`

| Aspect | Requirement |
|--------|-------------|
| **Calculation** | `wheelTorque = engineTorque × currentRatio × finalDriveRatio × efficiency`<br>- Efficiency = 0.95–0.98 (typical) |
| **During Shift** | If `shiftInProgress`, apply torque reduction factor (0.0–1.0) based on `shiftProgress` |
| **Thread Safety** | Lock-free (read-only state) |
| **Latency** | ≤ 10 μs |

### 4.4 SimpleGearbox Implementation Contract

| Requirement | Value |
|------------|-------|
| **Upshift threshold** | `RPM ≥ idle + (redline - idle) × (A + B × throttle)`<br>A = 0.20, B = 0.60 (ZF-like) |
| **Downshift threshold** | `RPM ≤ idle + (redline - idle) × (C + D × throttle)`<br>C = 0.10, D = 0.15 |
| **Idle RPM** | 750 RPM |
| **Redline** | 6500 RPM (configurable) |
| **Shift duration** | 250 ms (default) |
| **Kickdown** | Throttle increase > 0.4 within 0.5 s → downshift 1 gear (minimum 1 gear) |

---

## 5. IClutch

**Responsibility**: Model clutch engagement, slip ratio, and torque transfer limits.

### 5.1 Interface Definition

```cpp
struct ClutchFrictionModel {
    double maxTorqueNm;        // peak friction-limited torque
    double slipRatioAtMax;     // typical 0.05–0.15
    double lockupThreshold;    // slip below which considered engaged (e.g., 0.02)
};

struct ClutchStatus {
    double pressure = 0.0;       // 0.0–1.0
    double slipRatio = 0.0;      // (engineRpm - shaftRpm) / max(engineRpm, 1.0)
    double torqueCapacity = 0.0; // Nm currently transferrable
    bool   fullyEngaged = false; // slip < lockupThreshold
};

class IClutch {
public:
    virtual ~IClutch() = default;

    virtual void setFrictionModel(const ClutchFrictionModel& model) = 0;
    virtual void setEngagementCurve(const std::vector<double>& pedalToPressure) = 0;

    virtual void setPressure(double pressure01) = 0;       // commanded 0.0–1.0
    virtual void update(double dt, double engineRpm, double inputShaftRpm) = 0;

    virtual ClutchStatus getStatus() const = 0;
    virtual double getTransmittedTorque() const = 0;      // actual torque through clutch (Nm)

    virtual const char* getName() const = 0;
};
```

### 5.2 Contract — `setPressure(double)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `pressure01` ∈ [0, 1] (clamped if outside) |
| **Postconditions** | - Pressure command updated immediately<br>- Torque capacity = `pressure × maxTorqueNm` (at zero slip) |
| **Physical Model** | See `CLUTCH_PHYSICS_ANALYSIS.md`: finite `m_maxClutchTorque` means even at pressure=1.0, slip occurs if torque demand exceeds capacity |
| **Note** | For dyno-assisted twin, clutch pressure remains 0.0 (disengaged) |

### 5.3 Contract — `update(double, double, double)`

| Aspect | Requirement |
|--------|-------------|
| **Function** | Computes slip ratio and resulting torque transfer based on pressure, RPM difference, and friction model |
| **Slip Ratio** | `slip = |engineRpm - inputShaftRpm| / max(engineRpm, 100.0)` (avoid div/0) |
| **Torque Capacity** | `capacity = pressure × maxTorqueNm × f(slip)` where `f(slip)` is bell-curve peaking at `slipRatioAtMax` |
| **Lockup** | `fullyEngaged = (slip < lockupThreshold)` |
| **Latency** | ≤ 20 μs |

### 5.4 Contract — `getTransmittedTorque() const`

| Aspect | Requirement |
|--------|-------------|
| **Value** | Minimum of: (1) torque demand from engine side, (2) torque capacity, (3) torque that input shaft can accept |
| **Sign** | Positive for forward torque, negative for engine braking |
| **Thread Safety** | Lock-free read |

---

## 6. IWheel

**Responsibility**: Model wheel rotational dynamics, slip, and ground contact forces.

### 6.1 Interface Definition

```cpp
struct TireModel {
    double radiusM = 0.33;              // wheel radius
    double rollingResistanceCoeff = 0.015;
    double peakFrictionCoeff = 1.0;     // mu (dry asphalt)
    double slipAngleAtPeakDeg = 10.0;
};

struct WheelStatus {
    double angularVelocityRadS = 0.0;
    double linearVelocityKmh = 0.0;
    double longitudinalSlip = 0.0;      // (wheelSpeed - vehicleSpeed) / vehicleSpeed
    double lateralSlip = 0.0;
    double verticalLoadN = 0.0;
    double contactPatchForceN = 0.0;
};

class IWheel {
public:
    virtual ~IWheel() = default;

    virtual void setTireModel(const TireModel& model) = 0;
    virtual void setDriveTorque(double torqueNm) = 0;     // from driveshaft
    virtual void setBrakeTorque(double torqueNm) = 0;     // from caliper
    virtual void setSteeringAngleDeg(double angle) = 0;  // camber/scrub effect

    virtual void update(double dt, double vehicleMassKg) = 0;  // integrates forces

    virtual WheelStatus getStatus() const = 0;
    virtual double getRotationalSpeed() const = 0;

    virtual const char* getName() const = 0;
};
```

### 6.2 Contract — `update(double, double)`

| Aspect | Requirement |
|--------|-------------|
| **Dynamics** | Integrates angular acceleration: `α = (T_drive - T_brake - T_rolling - T_drag) / I`<br>- `I = m × r²` (simplified) |
| **Traction Limit** | `F_max = μ × F_z` (friction circle)<br>- If `T_drive / r > F_max`, wheel spins (longitudinal slip > 0) |
| **Rolling Resistance** | `F_rr = C_rr × m × g × sign(v)` |
| **Latency** | ≤ 30 μs |

### 6.3 Contract — `getStatus() const`

| Field | Validity |
|-------|----------|
| `longitudinalSlip` | ∈ [-1, ∞) — negative = braking, positive = driving slip |
| `lateralSlip` | ∈ [-0.5, 0.5] radians |
| `contactPatchForceN` | ≤ `μ × F_z` (friction limit) |

---

## 7. ITwinModel (Facade)

**Responsibility**: Orchestrate all twin subcomponents. Single façade exposed to bridge.

### 7.1 Interface Definition

```cpp
class ITwinModel {
public:
    virtual ~ITwinModel() = default;

    virtual bool initialize(ITelemetrySource*   telemetrySource,
                            IEngineController*   engineController,
                            ITwinStrategy*       strategy,
                            IGearbox*            gearbox = nullptr,
                            IClutch*             clutch = nullptr,
                            IWheel*              wheel = nullptr) = 0;

    virtual void reset() = 0;                    // clear all state, keep config
    virtual void shutdown() = 0;                 // teardown; no further calls safe

    virtual void update(double dt) = 0;          // main tick

    virtual double getCurrentRpm() const = 0;
    virtual int    getCurrentGear() const = 0;
    virtual double getVehicleSpeedKmh() const = 0;
    virtual const char* getStrategyName() const = 0;

    virtual bool switchStrategy(std::unique_ptr<ITwinStrategy> newStrategy) = 0;
    virtual std::vector<const char*> getAvailableStrategies() const = 0;

    virtual void registerTelemetry(telemetry::ITelemetryWriter* telemetry) = 0;
    virtual const char* getName() const = 0;
};
```

### 7.2 Contract — `update(double dt)`

| Aspect | Requirement |
|--------|-------------|
| **Sequence** | 1. `telemetrySource_->read(currentSignal)` (or use cached if stale)<br>2. `currentRpm = engineController_->getEngineRpm()`<br>3. `output = strategy_->update(currentSignal, currentRpm, dt)`<br>4. `engineController->setThrottle(output.recommendedThrottle)`<br>5. If `output.shiftRequested`: `engineController->setGear(output.recommendedGear)`<br>6. `engineController->setClutchPressure(output.clutchEngagement)` |
| **dt** | Typically 0.1 s (10 Hz), but must handle any `dt ≤ 1.0` |
| **Error Handling** | - If telemetry read fails → throw `TwinModelError` (fail-fast)<br>- If strategy update would throw (violates contract) → catch, log, throw wrapped error |
| **Latency Budget** | **≤ 5 ms** total (includes all component updates) |
| **Threading** | Called from single control thread (audio or sim thread) — not thread-safe for concurrent calls |

### 7.3 Contract — `switchStrategy(...)`

| Aspect | Requirement |
|--------|-------------|
| **Preconditions** | - `newStrategy` is non-null<br>- Not currently in a shift (if gearbox present and `gearbox->getStatus().shiftInProgress`) |
| **Postconditions** | - Old strategy is replaced (ownership transferred)<br>- New strategy's `resetToInitialState()` called<br>- Returns `true` on success |
| **Atomicity** | Swap is atomic (std::atomic pointer or mutex-protected) to prevent race with `update()` |
| **Failure** | Returns `false` if swap refused (e.g., mid-shift); old strategy remains active |

### 7.4 Contract — `reset()`

| Effect | Requirement |
|--------|-------------|
| **State** | All components return to initial state (as after `initialize()`)<br>- Strategy `resetToInitialState()`<br>- Gearbox to neutral<br>- Clutch pressure = 0<br>- Engine controller `reset()` |
| **Telemetry** | Source is **not** reset (external state) |

---

## 8. Error Conditions Summary

### 8.1 What Throws?

| Component | Throws? | When? |
|-----------|---------|-------|
| `ITelemetrySource::read()` | No | Returns `bool`; error via `getLastError()` |
| `ITwinStrategy::update()` | **No** (must not — audio thread) | Violation = undefined behavior (contract breach) |
| `IGearbox::selectGear()` | Yes | Invalid gear number (programming error) |
| `IEngineController::setGear()` | Yes | Invalid gear or no transmission (programming error) |
| `ITwinModel::update()` | **Yes** | Telemetry failure (I/O error) — fail-fast |
| `ITwinModel::switchStrategy()` | No | Returns `bool` for refusal |
| All `get*()` queries | No | Return safe defaults on error |

### 8.2 Error Code vs Exception Policy

- **Error codes** for **I/O-bound, recoverable** operations (telemetry, file I/O)
  - Rationale: Failures are expected (cable unplugged, network down)
  - Response: Retry, fallback, user notification

- **Exceptions** for **logic errors, invariant violations** (invalid arguments, null pointers)
  - Rationale: Programming error that should not happen in correct code
  - Response: Crash early, fix bug

- **No exceptions** from **real-time hot path** (`update()`, audio callback)
  - Rationale: Exception unwinding is non-deterministic and may block
  - Response: Validate inputs before hot path; use `noexcept` where possible

### 8.3 "Assert Correct Behaviour" Principle

Per CLAUDE.md: Tests and implementation prioritize **correct behaviour** over defensive error handling.

- Write code that **cannot fail** under valid inputs (use types to enforce invariants)
- Throw early on **invalid input** (fail-fast)
- Don't clutter logic with `if (error) return error;` for recoverable cases — use error codes only at I/O boundaries
- **Never** disguise an error as success (e.g., returning `0` RPM when sensor fails)

---

## 9. Performance & Latency Budgets

| Path | Budget | Rationale |
|------|--------|-----------|
| `ITwinStrategy::update()` | ≤ 100 μs | Runs in audio callback (44.1 kHz = 22.7 μs per sample, ~4 samples per callback) |
| `IGearbox::convertEngineTorqueToWheel()` | ≤ 10 μs | Called multiple times per update |
| `IClutch::update()` | ≤ 20 μs | Per-tick physics integration |
| `IWheel::update()` | ≤ 30 μs | Per-tick physics |
| `IEngineController::getEngineRpm()` | ≤ 100 μs | Audio thread reads every callback |
| `ITwinModel::update()` | ≤ 5 ms | Whole orchestration (10 Hz rate) |
| `ITelemetrySource::read()` | ≤ 10 ms | USB/BLE timeout allowance |

**All hot-path functions must be `noexcept` and lock-free.**

---

## 10. Cross-Component Data Flow (Contractual)

```
[ITelemetrySource] --(UpstreamSignal, 10 Hz)--> [ITwinModel]
                                                    |
                                                    v
                                             [ITwinStrategy]
                                                    |
                                                    v (TwinOutput)
                                             [ITwinModel] --(setThrottle, setGear)--> [IEngineController]
                                                    |                                              |
                                                    |                                              v
                                                    |                                    [ISimulator (engine-sim)]
                                                    |                                              |
                                                    +----------(getCurrentRpm)------------------+
```

**Data Contracts**:
- `UpstreamSignal` is **immutable snapshot** — strategies must not modify it
- `TwinOutput` is **command** — engine controller must apply it before next update
- All numerical values are **SI units** (m, s, kg, N, rad) except where explicitly documented (km/h, RPM, degrees)

---

## 11. Versioning & Compatibility

These interfaces are **stable** for the VirtualICE Twin project. Changes follow semantic versioning:

- **Patch**: Bug fixes, internal refactoring (no interface change)
- **Minor**: Add new optional methods with defaults; new strategy implementations
- **Major**: Alter existing method signatures or contracts

Backward compatibility is **not guaranteed** between major versions — strategies may need recompilation.

---

## 12. Compliance Checklist (for PRs)

Before merging, verify:
- [ ] All interface methods documented with pre/postconditions
- [ ] Latency budget specified for hot-path methods
- [ ] Error handling policy (throw vs error code) is consistent
- [ ] Real-time methods are `noexcept` and allocation-free
- [ ] Thread safety documented
- [ ] Concrete implementations match interface contracts
- [ ] Unit tests cover contract edge cases (min/max inputs, error paths)

---

**Last Updated**: 2026-04-25  
**Derived From**: CLUTCH_PHYSICS_ANALYSIS.md, INTERFACE_DESIGN.md, TRANSMISSION_RESEARCH.md, TEST_HARNESS_DESIGN.md

---