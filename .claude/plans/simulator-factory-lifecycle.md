# SimulatorFactory Lifecycle Analysis & Proposal

## Current State: Full Lifecycle Trace

### Upstream Simulator Base Class (engine-sim)

The upstream `Simulator` class has a three-phase lifecycle:

1. **`initialize(Parameters)`** — Creates the physics rigid body system (`RigidBodySystem`), allocates dyno torque sample buffer. No engine/vehicle/transmission yet.
2. **`loadSimulation(Engine*, Vehicle*, Transmission*)`** — Stores raw pointers. That is literally all it does (simulator.cpp:54-58).
3. **`initializeSynthesizer()`** — Creates the `Synthesizer` with channel count from `m_engine->getExhaustSystemCount()`, buffer sizes, sample rates.

These are three separate calls. The upstream code never unified them because the original app had a UI where scripts could be swapped at runtime.

### PistonEngineSimulator Lifecycle (upstream)

`PistonEngineSimulator::loadSimulation()` does the heavy lifting (piston_engine_simulator.cpp:33-176):

1. Calls `Simulator::loadSimulation(engine, vehicle, transmission)` — stores pointers
2. Allocates all physics constraints: crankConstraints, cylinderWallConstraints, linkConstraints, crankshaftFrictionConstraints, crankshaftLinks, delayFilters
3. Wires every crankshaft into the rigid body system (positions, masses, moments of inertia)
4. Wires transmission, vehicle, vehicleMass, vehicleDrag into the system
5. Wires every cylinder: piston/con-rod bodies, cylinder wall constraints, link constraints, combustion chambers as force generators
6. Wires dyno + starter motor into the system
7. Calls `placeAndInitialize()` — solves initial cylinder geometry, initializes combustion chambers, delay filters, ignition module, allocates `m_exhaustFlowStagingBuffer`
8. Calls `initializeSynthesizer()`

The PistonEngineSimulator does NOT own engine/vehicle/transmission. They come from the script compiler output. Its `destroy()` (piston_engine_simulator.cpp:335-359) cleans up system/constraints/buffers but sets `m_engine = nullptr` without deleting — ownership stays external.

### SineSimulator Lifecycle (bridge layer)

`SineSimulator::loadSimulation(nullptr, nullptr, nullptr)` does:

1. **Ignores** the passed-in nullptrs entirely
2. Creates its own `SineEngine`, `SineVehicle`, `SineTransmission` (internal ownership)
3. Calls `Simulator::loadSimulation(sineEngine, sineVehicle, sineTranny)` — stores pointers
4. Calls `initializeSynthesizer()` — PistonEngineSimulator does this inside loadSimulation too (line 175)
5. Sets unit impulse response (prevents null deref in ConvolutionFilter)
6. Disables all noise/convolution via `setAudioParameters()`
7. Calls `SimulatorInitHelpers::wirePhysics()` — shared physics wiring (vehicleMass, transmission, vehicle, dyno, starter, staging buffer)

### Factory: What SimulatorFactory::create() Does

**SineWave path (lines 50-54):**
```
auto sineSim = make_unique<SineSimulator>();
initSimulator(sineSim.get(), config);           // initialize() + set frequency/latency
sineSim->loadSimulation(nullptr, nullptr, nullptr);  // self-contained setup
sim = move(sineSim);
```

**PistonEngine path (lines 58-98):**
```
auto pistonSim = make_unique<PistonEngineSimulator>();
initSimulator(pistonSim.get(), config);         // initialize() + set frequency/latency

// PistonEngine-specific: compile script, extract engine/vehicle/transmission
compiler->compile(scriptPath);
output = compiler->execute();
engine = output.engine;
vehicle = output.vehicle ?: createDefaultVehicle();
transmission = output.transmission ?: createDefaultTransmission();

pistonSim->loadSimulation(engine, vehicle, transmission);

// PistonEngine-specific: load WAV impulse responses
loadImpulseResponses(pistonSim.get(), engine, assetBasePath, logger);

compiler->destroy();
sim = move(pistonSim);
```

