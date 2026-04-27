# VirtualICE Twin Module — Interface Design Specification

## Design Principles (Recap)

- **SRP**: Each interface owns exactly one concern
- **OCP**: Strategies are swappable without modifying bridge or vehicle-sim code
- **LSP**: PhysicsDriven and DynoAssisted strategies are substitutable
- **ISP**: Fine-grained interfaces, no fat contracts
- **DIP**: Bridge depends on abstractions (`ITwinModel`), not concrete strategies

---

## 1. ITelemetrySource

**Single Responsibility**: Abstract acquisition of upstream vehicle data (OBD2, mock, recorded) and normalize to `UpstreamSignal`.

**Abstraction**: Input data comes from diverse sources:
- Real OBD2/BLE (Tesla, other EVs)
- Mock/generator (deterministic test scenarios)
- Recorded trace files (CSV/WAV-referenced logs)
- Dyno load-cell sensors

The source normalizes all to a unified `UpstreamSignal` struct. Bridge reads from an `ITelemetrySource` without knowing the origin.

### Interface Definition

```cpp
// Twin/TwinTypes.h
#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <memory>

// UpstreamSignal — normalized data consumed by ITwinStrategy
// Immutable snapshot; strategies treat as read-only
struct UpstreamSignal {
    double   timestamp;           // seconds since simulation start
    double   vehicleSpeedKmh;     // 0.0+
    double   wheelSpeedKmh;       // 0.0+ (for slip calc)
    double   throttlePercent;     // 0.0–100.0
    double   brakePedalPercent;   // 0.0–100.0
    double   clutchPedalPercent;  // 0.0–100.0 (0 = engaged, 100 = disengaged)
    int32_t  gear;                // -2=reverse, -1=neutral, 0=N, 1+=forward gears
    double   steeringAngleDeg;    // -540..+540 (2.5 turns lock-to-lock)
    double   lateralAccelG;       // G-force lateral
    double   longitudinalAccelG;  // G-force forward/brake
    bool     absActive;           // ABS intervention detected
    bool     tractionControlActive;
    double   batteryVoltage;      // 12V system voltage
    double   coolantTempC;        // °C
    // Extensibility: reserved for CAN bus channel identifiers, dyno load, etc.
    std::vector<std::pair<std::string, double>> extendedMetrics;
};

// TelemetrySourceError — minimal error info; never throws exceptions
struct TelemetrySourceError {
    enum class Code {
        None,
        DeviceDisconnected,
        ParseError,
        TraceFileCorrupt,
        Timeout,
        HardwareUnavailable
    } code = Code::None;
    std::string message;
};

// ITelemetrySource — reads normalized vehicle data
class ITelemetrySource {
public:
    virtual ~ITelemetrySource() = default;

    // Blocking read; returns false on non-recoverable error (device unplugged, EOF)
    // Returns true + fills signal on success.
    virtual bool read(UpstreamSignal& signal) = 0;

    // Current error state (read-only, thread-safe query)
    virtual TelemetrySourceError getLastError() const = 0;

    // Reset error state after recovery (e.g. device reconnected)
    virtual void clearError() = 0;

    // Source metadata for diagnostics
    virtual const char* getName() const = 0;

    // Non-blocking poll; if no data available yet, returns false
    // Used by UI loops wanting to stay responsive during reads
    virtual bool tryRead(UpstreamSignal& signal) = 0;
};
```

### Concrete Implementations

| Class | What it owns | Construction | Destruction |
|---|---|---|---|
| `OBD2TelemetrySource` | Serial/CAN adapter handles, OBD-II session | `OBD2TelemetrySource::create(devicePath, baudRate, logger)` | Closes adapter, invalidates handles |
| `MockTelemetrySource` | Deterministic scenario generator (accel/brake/gear scripts) | `MockTelemetrySource::create(scenarioConfig)` | No external resources |
| `RecordedTelemetrySource` | Memory-mapped CSV/WAV index or stream reader | `RecordedTelemetrySource::create(csvPath, syncAudioPath)` | Releases file handles |
| `DynoTelemetrySource` | Dyno controller API client (load-cell, rollers) | `DynoTelemetrySource::create(dynoIP, port)` | Disconnects TCP session |

