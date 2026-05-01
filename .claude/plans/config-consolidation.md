# Config Consolidation Plan

## 1. Current State: Field Duplication Matrix

| Field | EngineSimConfig | SimulatorConfig | SimulationConfig | AudioStrategyConfig | PresentationConfig | BridgeSimulator members |
|-------|:-:|:-:|:-:|:-:|:-:|:-:|
| sampleRate | `int32_t` | `int` | `int` | `int` | | `sampleRate_` |
| simulationFrequency | `int32_t` | `int` | `int` | | | `simulationFrequency_` |
| synthLatency / targetSynthesizerLatency | `double` (as targetSynthesizerLatency) | `double` | `double` | `double` | | |
| fluidSimulationSteps | `int32_t` | `int` | `int` | | | |
| volume | `float` | | `float` | | | `volume_` |
| convolutionLevel | `float` | | | | | `convolutionLevel_` |
| airNoise | `float` | | | | | |
| interactive | | | `bool` | | `bool` | |
| duration | | | `double` | | `double` | |
| maxChunkFrames | `int32_t` | | | | | `maxChunkFrames_` |
| configPath / scriptPath | | `string` (scriptPath) | `string` (configPath) | | | |
| assetBasePath | | `string` | `string` | | | |
| inputBufferSize | `int32_t` | | | | | |
| audioBufferSize | `int32_t` | | | | | |

## 2. Identified Problems

### P1: Four-way duplication of sampleRate / simulationFrequency / synthLatency / fluidSimulationSteps

These four fields appear in **EngineSimConfig**, **SimulatorConfig**, **SimulationConfig**, and (sampleRate/synthLatency only) **AudioStrategyConfig**. The CLI copies them field-by-field from `SimulationConfig` to `SimulatorConfig` (CLIMain.cpp:126-128). SimulationLoop copies them from `SimulationConfig` to `EngineSimConfig` (line 211-213) and to `AudioStrategyConfig` (lines 367-369). This is pure relay -- none of these structs add value by having their own copy.

### P2: SimulatorConfig is a near-subset of EngineSimConfig

`SimulatorConfig` has: type, scriptPath, assetBasePath, sampleRate, simulationFrequency, synthLatency, fluidSimulationSteps. Of those 7 fields, 4 are duplicates of `EngineSimConfig`. The unique fields are `type`, `scriptPath`, `assetBasePath`. SimulatorConfig exists only as a factory parameter bag.

### P3: SimulationConfig is a god config with mixed responsibilities

`SimulationConfig` carries:
- **Presentation concerns**: `interactive`, `duration`, `playAudio`, `outputWav`, `simulatorLabel`
- **Audio concerns**: `volume`, `syncPull`, `sampleRate`, `synthLatency`, `framesPerUpdate`, `updateInterval`, `preFillMs`
- **Simulation concerns**: `simulationFrequency`, `fluidSimulationSteps`, `targetRPM`, `targetLoad`, `useDefaultEngine`
- **Routing concerns**: `configPath`, `assetBasePath`

This violates ISP -- the struct mixes layers.

### P4: BridgeSimulator copies fields from EngineSimConfig into separate members

`initAudioConfig()` (BridgeSimulator.cpp:159-166) copies 5 fields from `EngineSimConfig` into `sampleRate_`, `simulationFrequency_`, `maxChunkFrames_`, `volume_`, `convolutionLevel_`. These could be read from a stored `EngineSimConfig` directly.

### P5: `config.interactive` exists on both SimulationConfig and PresentationConfig

In CLIMain.cpp:72, `SimulationConfig.interactive` is set from `args.interactive`. Separately in CLIMain.cpp:53, `PresentationConfig.interactive` is also set from `args.interactive`. These are always the same value. The question of "who owns interactive" is unclear.

### P6: `duration` exists on both SimulationConfig and PresentationConfig

`SimulationConfig.duration = 3.0` (default). `PresentationConfig.duration = 0.0` (default, 0 = infinite). Different defaults, same concept, confusing.

### P7: SimulationConfig.sampleRate is never populated -- latent bug

