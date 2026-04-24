# ISP Config Partitioning Analysis

## 1. Current Config Struct Inventory

| Struct | Location | Owner |
|--------|----------|-------|
| `EngineSimConfig` | `simulator/EngineSimTypes.h` | ISimulator layer |
| `SimulatorFactoryConfig` | `simulator/SimulatorFactory.h` | Factory (creation) |
| `SimulationConfig` | `simulation/SimulationLoop.h` | SimulationLoop (orchestration) |
| `AudioStrategyConfig` | `strategy/IAudioBuffer.h` | IAudioBuffer |
| `AudioStreamFormat` | `hardware/IAudioHardwareProvider.h` | IAudioHardwareProvider |
| `PresentationConfig` | `io/IPresentation.h` | IPresentation |
| `EngineInput` | `io/IInputProvider.h` | IInputProvider |

## 2. Field-by-Field Consumer Trace

### EngineSimConfig (10 fields)

| Field | Who consumes it | Where exactly |
|-------|----------------|---------------|
| `sampleRate` | **ISimulator** (BridgeSimulator), **IAudioBuffer** (via param), **IAudioHardwareProvider** (via AudioStreamFormat) | `BridgeSimulator::renderOnDemand()` line 60: `dt = frames / sampleRate`; `SimulationLoop::runSimulation()` line 372: `audioBuffer->initialize(strategyConfig, config.sampleRate())`; `SimulationLoop::runSimulation()` line 380: `createHardwareProvider(config.sampleRate(), ...)` |
| `inputBufferSize` | **Nobody** | Not read anywhere in current code. Dead field. |
| `audioBufferSize` | **Nobody** | Not read anywhere in current code. Dead field. |
| `simulationFrequency` | **ISimulator** (BridgeSimulator) | `BridgeSimulator::update()` line 50: `advanceFixedSteps(..., engineConfig_.simulationFrequency, ...)` |
| `fluidSimulationSteps` | **SimulatorFactory** (via SimulatorFactoryConfig) | `initSimulator()` line 30: `sim->setFluidSimulationSteps(config.fluidSimulationSteps)`. **Also in SimulatorFactoryConfig** -- duplicate. |
| `maxChunkFrames` | **ISimulator** (BridgeSimulator) | `BridgeSimulator::initAudioConfig()` line 163: `ensureAudioConversionBufferSize(maxChunkFrames)`; `drainSynthesizerBuffer()` line 207 |
| `targetSynthesizerLatency` | **IAudioBuffer** (via AudioStrategyConfig) | `SimulationLoop::runSimulation()` line 369: `strategyConfig.synthLatency = config.engineConfig->targetSynthesizerLatency`; then `ThreadedStrategy::initialize()` uses it for cursor chasing |
| `volume` | **ISimulator** (BridgeSimulator) | `BridgeSimulator::renderOnDemand()` line 66 and `readAudioBuffer()` line 83: `convertInt16ToStereoFloat(..., engineConfig_.volume, ...)` |
| `convolutionLevel` | **ISimulator** (BridgeSimulator) | Same lines as volume -- `convertInt16ToStereoFloat(..., ..., engineConfig_.convolutionLevel)` |
| `airNoise` | **Nobody** | Not read anywhere in current code. Dead field. |

### SimulatorFactoryConfig (7 fields)

| Field | Who consumes it | Where exactly |
|-------|----------------|---------------|
| `type` | **SimulatorFactory** | `SimulatorFactory::create()` switch statement |
| `scriptPath` | **SimulatorFactory** | Compiled and loaded for PistonEngine type |
| `assetBasePath` | **SimulatorFactory** | Used to resolve impulse response paths |
| `sampleRate` | **Nobody (in factory)** | Set by CLI but never read by initSimulator(). Dead in factory context. |
| `simulationFrequency` | **SimulatorFactory** (initSimulator) | `initSimulator()` line 30: `sim->setSimulationFrequency(config.simulationFrequency)` |
| `synthLatency` | **SimulatorFactory** (initSimulator) | `initSimulator()` line 32: `sim->setTargetSynthesizerLatency(config.synthLatency)` |
| `fluidSimulationSteps` | **SimulatorFactory** (initSimulator) | `initSimulator()` line 31: `sim->setFluidSimulationSteps(config.fluidSimulationSteps)` |