**Ownership/Lifetime**: Source objects are **owned by** `ITwinModel` (strategy facade). Created via constructor injection or factory. Destroyed when twin model is destroyed.

**DIP Dependencies**: Depends on external hardware/serial/file APIs — these are wrapped/encapsulated within each concrete source. No dependency leaks outward.

---

## 2. IEngineController

**Single Responsibility**: Drive the engine-sim (`ISimulator`) — set throttle/gear/clutch and query engine RPM. It is the exclusive gateway to engine-sim from the twin.

**Abstraction**: The twin strategies call `setThrottle()`, `setGear()`, etc. These translate to simulator state updates and eventually audio output.

### Interface Definition

```cpp
// Twin/IEngineController.h
#pragma once
#include <memory>
#include "simulator/ISimulator.h"
#include "simulator/EngineSimTypes.h"

// DynoParams — optional dyno-load simulation
// Used by DynoAssisted strategy to load-test the engine
struct DynoParams {
    double rollingResistanceCoeff = 0.015;  // ~1.5% drag
    double inertiaKgM2 = 50.0;              // dyno drum inertia
    double targetLoadN = 0.0;               // commanded resistance force (N)
    bool   isAbsorbing = true;              // dyno under power (vs motoring)
};

// IEngineController — controls engine-sim and reports state
class IEngineController {
public:
    virtual ~IEngineController() = default;

    // ── Control primitives ──────────────────────────────────────────────────────
    virtual void setThrottle(double position) = 0;          // 0.0–1.0
    virtual void setGear(int gear) = 0;                     // -2(R), -1(N), 0+ forward
    virtual void setClutchPressure(double pressure) = 0;    // 0.0–1.0 (0 = disengaged)
    virtual void setDynoParams(const DynoParams& params) = 0;

    // ── Queries ────────────────────────────────────────────────────────────────
    virtual double getEngineRpm() const = 0;            // instantaneous RPM
    virtual double getEngineLoad() const = 0;           // 0.0–1.0
    virtual double getExhaustFlow() const = 0;          // m³/s
    virtual EngineSimStats getStats() const = 0;        // full struct snapshot

    // ── Lifecycle ─────────────────────────────────────────────────────────────
    virtual bool initialize(const ISimulatorConfig& config, ILogging* logger = nullptr) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void reset() = 0;

    // ── Diagnostics ────────────────────────────────────────────────────────────
    virtual const char* getName() const = 0;
    virtual void registerTelemetry(telemetry::ITelemetryWriter* telemetry) = 0;  // DIP hook
};
```

### Concrete Implementation

`EngineController` — wraps an `ISimulator` instance (BridgeSimulator or SineEngine). It:

- Constructs/owns an `ISimulator` via `SimulatorFactory::create`
- Translates strategy commands to simulator methods (`setThrottle → ISimulator::setThrottle`)
- Maintains the current gear/clutch state (source of truth for shift logic)
- Buffers stat queries (`getEngineRpm` returns last updated state atomically)
- Optionally forwards engine stats to the global telemetry writer

**Ownership/Lifetime**: `EngineController` is **owned by** `ITwinModel`. Created at twin startup.

**DIP Dependencies**:
```
IEngineController  ──depends on──►  ISimulator (abstraction)
                           ──depends on──►  ILogging (abstraction)
                           ──depends on──►  ITelemetryWriter (abstraction)
                           ──depends on──►  EngineSimDefaults constants
```

---

## 3. ITwinStrategy

**Single Responsibility**: Core twin update algorithm — given current vehicle telemetry and engine RPM, compute next throttle/gear/clutch setpoints.

**Abstraction**: Two distinct families:

| Strategy | Philosophy | Use-Case |
|---|---|---|
| `PhysicsDrivenStrategy` | Physics model: mass, drag, gear ratios, clutch slip, engine torque curve | Realistic vehicle simulation, driver training |
| `DynoAssistedStrategy` | Measured dyno load curves override physics; engine runs against known resistance | Engine calibration, dyno benchmarking |

### Interface Definition

