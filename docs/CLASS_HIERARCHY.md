# Class Hierarchy Diagram
## Complete Relationship Map of All Classes

**Last Updated:** 2026-04-15
**Status:** Reflects current codebase state (post ARCH-001/003/004 completion)

---

## Inheritance Hierarchy

```
IAudioStrategy (Interface)
    |
    +-- ThreadedStrategy (cursor-chasing mode with circular buffer)
    |
    +-- SyncPullStrategy (lock-step mode with on-demand rendering)


IAudioHardwareProvider (Interface)
    |
    +-- CoreAudioHardwareProvider (macOS CoreAudio AudioUnit wrapper)


IPresentation (Interface)
    |
    +-- ConsolePresentation


IInputProvider (Interface)
    |
    +-- KeyboardInputProvider


IAudioSource (Interface, in src/AudioSource.h)
    |
    +-- BaseAudioSource
        |
        +-- EngineAudioSource


audio::IAudioSource (Interface, in src/audio/common/IAudioSource.h)
    (NOT currently used by SimulationLoop -- separate from above IAudioSource)
```

---

## Composition/Aggregation Relationships

### BufferContext Composition (Current -- replaces StrategyContext/AudioUnitContext)

```
BufferContext
|-- AudioState
|   |-- isPlaying: atomic<bool>
|   +-- sampleRate: int
|
|-- BufferState
|   |-- writePointer: atomic<int>
|   |-- readPointer: atomic<int>
|   |-- underrunCount: atomic<int>
|   |-- fillLevel: int
|   +-- capacity: int
|   |-- availableFrames(): int
|   +-- freeSpace(): int
|
|-- Diagnostics
|   |-- lastRenderMs: atomic<double>
|   |-- lastHeadroomMs: atomic<double>
|   |-- lastBudgetPct: atomic<double>
|   |-- lastFrameBudgetPct: atomic<double>
|   +-- totalFramesRendered: atomic<int64_t>
|   |-- recordRender()
|   |-- reset()
|   +-- getSnapshot() -> Snapshot
|
|-- CircularBuffer* (non-owning, owned by AudioPlayer)
|
|-- IAudioStrategy* (non-owning, set by AudioPlayer)
|
|-- EngineSimHandle (engine simulator handle)
|
+-- const EngineSimAPI* (engine simulator API)
```

### AudioPlayer Composition (Current)

```
AudioPlayer
|-- BufferContext context_ (owned)
|-- CircularBuffer circularBuffer_ (owned)
|-- unique_ptr<IAudioHardwareProvider> hardwareProvider_ (owned)
|-- IAudioStrategy* strategy_ (injected, not owned)
|-- unique_ptr<ILogging> defaultLogger_ (owned if created internally)
|-- ILogging* logger_ (non-null, points to default or injected)
|-- bool isPlaying_
+-- int sampleRate_
```

### SimulationConfig Composition (Current)

```
SimulationConfig
|-- string configPath
|-- string assetBasePath
|-- double duration
|-- bool interactive
|-- bool playAudio
|-- float volume
|-- bool sineMode
|-- bool syncPull
|-- double targetRPM
|-- double targetLoad
|-- bool useDefaultEngine
|-- const char* outputWav
|-- int simulationFrequency
|-- int preFillMs
|-- ILogging* logger
+-- telemetry::ITelemetryWriter* telemetryWriter (always nullptr currently)
```

---

## Dependency Graph

### CLIMain Dependencies (Current)

```
CLIMain::main()
|-- CommandLineArgs (from CLIconfig)
|-- SimulationConfig (from SimulationLoop.h)
|-- IAudioStrategyFactory::createStrategy()
|   +-- AudioMode
|       |-- ThreadedStrategy
|       +-- SyncPullStrategy
|-- createInputProvider() -> IInputProvider*
|-- createPresentation() -> IPresentation*
+-- runSimulation(config, engineAPI, strategy, inputProvider, presentation)
```

### SimulationLoop Dependencies (Current)

```
SimulationLoop::runUnifiedAudioLoop()
|-- AudioPlayer
|   |-- unique_ptr<IAudioHardwareProvider> (CoreAudioHardwareProvider)
|   +-- BufferContext
|       |-- AudioState
|       |-- BufferState
|       |-- Diagnostics
|       +-- CircularBuffer
|
|-- IAudioStrategy (ThreadedStrategy/SyncPullStrategy)
|   +-- BufferContext*
|
|-- IAudioSource
|   +-- EngineAudioSource
|       +-- EngineSimAPI
|
|-- IInputProvider
|   +-- KeyboardInputProvider
|
|-- IPresentation
|   +-- ConsolePresentation
|
+-- telemetry::ITelemetryWriter (nullptr currently)
```