### SimulationConfig (13 fields)

| Field | Who consumes it | Where exactly |
|-------|----------------|---------------|
| `configPath` | Presentation (ShowConfigHeader) | Display only |
| `assetBasePath` | Nobody | Set but not consumed |
| `duration` | SimulationLoop | `runUnifiedAudioLoop()`: break condition at line 304 |
| `interactive` | PresentationConfig derivation, input provider creation | `createInputProvider()`, `createPresentation()` |
| `playAudio` | SimulationLoop | Warmup drain decision |
| `volume` | IAudioHardwareProvider | `hardwareProvider->setVolume(config.volume)` |
| `syncPull` | CLI main (strategy selection), ShowConfigHeader | Determines AudioMode |
| `targetRPM` | Nobody (display only) | ShowConfigHeader |
| `targetLoad` | Nobody (display only) | ShowConfigHeader |
| `useDefaultEngine` | Nobody (consumed at CLI level) | Used to choose script path |
| `outputWav` | Nobody (warning only) | `warnWavExportNotSupported()` |
| `preFillMs` | Nobody | Set but not consumed in SimulationLoop |
| `engineConfig` (ptr) | BridgeSimulator, strategies | `initializeSimulator()` passes to `simulator.create()` |
| `simulatorLabel` | Logging | `logger->info(...)` |

### AudioStrategyConfig (2 fields)

| Field | Who consumes it | Where exactly |
|-------|----------------|---------------|
| `channels` | ThreadedStrategy, SyncPullStrategy | Logging only |
| `synthLatency` | ThreadedStrategy | `synthLatency_ = config.synthLatency` for cursor chasing |

### AudioStreamFormat (5 fields)

| Field | Who consumes it | Where exactly |
|-------|----------------|---------------|
| `sampleRate` | IAudioHardwareProvider | `CoreAudioProvider::initialize()` configures hardware |
| `channels` | IAudioHardwareProvider | Stereo (always 2) |
| `bitsPerSample` | IAudioHardwareProvider | Always 32 (float) |
| `isFloat` | IAudioHardwareProvider | Always true |
| `isInterleaved` | IAudioHardwareProvider | Always true |

## 3. ISP Violations Found

### Violation 1: EngineSimConfig is a god config

EngineSimConfig bundles fields consumed by three different pluggable components:
- **ISimulator fields**: `simulationFrequency`, `maxChunkFrames`, `volume`, `convolutionLevel`
- **IAudioBuffer field**: `targetSynthesizerLatency` (handed off via AudioStrategyConfig)
- **IAudioHardwareProvider field**: `sampleRate` (handed off via AudioStreamFormat)
- **Dead fields**: `inputBufferSize`, `audioBufferSize`, `airNoise`

This violates ISP because BridgeSimulator receives `targetSynthesizerLatency` (which it never uses) and IAudioBuffer receives it only after it's been manually extracted from a struct it doesn't own.

### Violation 2: sampleRate exists in 4 places

`sampleRate` appears in:
1. `EngineSimConfig::sampleRate` -- read by BridgeSimulator for dt calculation
2. `SimulatorFactoryConfig::sampleRate` -- set by CLI but **never read** by factory
3. `AudioStreamFormat::sampleRate` -- set from `config.sampleRate()` in SimulationLoop
4. Passed as parameter to `IAudioBuffer::initialize(config, sampleRate)` -- separate from both configs

This is a 4-way duplication of the same value, with implicit contracts that they're all the same number.

### Violation 3: SimulatorFactoryConfig is an override shadow of EngineSimConfig