```cpp
// Twin/ITwinStrategy.h
#pragma once
#include <memory>
#include "TwinTypes.h" // UpstreamSignal

// TwinOutput — commands computed by strategy for next timestep
struct TwinOutput {
    double recommendedThrottle;    // 0.0–1.0 (may be overridden by user)
    int    recommendedGear;        // -2..N (desired gear)
    double clutchEngagement;       // 0.0–1.0 (0=fully engaged)
    bool   shiftRequested;         // true if gear change should occur
    double derivedRpm;             // predicted next-tick RPM (for diagnostics)
    double estimatedWheelTorque;   // Nm (post-torque-converter)
};

/// ITwinStrategy — pure virtual update function
///
/// Strategy holds no mutable global state that bridges or vehicle-sim depend on.
/// It may own private data (filters, integrators) but its public contract is
/// completely encapsulated in this one method.
///
/// Threading: This method is invoked synchronously by ITwinModel in real-time
/// audio thread context (SyncPullStrategy), or via twin update hook. Strategies
/// must be lock-free, non-blocking, real-time safe.
class ITwinStrategy {
public:
    virtual ~ITwinStrategy() = default;

    /// update — compute control outputs for dt seconds ahead
    ///
    /// \param signal       Current normalized vehicle telemetry
    /// \param currentRpm   Actual engine RPM (from IEngineController)
    /// \param dt           Time since last update (seconds)
    /// \return TwinOutput  Computed setpoints for engine controller
    ///
    /// Guarantees: returns within microseconds, allocates no heap, never blocks.
    virtual TwinOutput update(const UpstreamSignal& signal,
                              double currentRpm,
                              double dt) = 0;

    // Strategy metadata — used by UI and logging
    virtual const char* getName() const = 0;
    virtual const char* getDescription() const = 0;
    virtual void resetToInitialState() = 0;    // clear integrators, filters

    /// clone — produce an identical independent strategy instance (Prototype pattern)
    ///
    /// Used when ITwinModel needs to swap strategies at runtime without
    /// destroying the active twin session. The cloned instance starts with
    /// identical internal state (filters, last-known values).
    virtual std::unique_ptr<ITwinStrategy> clone() const = 0;
};
```

### Concrete Strategy Implementation Notes

**PhysicsDrivenStrategy**:
- Owns: `IGearbox`, `IClutch`, `IWheel` part models (composed via DI)
- Uses torque curves from `engine-sim` (`TorqueCurve` query if available, else lookup table)
- Computes tractive force: `F = (engineTorque * gearRatio * finalDrive * transmissionEfficiency) / wheelRadius`
- Integrates acceleration: `dv/dt = (F - drag) / vehicleMass`
- Models clutch slip: `slip = |engineRpm / gearboxRpm - 1|` (torque-converter model as needed)

**DynoAssistedStrategy**:
- Owns: `IDynoCurveProvider` injectable dependency (abstraction over dyno data)
- Uses measured dyno torque table (speed vs. torque) instead of computed
- Still computes engagement/shift logic but skips physics-based tractive-force prediction
- May include smoothing/low-pass filter for measured dyno values

---

## 4. Vehicle Part Interfaces (IGearbox, IClutch, IWheel)

### 4.1 IGearbox

**Single Responsibility**: Model transmission behavior — which gear is active, shift timing, gear ratios, shift curve boundaries.