### Audio Layer Dependencies (Current)

```
IAudioStrategy (Interface)
|-- BufferContext*
|   |-- AudioState
|   |-- BufferState
|   |-- Diagnostics
|   |-- CircularBuffer*
|   |-- IAudioStrategy*
|   |-- EngineSimHandle
|   +-- const EngineSimAPI*
+-- ILogging*

IAudioHardwareProvider (Interface)
|-- AudioStreamFormat
|-- PlatformAudioBufferList
|-- AudioHardwareState
+-- ILogging*

CoreAudioHardwareProvider
|-- AudioUnit (CoreAudio)
|-- AudioDeviceID
|-- AudioCallback (std::function)
|-- BufferContext* (for callback invocation)
+-- ILogging*
```

---

## File-Component Mapping

### Audio Strategy Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/strategies/IAudioStrategy.h | IAudioStrategy, IAudioStrategyFactory, AudioMode, AudioStrategyConfig | Strategy interface, factory, and types |
| src/audio/strategies/IAudioStrategyFactory.cpp | IAudioStrategyFactory | Factory implementation |
| src/audio/strategies/ThreadedStrategy.h/.cpp | ThreadedStrategy | Cursor-chasing mode with circular buffer |
| src/audio/strategies/SyncPullStrategy.h/.cpp | SyncPullStrategy | Lock-step mode with on-demand rendering |

### Audio Hardware Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/hardware/IAudioHardwareProvider.h | IAudioHardwareProvider, AudioStreamFormat, PlatformAudioBufferList, AudioHardwareState, AudioHardwareProviderFactory | Hardware abstraction and supporting types |
| src/audio/hardware/AudioHardwareProviderFactory.cpp | AudioHardwareProviderFactory | Factory for creating providers |
| src/audio/hardware/CoreAudioHardwareProvider.h/.cpp | CoreAudioHardwareProvider | macOS CoreAudio implementation |

### Audio State Management

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/state/BufferContext.h | BufferContext | Composed context for audio strategies |
| src/audio/state/AudioState.h | AudioState | Audio playback state |
| src/audio/state/BufferState.h | BufferState | Circular buffer state |
| src/audio/state/Diagnostics.h | Diagnostics | Performance metrics |

### Audio Common Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/common/CircularBuffer.h/.cpp | CircularBuffer | Ring buffer implementation |
| src/audio/common/IAudioSource.h | audio::IAudioSource | Audio source interface (NOT used by SimulationLoop) |

### Core Audio Components

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/AudioPlayer.h/.cpp | AudioPlayer | Orchestrates playback with injected strategy + hardware provider |
| src/AudioSource.h/.cpp | IAudioSource, BaseAudioSource, EngineAudioSource | Audio source abstraction (used by SimulationLoop) |

### Simulation Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/simulation/SimulationLoop.h/.cpp | SimulationConfig, runSimulation(), runUnifiedAudioLoop() | Main simulation loop |
| src/simulation/EngineConfig.h/.cpp | EngineConfig, EngineSimConfig | Engine configuration |

### CLI/Config Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/config/CLIMain.h/.cpp | main(), CreateSimulationConfig() | Entry point |
| src/config/CLIconfig.h/.cpp | CommandLineArgs, parseArguments() | CLI parsing |
| src/config/ANSIColors.h/.cpp | ANSIColors | Console color helpers |

### Presentation Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/presentation/IPresentation.h | IPresentation, EngineState, PresentationConfig | Presentation interface |
| src/presentation/ConsolePresentation.h/.cpp | ConsolePresentation | Console output |

### Input Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/input/IInputProvider.h | IInputProvider, EngineInput | Input interface |
| src/input/KeyboardInput.h/.cpp | KeyboardInput | Keyboard input |
| src/input/KeyboardInputProvider.h/.cpp | KeyboardInputProvider | Keyboard input provider |

### Bridge Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/bridge/engine_sim_loader.h | EngineSimAPI, EngineSimHandle, EngineSimResult | Engine simulator C API bridge |

---

## Deleted Components (No Longer in Codebase)

These files/directories have been removed as part of the architecture refactor:

| Component | Status | Replaced By |
|-----------|--------|-------------|
| src/audio/adapters/StrategyAdapter.h | DELETED | Not needed (strategies directly unified) |
| src/audio/adapters/StrategyAdapter.cpp | DELETED | Not needed |
| src/audio/adapters/StrategyAdapterFactory.h | DELETED | IAudioStrategyFactory |
| src/audio/renderers/IAudioRenderer.h | DELETED | IAudioStrategy |
| src/audio/renderers/ (all files) | DELETED | src/audio/strategies/ |
| src/audio/modes/ (all files) | DELETED | src/audio/strategies/ |
| AudioUnitContext (struct) | DELETED | BufferContext |
| StrategyContext (struct) | RENAMED | BufferContext |