`SimulatorFactoryConfig` duplicates three fields from `EngineSimConfig`:
- `simulationFrequency` -- in both
- `synthLatency` / `targetSynthesizerLatency` -- in both
- `fluidSimulationSteps` -- in both
- `sampleRate` -- in both (but dead in factory)

The CLI (`CLIMain.cpp` lines 131-134) manually copies values from EngineSimConfig into SimulatorFactoryConfig:
```cpp
factoryConfig.sampleRate = config.engineConfig->sampleRate;
factoryConfig.simulationFrequency = config.engineConfig->simulationFrequency;
factoryConfig.synthLatency = config.engineConfig->targetSynthesizerLatency;
factoryConfig.fluidSimulationSteps = config.engineConfig->fluidSimulationSteps;
```

This is a textbook violation: two configs carrying the same data, with manual synchronization.

### Violation 4: SimulationConfig is an orchestration god

`SimulationConfig` aggregates:
- Loop parameters (`duration`, `interactive`, `syncPull`, `preFillMs`)
- Display parameters (`simulatorLabel`, `configPath`)
- Audio output parameters (`volume`, `playAudio`, `outputWav`)
- Simulation control (`targetRPM`, `targetLoad`, `useDefaultEngine`)
- An owned `EngineSimConfig*` that it passes through to BridgeSimulator

It acts as a "bag of everything" rather than a focused config for its component.

## 4. Proposed ISP-Compliant Partitioning

### Principle: Each pluggable component gets exactly the config it needs.

### Proposed Structure

```
ISimulatorConfig           -- consumed by ISimulator::create()
  simulationFrequency
  maxChunkFrames
  volume
  convolutionLevel

AudioBufferConfig          -- consumed by IAudioBuffer::initialize()
  channels
  synthLatency

AudioStreamFormat          -- already exists, consumed by IAudioHardwareProvider::initialize()
  sampleRate               -- THIS is where sampleRate lives
  channels
  bitsPerSample
  isFloat
  isInterleaved

SimulationLoopConfig       -- consumed by runSimulation()
  duration
  interactive
  playAudio
  syncPull
  volume                   -- for hardware provider
  preFillMs
  configPath
  simulatorLabel
  targetRPM
  targetLoad
  useDefaultEngine
  outputWav

SimulatorFactoryConfig     -- consumed by SimulatorFactory::create()
  type
  scriptPath
  assetBasePath
  simulationFrequency      -- needed by factory to init Simulator subclass
  fluidSimulationSteps     -- needed by factory to init Simulator subclass
  synthLatency             -- needed by factory to init Simulator subclass
```

### 5. Can EngineSimConfig and SimulatorFactoryConfig Be Merged?

**No -- but for the right reason.** They serve genuinely different roles:

- **SimulatorFactoryConfig** is a *creation recipe*: "build me a SineWave from this script, with these physics overrides." It's consumed once, at construction time, by the factory.
- **EngineSimConfig** (renamed to ISimulatorConfig) is a *runtime config*: "here's what the simulator needs to operate." It's consumed by BridgeSimulator's `create()` and stored for the lifetime of the simulator.

The problem isn't that both exist. The problem is that three fields (`simulationFrequency`, `synthLatency`, `fluidSimulationSteps`) exist in BOTH, and the CLI manually copies from one to the other.

**The fix**: SimulatorFactoryConfig should contain ONLY creation concerns. After the factory creates and wires the Simulator subclass (setting simulationFrequency, fluidSimulationSteps, synthLatency directly on the Simulator via `initSimulator()`), those values should NOT also need to appear in ISimulatorConfig. BridgeSimulator should get simulationFrequency from the Simulator it already wraps, not from a config.

But there's a practical constraint: `Simulator::getSimulationFrequency()` may not exist in the upstream engine-sim API. The Simulator is an opaque object from the engine-sim submodule. So BridgeSimulator stores `simulationFrequency` to compute `advanceFixedSteps()`. This means ISimulatorConfig genuinely needs `simulationFrequency`.