```cpp
// Twin/IGearbox.h
#pragma once
#include <vector>

struct GearDefinition {
    int         gear;            // -2=reverse, -1=neutral, 0=N, 1..N forward
    double      ratio;           // gear ratio (<1 for overdrive, >1 for reduction)
    double      maxRpm;          // redline while in this gear (auto-upshift)
    double      minRpm;          // downshift threshold
};

struct ShiftCurve {
    // Shift point is gear- and RPM-dependent; strategies consume these.
    // Pre-computed per-strategy or loaded from config.
    std::vector<double> upshiftRpmByGear;     // size = forwardGearCount + 1
    std::vector<double> downshiftRpmByGear;   // size = forwardGearCount + 1
};

struct GearStatus {
    int   currentGear = 0;       // active gear
    bool  neutralEngaged = true;
    bool  reverseEngaged = false;
    bool  shiftInProgress = false;
    double shiftProgress = 0.0;  // 0.0→1.0, shift duration normalized
};

// IGearbox — models transmission gearing and shift behavior
class IGearbox {
public:
    virtual ~IGearbox() = default;

    // Configuration (injected once at construction)
    virtual void setGearRatios(const std::vector<GearDefinition>& gears) = 0;
    virtual void setShiftCurve(const ShiftCurve& curve) = 0;
    virtual void setShiftDurationMs(double duration) = 0;  // mechanical shift time

    // Control
    virtual void selectGear(int targetGear) = 0;           // request shift
    virtual void abortShift() = 0;                         // cancel in-progress shift

    // Queries
    virtual int getCurrentGear() const = 0;
    virtual double getCurrentRatio() const = 0;
    virtual GearStatus getStatus() const = 0;

    // Torque conversion
    virtual double convertEngineTorqueToWheel(double engineTorqueNm) const = 0;

    virtual const char* getName() const = 0;
};
```

**Concrete**: `ManualGearbox` (clutch-required shifts), `SequentialGearbox` (auto-clutch), `AutoTransmission` (torque converter + shift scheduling).

**Ownership**: Shared/owned by `PhysicsDrivenStrategy`. Strategies compose their own gearbox instance (does not need to be shared across strategies).

**DIP Dependencies**: Depends on `GearDefinition` and `ShiftCurve` data structs only.

---

### 4.2 IClutch

**Single Responsibility**: Model clutch engagement, slip ratio, and torque transfer limits.

```cpp
// Twin/IClutch.h
#pragma once

// Friction curve model (torqueCapacity vs. slip)
struct ClutchFrictionModel {
    double maxTorqueNm;          // peak friction-limited torque
    double slipRatioAtMax;       // wheel speed / engine speed - 1 at peak (typically 0.05–0.15)
    double lockupThreshold;      // slip below which clutch is considered fully engaged
};

struct ClutchStatus {
    double pressure = 0.0;       // 0.0–1.0 (pedal position / commanded)
    double slipRatio = 0.0;      // (engineRpm - wheelRpm) / engineRpm; 0=locked, 0.3=burnout
    double torqueCapacity = 0.0; // Nm currently transferrable
    bool   fullyEngaged = false;
};

// IClutch — models clutch dynamics and slip
class IClutch {
public:
    virtual ~IClutch() = default;

    virtual void setFrictionModel(const ClutchFrictionModel& model) = 0;
    virtual void setEngagementCurve(const std::vector<double>& pedalToPressure) = 0;

    // Called every update
    virtual void setPressure(double pressure01) = 0;       // commanded 0.0–1.0
    virtual void update(double dt, double engineRpm, double inputShaftRpm) = 0;

    // Queries (thread-safe real-time reads)
    virtual ClutchStatus getStatus() const = 0;
    virtual double getTransmittedTorque() const = 0;      // actual torque through clutch (Nm)

    virtual const char* getName() const = 0;
};
```

**Concrete**: `FrictionClutch` (standard dry-plate model), `TorqueConverter` (fluid coupling with impeller/turbine, used in autos).

**Ownership**: Shared/owned by `PhysicsDrivenStrategy`. May also be owned by a gearbox model for sequential transmissions.

**DIP Dependencies**: `ClutchFrictionModel`, `ClutchStatus` structs.

---

### 4.3 IWheel

**Single Responsibility**: Model wheel rotational dynamics, slip, and ground contact forces.