`CreateSimulationConfig()` in CLIMain.cpp never assigns `config.sampleRate`. It defaults to 0. Line 87 computes `framesPerUpdate = 0 / 60 = 0`. SimulationLoop asserts `config.sampleRate > 0` and `config.framesPerUpdate > 0`. The code only works because `initializeSimulator()` creates its own `EngineSimConfig` from defaults, bypassing the broken SimulationConfig values.

## 3. Consolidation Plan

### Principle: Config structs follow the dependency layer (ISP)

Each config struct owns ONLY the fields consumed by its layer. No field appears in more than one struct. "Computed" or "derived" fields (updateInterval, framesPerUpdate) are computed from the source-of-truth config, not stored separately.

### Proposed struct layout:

```
EngineSimConfig          (layer: simulator/audio hardware -- the SOURCE OF TRUTH)
  |- sampleRate, simulationFrequency, fluidSimulationSteps
  |- maxChunkFrames, targetSynthesizerLatency
  |- inputBufferSize, audioBufferSize
  |- volume, convolutionLevel, airNoise

SimulatorFactoryConfig   (layer: factory -- RENAMED from SimulatorConfig)
  |- type: SimulatorType
  |- scriptPath, assetBasePath
  |- simulationFrequency override (0 = use EngineSimConfig default)
  |- synthLatency override (0 = use EngineSimConfig default)
  |- fluidSimulationSteps override (0 = use EngineSimConfig default)
  |- sampleRate override (0 = use EngineSimConfig default)

SimulationConfig         (layer: orchestration -- SLIMMED DOWN)
  |- configPath           (routing: which engine script)
  |- duration             (how long to run)
  |- interactive          (input mode)
  |- playAudio            (hardware output enable)
  |- syncPull             (strategy selection)
  |- outputWav            (file output)
  |- targetRPM            (initial RPM)
  |- targetLoad           (initial load)
  |- useDefaultEngine     (engine selection)
  |- preFillMs            (warmup tuning)
  |- simulatorLabel       (display name)
  |- const EngineSimConfig& engineConfig   (REFERENCED, not copied)

AudioStrategyConfig      (layer: audio strategy)
  |- synthLatency         (from EngineSimConfig)
  |- channels             (always 2, could be a constant)

PresentationConfig       (layer: presentation -- UNCHANGED)
  |- interactive          (from SimulationConfig -- no longer duplicated, passed through)
  |- duration             (from SimulationConfig)
  |- showProgress
  |- showDiagnostics
```

### Key design decisions:

**A. EngineSimConfig is the single source of truth for all audio/simulation constants.**
No other struct carries `sampleRate`, `simulationFrequency`, `synthLatency`, or `fluidSimulationSteps` as "real" values. Other structs may carry optional overrides (0-sentinel), resolved at the factory/config level.

**B. SimulationConfig holds a `const EngineSimConfig&` (reference), not copies.**
Eliminates the `sampleRate` duplication, the `simulationFrequency` duplication, and the `synthLatency` duplication. SimulationLoop reads `config.engineConfig.sampleRate` instead of `config.sampleRate`. The computed fields (`updateInterval`, `framesPerUpdate`) become inline helper methods or are computed once at construction.