Both paths then wrap in BridgeSimulator.

---

## The Asymmetry Problem

### 1. Ownership Inversion
- **SineSimulator**: Creates and OWNS engine/vehicle/transmission internally in `loadSimulation()`
- **PistonEngineSimulator**: Receives engine/vehicle/transmission from EXTERNAL source, does NOT own them

This is the fundamental asymmetry. The factory's `loadSimulation(nullptr, nullptr, nullptr)` for SineSimulator is a semantic lie — the method signature says "load these objects" but SineSimulator ignores them entirely.

### 2. Factory Knows PistonEngine Details
SimulatorFactory contains:
- Piranha compiler creation and script compilation logic (lines 68-76)
- Default vehicle/transmission fallback logic (lines 78-79)
- Impulse response loading orchestration (line 88)
- Engine null-check validation (line 81)

This is PistonEngine-specific domain knowledge that violates SRP. If a third simulator type appeared that needed different setup (e.g., ElectricMotorSimulator), the factory's switch case would grow further.

### 3. initSimulator() is a Free Function
`initSimulator()` is a static free function in SimulatorFactory.cpp (line 25-33). It:
- Creates `Simulator::Parameters` with NsvOptimized
- Calls `sim->initialize(params)`
- Sets simulation frequency, fluid simulation steps, synth latency

This is shared/common logic that could be in the base class or in each subclass's constructor.

### 4. Physics Wiring is Already DRY
`SimulatorInitHelpers::wirePhysics()` and `cleanupPhysics()` already handle shared physics wiring. This is well-extracted.

### 5. The `nullptr` Problem
SineSimulator calling `loadSimulation(nullptr, nullptr, nullptr)` is a code smell. The method contract says "load these objects" but SineSimulator:
- Ignores all three arguments
- Creates its own objects
- Calls `Simulator::loadSimulation(REAL_OBJECTS)` internally

The `nullptr` args exist only to satisfy the virtual method signature.

---

## Proposal: Symmetric Lifecycle via Template Method + Type-Specific Loaders

### Key Insight
Both simulator types share the same three-phase lifecycle:
1. Initialize physics system (`initialize()`)
2. Create/wire engine+vehicle+transmission (`loadSimulation()`)
3. Initialize synthesizer (`initializeSynthesizer()`)

The divergence is in step 2: WHERE the engine/vehicle/transmission come from and WHAT additional wiring is needed.

### Proposal A: Make Each Simulator Type Self-Contained (Recommended)

**Goal**: Factory becomes a thin switch. Each Simulator subclass owns its entire setup, including sourcing its engine/vehicle/transmission.

#### Step 1: Change `loadSimulation()` to take no arguments

Rename the override point. Instead of:
```cpp
void loadSimulation(Engine*, Vehicle*, Transmission*) override;
```

Each subclass gets a no-arg setup method. The base class `loadSimulation()` with args becomes an implementation detail, not the public API:

```cpp
class Simulator {
public:
    // NEW: Subclass override point — "set yourself up"
    virtual void loadSimulation() = 0;

protected:
    // Shared helper — subclasses call this with their engine/vehicle/transmission
    void storeSimulationObjects(Engine* e, Vehicle* v, Transmission* t);
    void initSynthesizer();
};
```

**SineSimulator::loadSimulation()** — no args, creates its own SineEngine/SineVehicle/SineTransmission internally (same as now, just cleaner signature).

**PistonEngineSimulator::loadSimulation()** — no args. But where does it get the engine? This leads to Step 2.

#### Step 2: PistonEngineSimulator Gets a Script Loading Method

Move script compilation OUT of the factory and INTO a PistonEngine-specific class:

```cpp
class PistonEngineSimulator : public Simulator {
public:
    void loadSimulation() override;  // no args

    // NEW: Set the script compilation results before calling loadSimulation()
    void setScriptOutput(Engine* engine, Vehicle* vehicle, Transmission* transmission);
    void setAssetBasePath(const std::string& path);

private:
    Engine* m_scriptEngine = nullptr;
    Vehicle* m_scriptVehicle = nullptr;
    Transmission* m_scriptTransmission = nullptr;
    std::string m_assetBasePath;
};
```