```cpp
// Twin/IWheel.h
#pragma once

// Tire model parameters (simplified Pacejka/Magic Formula can be added later)
struct TireModel {
    double radiusM = 0.33;          // wheel radius (m)
    double rollingResistanceCoeff = 0.015;
    double peakFrictionCoeff = 1.0; // mu (dry asphalt ~0.9–1.0)
    double slipAngleAtPeakDeg = 10.0;
};

struct WheelStatus {
    double angularVelocityRadS = 0.0;        // rad/s
    double linearVelocityKmh = 0.0;          // km/h ground speed
    double longitudinalSlip = 0.0;           // (wheelSpeed - vehicleSpeed) / vehicleSpeed
    double lateralSlip = 0.0;                // side-slip angle derivative
    double verticalLoadN = 0.0;              // N (weight transfer)
    double contactPatchForceN = 0.0;         // driving/braking force at contact
};

// IWheel — models a single wheel or aggregated axle
//  - Set commanded drive torque (from gearbox/clutch)
//  - Update with dt to integrate dynamics
//  - Query velocity, slip, force states
class IWheel {
public:
    virtual ~IWheel() = default;

    virtual void setTireModel(const TireModel& model) = 0;
    virtual void setDriveTorque(double torqueNm) = 0;           // from driveshaft
    virtual void setBrakeTorque(double torqueNm) = 0;           // from caliper
    virtual void setSteeringAngleDeg(double angle) = 0;        // camber/scrub effect

    virtual void update(double dt, double vehicleMassKg) = 0;  // integrates forces

    virtual WheelStatus getStatus() const = 0;
    virtual double getRotationalSpeed() const = 0;

    virtual const char* getName() const = 0;
};
```

**Concrete**: `SimpleWheel` (friction circle + slip model), `PacejkaWheel` (full Magic Formula).

**Ownership**: `PhysicsDrivenStrategy` composes 2–4 `IWheel` instances (front/rear or per-wheel). DynoAssisted owns none (dyno supplies forces directly).

**DIP Dependencies**: `TireModel`, `WheelStatus` structs.

---

## 5. ITwinModel (Facade)

**Single Responsibility**: Orchestrate all twin subcomponents. It is the single façade exposed to the bridge. Bridge calls `twin.update()` once per frame; `ITwinModel` coordinates telemetry → strategy → engine controller.

### Interface Definition

```cpp
// Twin/ITwinModel.h
#pragma once
#include <memory>
#include "TwinTypes.h"

// Forward declarations
class ITelemetrySource;
class IEngineController;
class ITwinStrategy;
class IGearbox;
class IClutch;
class IWheel;

/// ITwinModel — twin subsystem façade (Subsystem pattern)
///
/// Callers (BridgeSimulator, ThreadedStrategy) interact with exactly one object.
/// Internal delegation to parts is encapsulated.
class ITwinModel {
public:
    virtual ~ITwinModel() = default;

    // ── Lifecycle ──────────────────────────────────────────────────────────────
    virtual bool initialize(ITelemetrySource*   telemetrySource,
                            IEngineController*   engineController,
                            ITwinStrategy*       strategy,
                            IGearbox*            gearbox = nullptr,
                            IClutch*             clutch = nullptr,
                            IWheel*              wheel = nullptr) = 0;

    virtual void reset() = 0;                    // clear all state, keep config
    virtual void shutdown() = 0;                 // teardown; no further calls safe

    // ── Real-time update (called once per main-loop tick or audio callback) ───
    /// \param dt  seconds since last update
    virtual void update(double dt) = 0;

    // ── Runtime queries ───────────────────────────────────────────────────────
    virtual double getCurrentRpm() const = 0;
    virtual int    getCurrentGear() const = 0;
    virtual double getVehicleSpeedKmh() const = 0;
    virtual const char* getStrategyName() const = 0;

    // ── Strategy swapping (OCP hot-swap) ──────────────────────────────────────
    /// Swap the active strategy while preserving twin state.
    /// Returns true on success, false if swap refused (e.g. during shift).
    virtual bool switchStrategy(std::unique_ptr<ITwinStrategy> newStrategy) = 0;
    virtual std::vector<const char*> getAvailableStrategies() const = 0;

    // ── Diagnostics ────────────────────────────────────────────────────────────
    virtual void registerTelemetry(telemetry::ITelemetryWriter* telemetry) = 0;
    virtual const char* getName() const = 0;
};
```

### TwinModel Implementation Notes

`TwinModel` is the concrete class implementing `ITwinModel`.

**Internal state composition**:
```
TwinModel (owns)
 ├─ ITelemetrySource*         telemetrySource_                (injected, not owned)
 ├─ IEngineController*         engineController_                (owned)
 ├─ std::unique_ptr<ITwinStrategy> activeStrategy_              (owned)
 ├─ IGearbox*                  gearbox_                         (owned or injected)
 ├─ IClutch*                   clutch_                          (owned or injected)
 └─ std::vector<std::unique_ptr<IWheel>> wheels_               (owned)
```