**C. SimulatorConfig renamed to `SimulatorFactoryConfig`.**
Different name from SimulationConfig, eliminating confusion. Its override fields stay (they're 0-sentinel, resolved by `initSimulator()`), but it no longer duplicates fields that come from EngineSimConfig. Instead, the factory receives the EngineSimConfig reference and applies overrides from SimulatorFactoryConfig.

**D. BridgeSimulator stores `EngineSimConfig` directly, not copied members.**
Replace `sampleRate_`, `simulationFrequency_`, `maxChunkFrames_`, `volume_`, `convolutionLevel_` with a single `EngineSimConfig engineConfig_` member. Update all internal usage to read from `engineConfig_.sampleRate` etc.

**E. `interactive` ownership: SimulationConfig owns it, PresentationConfig receives it.**
SimulationConfig is the orchestration layer config. It decides the mode. PresentationConfig gets its `interactive` value set FROM SimulationConfig, not from args directly. One source of truth.

**F. `duration` ownership: SimulationConfig owns it, PresentationConfig receives it.**
Same as interactive. PresentationConfig.duration is set from SimulationConfig.duration at construction time.

**G. AudioStrategyConfig loses `sampleRate`.**
Strategies receive sampleRate from the EngineSimConfig they're initialized with (passed through SimulationConfig.engineConfig). AudioStrategyConfig keeps only `synthLatency` (strategy-specific tuning) and `channels` (could even be a constant).

### Implementation steps (ordered by dependency):

#### Step 1: BridgeSimulator stores EngineSimConfig directly
- Replace `sampleRate_`, `simulationFrequency_`, `maxChunkFrames_`, `volume_`, `convolutionLevel_` with `EngineSimConfig engineConfig_`
- Update `initAudioConfig()` to store the whole config
- Update all usages in `.cpp` to read from `engineConfig_.xxx`
- **Risk**: Low -- internal refactor only, no API change

#### Step 2: SimulationConfig gains EngineSimConfig reference, loses duplicated fields
- Add `const EngineSimConfig* engineConfig = nullptr` (pointer, not reference, for move semantics)
- Remove: `sampleRate`, `simulationFrequency`, `synthLatency`, `fluidSimulationSteps`, `updateInterval`, `framesPerUpdate`
- Add computed accessors or have callers compute once from engineConfig
- Fix the CLIMain.cpp `CreateSimulationConfig()` to create an EngineSimConfig and store it
- **Risk**: Medium -- touches CLI, SimulationLoop, and all callers

#### Step 3: SimulationLoop reads from config.engineConfig instead of config.sampleRate etc.
- Update `initializeSimulator()` to pass `config.engineConfig` (or merge overrides into it)
- Update `AudioStrategyConfig` construction to read from `config.engineConfig`
- Update hardware provider creation
- **Risk**: Medium -- depends on Step 2

#### Step 4: Rename SimulatorConfig to SimulatorFactoryConfig
- Rename struct, update all references
- This is a pure rename, no logic change
- **Risk**: Low

#### Step 5: Resolve interactive/duration ownership
- PresentationConfig gets its values FROM SimulationConfig, not from args
- CLIMain.cpp creates PresentationConfig from SimulationConfig
- **Risk**: Low

#### Step 6: AudioStrategyConfig loses sampleRate
- Strategies receive it from EngineSimConfig (passed via initialize or stored)
- AudioStrategyConfig keeps synthLatency and channels only
- **Risk**: Low -- strategies already have logger/telemetry pattern, add EngineSimConfig param

### What NOT to merge:

- **AudioLoopConfig**: Only contains loop iteration constants (WARMUP_ITERATIONS etc). These are truly constants, not config. Leave as-is.
- **CommandLineArgs**: CLI-layer only, all 0-sentinel. Correct layer separation. Leave as-is.

### Files touched (estimate):

| File | Change |
|------|--------|
| `engine-sim-bridge/include/simulator/BridgeSimulator.h` | Replace 5 members with EngineSimConfig |
| `engine-sim-bridge/src/simulator/BridgeSimulator.cpp` | Read from engineConfig_ |
| `engine-sim-bridge/include/simulation/SimulationLoop.h` | Remove duplicated fields, add engineConfig pointer |
| `engine-sim-bridge/src/simulation/SimulationLoop.cpp` | Read from config.engineConfig, fix initializeSimulator |
| `engine-sim-bridge/include/simulator/SimulatorFactory.h` | Rename to SimulatorFactoryConfig |
| `src/config/CLIMain.cpp` | Create EngineSimConfig, wire into SimulationConfig |
| `engine-sim-bridge/include/strategy/IAudioBuffer.h` | Remove sampleRate from AudioStrategyConfig |
| `engine-sim-bridge/src/strategy/ThreadedStrategy.cpp` | Receive sampleRate via different path |
| `engine-sim-bridge/src/strategy/SyncPullStrategy.cpp` | Same |
| `escli-ios/EngineSimApp/EngineSimWrapper.mm` | Update SimulatorFactoryConfig rename |
| Tests | Update any test that constructs these configs |