---

## Key Architecture Patterns

### Strategy Pattern
- **IAudioStrategy**: Abstracts audio generation strategies
- **ThreadedStrategy**: Cursor-chasing with circular buffer
- **SyncPullStrategy**: On-demand synchronous rendering
- **IAudioStrategyFactory**: Creates strategies by AudioMode enum

### Factory Pattern
- **IAudioStrategyFactory**: Creates IAudioStrategy implementations
- **AudioHardwareProviderFactory**: Creates IAudioHardwareProvider implementations

### Dependency Injection
- **IAudioStrategy**: Injected into AudioPlayer and SimulationLoop
- **IAudioHardwareProvider**: Owned by AudioPlayer
- **IInputProvider**: Injected into SimulationLoop
- **IPresentation**: Injected into SimulationLoop
- **ILogging**: Injected throughout codebase

### Composition Pattern
- **BufferContext**: Composed of AudioState, BufferState, Diagnostics, CircularBuffer
- Replaces monolithic AudioUnitContext god struct

---

## SOLID Principles Assessment (Current State)

### Single Responsibility Principle (SRP)

| Component | Compliance | Notes |
|-----------|------------|-------|
| IAudioStrategy | PASS | One responsibility: audio generation strategy |
| ThreadedStrategy | PASS | Cursor-chasing rendering only |
| SyncPullStrategy | PASS | Lock-step rendering only |
| IAudioHardwareProvider | PASS | Platform hardware abstraction |
| CoreAudioHardwareProvider | PASS | CoreAudio AudioUnit lifecycle |
| BufferContext | PASS | Composes state, no logic |
| AudioState | PASS | Playback state only |
| BufferState | PASS | Buffer pointers and counters only |
| Diagnostics | PASS | Performance metrics only |
| CircularBuffer | PASS | Ring buffer storage only |
| AudioPlayer | PASS | Orchestrates strategy + hardware (was borderline, now delegates) |
| SimulationLoop | BORDERLINE | Orchestration + some inline helper logic |

### Open/Closed Principle (OCP)

| Component | Compliance | Notes |
|-----------|------------|-------|
| IAudioStrategy | PASS | New strategies added without modification |
| IAudioHardwareProvider | PASS | New platforms added without modification |
| SimulationLoop | PASS | Extensible via injected strategy/input/presentation |
| IAudioStrategyFactory | PASS | Switch statement in factory (minor OCP tension) |

### Liskov Substitution Principle (LSP)

| Interface | Compliance | Notes |
|-----------|------------|-------|
| IAudioStrategy | PASS | ThreadedStrategy/SyncPullStrategy fully substitutable |
| IAudioHardwareProvider | PASS | CoreAudioHardwareProvider substitutable |
| IInputProvider | PASS | All implementations honor contracts |
| IPresentation | PASS | All implementations honor contracts |

### Interface Segregation Principle (ISP)

| Interface | Compliance | Notes |
|-----------|------------|-------|
| IAudioStrategy | PASS | Focused interface for audio generation |
| IAudioHardwareProvider | PASS | Focused interface for hardware operations |
| IInputProvider | PASS | Focused interface for input |
| IPresentation | PASS | Focused interface for display |
| BufferContext | PASS | Composed of focused components |

### Dependency Inversion Principle (DIP)

| Component | Compliance | Notes |
|-----------|------------|-------|
| CLIMain | PASS | Depends on abstractions (strategies, factories) |
| SimulationLoop | PASS | Depends on IAudioStrategy, IInputProvider, IPresentation |
| AudioPlayer | PASS | Depends on IAudioStrategy and IAudioHardwareProvider |
| ThreadedStrategy | PASS | Depends on BufferContext abstraction |
| SyncPullStrategy | PASS | Depends on BufferContext abstraction |

### Remaining DIP Concerns
- AudioPlayer creates `CoreAudioHardwareProvider` directly instead of using `AudioHardwareProviderFactory`
- `audio::IAudioSource` exists in `src/audio/common/` but is unused (separate `IAudioSource` in `src/AudioSource.h` is used)

---

## Known Gaps

1. **AudioPlayer creates CoreAudioHardwareProvider directly** -- should use `AudioHardwareProviderFactory` for full DIP compliance
2. **Legacy `static audioUnitCallback`** still declared in AudioPlayer.h but not used (callback registered via hardware provider)
3. **Duplicate IAudioSource** -- `audio::IAudioSource` in `src/audio/common/` is not used; `IAudioSource` in `src/AudioSource.h` is the one in use
4. **ITelemetryWriter has no implementation** -- forward-declared and always nullptr in SimulationConfig

---

**Document End**