**`update(dt)` flow**:
1. `telemetrySource_->read(currentSignal)`
2. `currentRpm = engineController_->getEngineRpm()`
3. `strategyOutput = activeStrategy_->update(currentSignal, currentRpm, dt)`
4. `engineController_->setThrottle(strategyOutput.recommendedThrottle)`
5. `engineController_->setGear(strategyOutput.recommendedGear)`
6. `engineController_->setClutchPressure(strategyOutput.clutchEngagement)`
7. (optional) propagate stats → telemetry writer

**Threading model**:
- `ITwinModel::update()` is invoked on the audio thread (SyncPullStrategy) or simulation thread (ThreadedStrategy).
- All component reads/writes within `update()` are lock-free.
- `switchStrategy()` performs lock-free swap using atomic `activeStrategy_`.

---

## 6. TwinFactory (Dependency Injection Configuration)

**Single Responsibility**: Assemble a complete `ITwinModel` with all dependencies.

### Factory Interface

```cpp
// Twin/TwinFactory.h
#pragma once
#include <memory>
#include "Twin/TwinTypes.h"
#include "Twin/ITwinModel.h"

// TwinMode — strategy selection
enum class TwinMode {
    PhysicsDriven,    // full physics simulation
    DynoAssisted      // measured dyno curves
};

// TwinConfig — configuration for TwinFactory::create()
struct TwinConfig {
    TwinMode                  mode = TwinMode::PhysicsDriven;
    ISimulatorConfig          engineConfig;          // from EngineSimDefaults
    bool                      enableTelemetry = true;
    std::string               telemetrySourceType;   // "obd2", "mock", "recorded", "dyno"
    std::string               sourceConfigJson;      // per-source config blob
    std::string               vehicleMassKg = "1500"; // for physics model
    std::string               gearboxConfigFile;     // JSON gear ratios
    std::string               clutchFrictionConfig;  // JSON friction curve
};

// TwinFactory — creates fully wired ITwinModel
class TwinFactory {
public:
    static std::unique_ptr<ITwinModel> create(const TwinConfig& config,
                                               ILogging* logger = nullptr);
    static void destroy(ITwinModel* twin);  // explicit teardown if needed

    // Helper constructors for individual parts (test seam)
    static std::unique_ptr<ITelemetrySource> createTelemetrySource(
        const std::string& type, const std::string& configJson, ILogging* logger = nullptr);
    static std::unique_ptr<IEngineController> createEngineController(
        const ISimulatorConfig& config, ILogging* logger = nullptr);
    static std::unique_ptr<ITwinStrategy> createStrategy(
        TwinMode mode, IGearbox* gearbox = nullptr, IClutch* clutch = nullptr);
    static std::unique_ptr<IGearbox> createGearbox(
        const std::string& configPath);
    static std::unique_ptr<IClutch> createClutch(
        const std::string& configPath);
};
```

### Factory Assembly Flow (`create()`)

```
TwinFactory::create(config)
 │
 ├─ 1. Parse config JSONs for gearbox + clutch
 │
 ├─ 2. Construct parts (for PhysicsDriven mode)
 │    ├─ gearbox = IGearbox::create(gearboxConfig)
 │    ├─ clutch   = IClutch::create(clutchConfig)
 │    └─ wheel    = IWheel::create(tireModel) × 4
 │
 ├─ 3. Construct engine controller
 │    └─ engineController = IEngineController::create(config.engineConfig, logger)
 │
 ├─ 4. Construct telemetry source
 │    └─ source = createTelemetrySource(config.sourceType, config.sourceConfig)
 │
 ├─ 5. Construct strategy
 │    └─ strategy = createStrategy(config.mode, gearbox.get(), clutch.get())
 │
 ├─ 6. Construct twin model
 │    └─ twin = new TwinModel()
 │
 ├─ 7. Wire (DI)
 │    twin->initialize(source, engineController, strategy, gearbox, clutch, wheel)
 │
 └─ 8. return twin as ITwinModel
```