Or more cleanly, encapsulate script compilation into its own class:

```cpp
// NEW: Encapsulates Piranha compiler lifecycle
class PistonEngineLoader {
public:
    struct Result {
        Engine* engine;
        Vehicle* vehicle;
        Transmission* transmission;
    };

    Result compileAndLoad(const std::string& scriptPath, ILogging* logger);
    void destroy();

private:
    std::unique_ptr<es_script::Compiler> m_compiler;
};
```

#### Step 3: Factory Becomes Thin

```cpp
std::unique_ptr<ISimulator> SimulatorFactory::create(
    SimulatorType type,
    const std::string& scriptPath,
    const std::string& assetBasePath,
    const ISimulatorConfig& config,
    ILogging* logger,
    telemetry::ITelemetryWriter* telemetryWriter)
{
    std::unique_ptr<Simulator> sim;

    switch (type) {
        case SimulatorType::SineWave: {
            auto sineSim = std::make_unique<SineSimulator>();
            initSimulator(sineSim.get(), config);
            sineSim->loadSimulation();  // self-contained
            sim = std::move(sineSim);
            break;
        }

        case SimulatorType::PistonEngine: {
            auto pistonSim = std::make_unique<PistonEngineSimulator>();

            // PistonEngine-specific: compile script
            PistonEngineLoader loader;
            auto result = loader.compileAndLoad(scriptPath, logger);
            pistonSim->setScriptOutput(result.engine, result.vehicle, result.transmission);
            pistonSim->setAssetBasePath(assetBasePath);

            initSimulator(pistonSim.get(), config);
            pistonSim->loadSimulation();  // uses stored script output

            loader.destroy();  // compiler no longer needed
            sim = std::move(pistonSim);
            break;
        }
    }

    auto bridgeSim = std::make_unique<BridgeSimulator>(std::move(sim));
    if (type == SimulatorType::PistonEngine && !scriptPath.empty()) {
        bridgeSim->setNameFromScript(scriptPath);
    }
    return bridgeSim;
}
```

This is still not perfectly symmetric — the PistonEngine path has extra steps. But the asymmetry is now **contained**: the factory orchestrates PistonEngine-specific setup via clearly-named setters, and both types end with the same `initSimulator() + loadSimulation()` pattern.

#### Step 4: Impulse Response Loading Moves Into PistonEngineSimulator

`loadImpulseResponses()` is PistonEngine-specific. Move the call from the factory into `PistonEngineSimulator::loadSimulation()`:

```cpp
void PistonEngineSimulator::loadSimulation() {
    // Store script output into base class
    storeSimulationObjects(m_scriptEngine, m_scriptVehicle, m_scriptTransmission);

    // ... existing physics wiring (constraints, cylinders, etc.) ...
    // ... placeAndInitialize() ...

    initSynthesizer();

    // Load impulse responses (was in factory, now encapsulated)
    ScriptLoadHelpers::loadImpulseResponses(this, m_scriptEngine, m_assetBasePath, /* logger */);
}
```

### What This Achieves

| Concern | Before | After |
|---------|--------|-------|
| Engine/vehicle/transmission ownership | SineSimulator owns; factory passes for PistonEngine | Each type sources its own |
| Script compilation | In factory | In `PistonEngineLoader` class |
| Impulse response loading | In factory | In `PistonEngineSimulator::loadSimulation()` |
| `loadSimulation(nullptr,nullptr,nullptr)` | SineSimulator semantic lie | Gone — no-arg override |
| Factory SRP | Violated (knows compiler, impulse responses, defaults) | Thin switch + orchestration |
| Symmetry | Different code shapes | Same `initSimulator() + loadSimulation()` for both |
| Default vehicle/transmission | In factory (ScriptLoadHelpers) | In PistonEngineLoader |
| Adding new simulator type | Must modify factory's switch with type-specific details | Add case, call `initSimulator() + loadSimulation()` |