**Resolution**: The factory should CREATE ISimulatorConfig internally (not receive duplicated values). The CLI passes one set of values; the factory distributes them to the right places.

### 6. The sampleRate Problem

`sampleRate` is needed by:
1. **ISimulator** (BridgeSimulator): `dt = frames / sampleRate` for advanceFixedSteps
2. **IAudioBuffer**: passed as a parameter to `initialize()`
3. **IAudioHardwareProvider**: via AudioStreamFormat

**ISP answer**: `sampleRate` belongs to the audio pipeline, not the simulator. The simulator's physics runs at `simulationFrequency`, not `sampleRate`. BridgeSimulator only needs `sampleRate` for `renderOnDemand()` where it converts frame counts to time deltas.

Options:
1. **(Recommended)** Pass `sampleRate` as a parameter to `renderOnDemand()` instead of storing it in the config. The caller (SyncPullStrategy) already has it. This eliminates sampleRate from ISimulatorConfig entirely.
2. **(Alternative)** Keep sampleRate in ISimulatorConfig but accept it's shared. The factory sets it once; everyone reads from one source.

Option 1 is cleaner but requires changing the `ISimulator` interface signature. Option 2 is pragmatic and requires less refactoring.

### 7. Recommended Action Plan

**Phase 1: Kill dead fields**
- Remove `inputBufferSize`, `audioBufferSize`, `airNoise` from EngineSimConfig
- Remove `sampleRate` from SimulatorFactoryConfig (dead in factory context)

**Phase 2: Rename EngineSimConfig -> ISimulatorConfig**
- Move to `simulator/ISimulatorConfig.h`
- Remove `targetSynthesizerLatency` (belongs to AudioBufferConfig)
- Remove `sampleRate` (either move to renderOnDemand param or keep with documentation)

**Phase 3: Merge AudioStrategyConfig into AudioBufferConfig**
- Rename `AudioStrategyConfig` -> `AudioBufferConfig`
- It becomes the single config for IAudioBuffer

**Phase 4: Make factory build ISimulatorConfig internally**
- CLI passes ONE set of values (the "raw" values from CommandLineArgs)
- Factory creates ISimulatorConfig for BridgeSimulator, sets physics values on Simulator, and returns the fully-wired ISimulator
- Eliminates the manual copy in CLIMain.cpp lines 131-134

**Phase 5: sampleRate ownership**
- Either pass as parameter to `renderOnDemand()` (cleanest ISP)
- Or document that ISimulatorConfig.sampleRate is a "cross-cutting" value shared with audio pipeline

### 8. Final Tally: From 7 Configs to 6 ISP-Compliant Configs

| Config | Owner | Fields |
|--------|-------|--------|
| `ISimulatorConfig` | ISimulator | simulationFrequency, maxChunkFrames, volume, convolutionLevel, (sampleRate?) |
| `SimulatorFactoryConfig` | SimulatorFactory | type, scriptPath, assetBasePath, simulationFrequency, fluidSimulationSteps, synthLatency |
| `AudioBufferConfig` | IAudioBuffer | channels, synthLatency |
| `AudioStreamFormat` | IAudioHardwareProvider | sampleRate, channels, bitsPerSample, isFloat, isInterleaved |
| `SimulationLoopConfig` | SimulationLoop | duration, interactive, playAudio, syncPull, volume, preFillMs, configPath, simulatorLabel, targetRPM, targetLoad, useDefaultEngine, outputWav |
| `PresentationConfig` | IPresentation | interactive, duration, showProgress, showDiagnostics |

Each config is consumed by exactly one component. The factory is the distribution point -- it takes raw creation params and produces the right configs for the right components.

**Overlap remaining**: `simulationFrequency` appears in both `SimulatorFactoryConfig` (creation) and `ISimulatorConfig` (runtime). This is unavoidable because the factory needs it to initialize the Simulator subclass, and BridgeSimulator needs it at runtime. The factory should build ISimulatorConfig from its own values rather than requiring external duplication.