**Ownership**: Factory transfers ownership via `unique_ptr`. Callee (`ITwinModel`) takes ownership of `engineController`, `strategy`, `gearbox`, `clutch`, `wheels`. `ITelemetrySource` remains borrowed (not owned — shared with other consumers if needed).

**DIP**: Factory depends only on abstract factory methods (`createStrategy`, `createTelemetrySource`). Concrete implementations are plug-in style.

---

## 7. Component Diagram (ASCII)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                              BRIDGE LAYER                                 │
│  (BridgeSimulator / ThreadedStrategy / main.cpp)                          │
└────────────────────────────────────┬─────────────────────────────────────┘
                                     │ owns
                                     ▼
                    ╔══════════════════════════════╗
                    ║   ITwinModel (facade)        ║
                    ║   ────────────────────       ║
                    ║   + update(dt)               ║
                    ║   + switchStrategy(...)      ║
                    ╚═══╤═══════════╤════════╤════╝
        injects         │           │        │
     (not owned)        │           │        │
            ┌───────────▼────┐  ┌──▼──────────┐
            │ ITelemetrySource│  │ IEngineController │
            │ ──────────────  │  │ ───────────       │
            │ + read()        │  │ + setThrottle()   │
            │ + tryRead()     │  │ + setGear()       │
            └────────┬────────┘  │ + getEngineRpm()  │
                     │           └──┬───────────────┘
                     │              │ owns
                     │              ▼
                     │       ┌─────────────────┐
                     │       │ ISimulator      │
                     │       │ (engine-sim)    │
                     │       └────────┬────────┘
                     │              │
                     └──────────────┴─────────────── adapters
                                   │
                     ┌─────────────▼────────────────┐
                     │   UpstreamSignal (normalized)│
                     │   Throttle/Gear/Clutch cmds  │
                     └─────────────┬────────────────┘
                                   │
                ┌──────────────────▼──────────────┐
                │   ITwinStrategy.update()        │
                │   ──────────────────────        │
                │   TwinOutput → EngineController │
                └─────────────┬────────────┬──────┘
                              │            │
              ┌───────────────▼─┐   ┌──────▼────────────┐
              │ PhysicsDriven   │   │ DynoAssisted       │
              │ ─────────────   │   │ ──────────────    │
              │ + IGearbox*     │   │ + IDynoCurve*     │
              │ + IClutch*      │   │ + Filter          │
              │ + IWheel*×N     │   │                   │
              └────────┬────────┘   └───────────────────┘
                       │
          ┌────────────┼────────────┐
          ▼            ▼            ▼
      ┌────────┐ ┌─────────┐ ┌──────────┐
      │IGearbox│ │ IClutch │ │  IWheel  │
      │────────│ │─────────│ │──────────│
      │+ gears │ │+ slip   │ │+ torque  │
      │+ shift │ │+ torque │ │+ velocity│
      └────────┘ └─────────┘ └──────────┘

LEGEND:
  ──► owns / composes
  ───► calls / invokes
═══════► façade / boundary
```

**Data flow in one tick**:

```
vehicle → ITelemetrySource → UpstreamSignal → ITwinStrategy.update()
                                                              ↓
                                       TwinOutput (throttle/gear/clutch)
                                                              ↓
                                    IEngineController.set*() → ISimulator
                                                              ↓
                                                            Audio output