### What This Does NOT Change

- **BridgeSimulator** — unchanged, still wraps any Simulator
- **ISimulator interface** — unchanged
- **SimulatorInitHelpers** — unchanged, already DRY
- **Upstream engine-sim** — unchanged, we just change how we call it

---

## Alternative: Proposal B — Extract initSimulator() Into Constructor

`initSimulator()` does three things:
1. `sim->initialize(NsvOptimized)` — creates physics system
2. Sets frequency/steps/latency from config

This could move into each Simulator subclass constructor taking a config:

```cpp
class SineSimulator : public Simulator {
public:
    explicit SineSimulator(const ISimulatorConfig& config) {
        Simulator::Parameters p;
        p.systemType = Simulator::SystemType::NsvOptimized;
        initialize(p);
        setSimulationFrequency(config.simulationFrequency);
        setFluidSimulationSteps(config.fluidSimulationSteps);
        setTargetSynthesizerLatency(config.targetSynthesizerLatency);
    }
};
```

This eliminates `initSimulator()` entirely, but couples Simulator subclasses to `ISimulatorConfig` (a bridge-layer type). The upstream `Simulator` knows nothing about `ISimulatorConfig`. This is a layering violation — the engine-sim layer shouldn't depend on the bridge layer's config types.

**Verdict**: Keep `initSimulator()` as a bridge-layer helper. It's the correct seam between layers.

---

## Implementation Sequence (Recommended Order)

1. **Introduce `PistonEngineLoader`** — extract script compilation into its own class (compiler create/compile/execute/destroy lifecycle)
2. **Move `loadImpulseResponses()` call** from factory into `PistonEngineSimulator::loadSimulation()`
3. **Add no-arg `loadSimulation()`** to base class as the primary override point, rename old method to `storeSimulationObjects()` (protected helper)
4. **Update SineSimulator** — `loadSimulation()` becomes no-arg, internal logic unchanged
5. **Update PistonEngineSimulator** — `loadSimulation()` becomes no-arg, uses stored script output
6. **Slim down factory** — remove PistonEngine-specific code, use `PistonEngineLoader`
7. **Tests** — update factory tests, add PistonEngineLoader unit tests, verify sine/piston integration tests pass

Steps 1-2 are independent of steps 3-5 and can be done in parallel. Step 6 depends on both.

---

## Risk Assessment

**Low risk**: Steps 1-2 (PistonEngineLoader extraction) are purely mechanical moves. No behavior change.

**Medium risk**: Steps 3-5 (loadSimulation signature change) touch the virtual method that both subclasses override. The upstream `simulator.cpp` calls `Simulator::loadSimulation(e,v,t)` internally, but only in `releaseSimulation()` which calls `destroy()` first — and we never use `releaseSimulation()` in the bridge. The `loadSimulation(e,v,t)` is only called from our subclass overrides, so the rename is safe.

**No risk to existing tests**: The public API (ISimulator/BridgeSimulator) doesn't change. Only internal Simulator subclass plumbing changes.

---

## Open Questions

1. **Should PistonEngineSimulator own the engine from script compiler?** Currently the script compiler's `output.engine` is a raw pointer with unclear ownership. `PistonEngineSimulator::destroy()` sets `m_engine = nullptr` without deleting. Who deletes it? If the answer is "nobody leaks it" or "compiler->destroy() handles it", that should be documented. If PistonEngineLoader takes ownership, this needs explicit lifecycle management.

2. **Should `initSimulator()` become a protected static on SimulatorBase?** Currently it's a free function in SimulatorFactory.cpp. Making it a protected method on a future `SimulatorBase` would give subclasses direct access without the factory as middleman. But this requires a new base class between `Simulator` (upstream) and the bridge-layer subclasses.

3. **Logger in PistonEngineSimulator**: Moving impulse response loading into `loadSimulation()` means the subclass needs a logger. Currently loadSimulation() has no logger parameter. Options: pass in constructor, store as member, or use a null-safe default.