```

---

## 8. Ownership & Lifetime Matrix

| Interface | Creator | Owner | Destroyer | Notes |
|---|---|---|---|---|
| `ITelemetrySource` | `TwinFactory` (via `createTelemetrySource`) | `ITwinModel` (held by `TwinModel`) | `ITwinModel` destructor | Not owned if injected from external consumer |
| `IEngineController` | `TwinFactory::createEngineController` | `ITwinModel` (`TwinModel`) | `ITwinModel` destructor | Owns embedded `ISimulator` |
| `ITwinStrategy` | `TwinFactory::createStrategy` or `activeStrategy_->clone()` | `ITwinModel` (`TwinModel`) | `ITwinModel` destructor or `switchStrategy()` | `clone()` protects against state loss during swap |
| `IGearbox` / `IClutch` / `IWheel` | `TwinFactory` | `ITwinStrategy` (`PhysicsDrivenStrategy`) | `PhysicsDrivenStrategy` destructor | DynoAssignedStrategy owns none |
| `ITwinModel` | `TwinFactory::create` | Caller (BridgeSimulator / ThreadedStrategy) | Caller (`delete` or `unique_ptr`) | Top-level façade |

---

## 9. Threading & Real-Time Guarantees

| Interface | Thread context | Blocking allowed? | Heap allocation |
|---|---|---|---|
| `ITelemetrySource::tryRead` | Any | No | No |
| `ITelemetrySource::read`   | Bridge sim thread | May block up to 10ms (USB timeout) | No |
| `IEngineController::set*`  | Twin update thread | No | No |
| `IEngineController::get*`  | Bridge UI thread | No | No |
| `ITwinStrategy::update`   | Twin update thread (real-time) | **Never** | **Never** (use pools if needed) |
| `IGearbox` / `IClutch` / `IWheel::update` | Twin update thread | No | No |

**Note:** Strategies must be lock-free and allocate no heap during `update()`. Any dynamic data initialized once at construction.

---

## 10. Dependency Graph

```
                    ┌─────────────────────┐
                    │   EngineSimDefaults │ (const values only)
                    └──────────┬──────────┘
                               │
    ┌──────────────────────────▼──────────────────────────┐
    │              DIP Boundary (Abstractions)             │
    ├────────────────┬─────────────────┬───────────────────┤
    │                │                 │                   │
    ▼                ▼                 ▼                   ▼
ITelemetrySource  IEngineController  ITwinStrategy       IGearbox/IClutch/IWheel
    │                │                 │                   │
    │                │                 │                   │
    ▼                ▼                 ▼                   ▼
    └────────────────┴─────────────────┴───────────────────┘
                               │
                               ▼
                          ITwinModel (Facade)
                               │
                               ▼
                            BridgeSimulator
```

**Key DIP direction**: Bridge depends on `ITwinModel` (abstraction). It does not depend on `PhysicsDrivenStrategy` or `DynoAssistedStrategy` directly. Strategy choice is injected via `TwinFactory` at startup or via `switchStrategy()` at runtime.

---

## 11. Example: Swapping Strategies at Runtime

```cpp
// Bridge code — OCP in action
void BridgeSimulator::setTwinMode(TwinMode newMode) {
    auto newStrategy = TwinFactory::createStrategy(newMode,
                                                   gearbox_,   // share same parts
                                                   clutch_);   // across strategies
    if (twin_->switchStrategy(std::move(newStrategy))) {
        currentMode_ = newMode;
    }
    // Bridge never touches PhysicsDriven or DynoAssisted class names.
}
```

---

## 12. Extension Points (OCP Compliance)

| Want to add… | What changes? | DIP violation? |
|---|---|---|
| New telemetry source (e.g., CAN-FD) | Add `CANFDTelemetrySource` class. Zero changes elsewhere | No |
| New strategy (e.g., `RecordedPlaybackStrategy`) | Add `RecordedPlaybackStrategy : ITwinStrategy`. Register in `TwinFactory::createStrategy` | No |
| New part model (e.g., `DCTGearbox`) | Add `DCTGearbox : IGearbox`. Inject into strategy ctor | No |
| New engine-sim backend (e.g., external dyno API) | Add `ExternalDynoEngineController : IEngineController` | No |

---

## Summary

All five interfaces are now defined with:

- **Clear SRP** boundaries (data acquisition → control → strategy → parts → facade)
- **OCP** via strategy pattern (`ITwinStrategy.switchStrategy()`)
- **Liskov** substitutability (any `ITwinStrategy` plugs into `ITwinModel`)
- **ISP** (no fat interfaces; part interfaces are minimal)
- **DIP** (bridge depends only on `ITwinModel` / `ITelemetrySource` abstractions)

The bridge, vehicle-sim module, and presentation layer never need to know whether the twin is PhysicsDriven or DynoAssisted. The strategy and part implementations are encapsulated behind factories and interfaces.
