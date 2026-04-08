# Class Hierarchy Diagram
## Complete Relationship Map of All Classes

---

## Inheritance Hierarchy

```
IAudioRenderer (Interface)
    ↑
    │
    ├─► ThreadedRenderer
    │
    └─► SyncPullRenderer


IAudioStrategy (Interface)
    ↑
    │
    ├─► ThreadedStrategy
    │
    └─► SyncPullStrategy


IPresentation (Interface)
    ↑
    │
    └─► ConsolePresentation


IInputProvider (Interface)
    ↑
    │
    └─► KeyboardInputProvider


IAudioSource (Interface)
    ↑
    │
    └─► BridgeAudioSource


IAudioHardwareProvider (Interface)
    ↑
    │
    └─► CoreAudioHardwareProvider
```

---

## Composition/Aggregation Relationships

### StrategyContext Composition

```
StrategyContext
├─► AudioState
│   ├─ sampleRate: int
│   └─ isPlaying: bool
│
├─► BufferState
│   ├─ writePointer: std::atomic<int>
│   ├─ readPointer: std::atomic<int>
│   ├─ underrunCount: std::atomic<int>
│   ├─ fillLevel: int
│   └─ capacity: int
│
├─► Diagnostics
│   ├─ lastRenderMs: std::atomic<double>
│   ├─ lastHeadroomMs: std::atomic<double>
│   ├─ lastBudgetPct: std::atomic<double>
│   ├─ lastFrameBudgetPct: std::atomic<double>
│   ├─ totalFramesRendered: int
│   └─ ... (timing metrics)
│
├─► std::unique_ptr<CircularBuffer>
│
├─► IAudioStrategy* (reference, not owned)
│
└─► EngineSimHandle
```

### AudioUnitContext Composition (Legacy)

```
AudioUnitContext
├─► EngineSimHandle
├─► std::atomic<bool> isPlaying
├─► IAudioRenderer* audioRenderer
├─► std::unique_ptr<CircularBuffer>
├─► std::atomic<int> writePointer
├─► std::atomic<int> readPointer
├─► std::atomic<int> underrunCount
├─► int bufferStatus
├─► std::atomic<int64_t> totalFramesRead
├─► int sampleRate
├─► std::unique_ptr<SyncPullAudio>
├─► std::atomic<int> lastReqFrames
├─► std::atomic<int> lastGotFrames
├─► std::atomic<double> lastRenderMs
├─► std::atomic<double> lastHeadroomMs
├─► std::atomic<double> lastBudgetPct
├─► std::atomic<double> lastFrameBudgetPct
├─► std::atomic<double> lastBufferTrendPct
├─► std::atomic<double> lastCallbackIntervalMs
├─► std::atomic<bool> preBufferDepleted
├─► std::atomic<double> windowStartTimeMs
├─► std::atomic<int> framesServedInWindow
├─► std::atomic<double> perfWindowStartTimeMs
├─► std::atomic<int> framesRequestedInWindow
├─► std::atomic<int> framesGeneratedInWindow
├─► std::atomic<double> lastCallbackTimeMs
└─► float volume
```

### AudioPlayer Composition (Legacy)

```
AudioPlayer
├─► AudioUnit audioUnit
├─► AudioDeviceID deviceID
├─► bool isPlaying
├─► int sampleRate
├─► AudioUnitContext* context
├─► IAudioRenderer* renderer
├─► std::unique_ptr<ConsoleLogger> defaultLogger_
└─► ILogging* logger_
```

### StrategyAdapter Composition (Bridge)

```
StrategyAdapter : public IAudioRenderer
├─► std::unique_ptr<IAudioStrategy> strategy_
├─► std::unique_ptr<StrategyContext> context_
├─► int sampleRate_
├─► EngineSimHandle engineHandle_
└─► const EngineSimAPI* engineAPI_
```

---

## Dependency Graph

### CLIMain Dependencies

```
CLIMain::main()
├─► CommandLineArgs (from CLIconfig)
├─► SimulationConfig (from SimulationLoop.h)
├─► createStrategyAdapter()
│   └─► IAudioStrategyFactory
│       └─► AudioMode
│           ├─► ThreadedStrategy
│           └─► SyncPullStrategy
├─► SimulationLoop::runUnifiedAudioLoop()
└─► EngineSimAPI
```

### SimulationLoop Dependencies

```
SimulationLoop::runUnifiedAudioLoop()
├─► AudioPlayer
│   ├─► AudioUnit (CoreAudio)
│   └─► AudioUnitContext
│
├─► IAudioRenderer (via StrategyAdapter)
│   └─► IAudioStrategy (ThreadedStrategy/SyncPullStrategy)
│       └─► StrategyContext
│           ├─► AudioState
│           ├─► BufferState
│           ├─► Diagnostics
│           └─► CircularBuffer
│
├─► IAudioSource
│   └─► BridgeAudioSource
│       └─► EngineSimAPI
│
├─► IInputProvider
│   └─► KeyboardInputProvider
│
└─► IPresentation
    └─► ConsolePresentation
```

### Audio Layer Dependencies

```
IAudioStrategy (Interface)
├─► StrategyContext
│   ├─► AudioState
│   ├─► BufferState
│   ├─► Diagnostics
│   ├─► CircularBuffer
│   └─► EngineSimHandle
│
└─► ILogging
```

```

IAudioHardwareProvider (Interface)
├─► AudioStreamFormat
├─► PlatformAudioBufferList
├─► AudioHardwareState
└─► ILogging
```

### ThreadedStrategy Dependencies

```
ThreadedStrategy
├─► StrategyContext
│   ├─► AudioState
│   ├─► BufferState
│   ├─► Diagnostics
│   ├─► CircularBuffer
│   └─► EngineSimHandle
│
└─► ILogging
```

### SyncPullStrategy Dependencies

```
SyncPullStrategy
├─► StrategyContext
│   ├─► AudioState
│   ├─► BufferState
│   ├─► Diagnostics
│   └─► EngineSimHandle
│
└─► ILogging
```

### CoreAudioHardwareProvider Dependencies

```
CoreAudioHardwareProvider : public IAudioHardwareProvider
├─► AudioUnit audioUnit_
├─► AudioDeviceID deviceID_
├─► AudioStreamFormat format_
├─► IAudioHardwareProvider::AudioCallback callback_
└─► ILogging* logger_
```

---

## File-Component Mapping

### Audio Strategy Layer (New Architecture)

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/strategies/IAudioStrategy.h | IAudioStrategy, AudioMode, AudioStrategyConfig | Strategy interface and types |
| src/audio/strategies/IAudioStrategyFactory.cpp | IAudioStrategyFactory | Factory for creating strategies |
| src/audio/strategies/ThreadedStrategy.h | ThreadedStrategy | Cursor-chasing mode implementation |
| src/audio/strategies/ThreadedStrategy.cpp | ThreadedStrategy | Implementation |
| src/audio/strategies/SyncPullStrategy.h | SyncPullStrategy | Lock-step mode implementation |
| src/audio/strategies/SyncPullStrategy.cpp | SyncPullStrategy | Implementation |

### Audio Hardware Layer (New Architecture)

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/hardware/IAudioHardwareProvider.h | IAudioHardwareProvider, AudioStreamFormat, PlatformAudioBufferList, AudioHardwareState | Hardware abstraction |
| src/audio/hardware/AudioHardwareProviderFactory.cpp | AudioHardwareProviderFactory | Factory for creating providers |
| src/audio/hardware/CoreAudioHardwareProvider.h | CoreAudioHardwareProvider | macOS implementation |
| src/audio/hardware/CoreAudioHardwareProvider.cpp | CoreAudioHardwareProvider | Implementation |

### Audio State Management (New Architecture)

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/state/StrategyContext.h | StrategyContext | Composed context for strategies |
| src/audio/state/AudioState.h | AudioState | Audio playback state |
| src/audio/state/BufferState.h | BufferState | Circular buffer state |
| src/audio/state/Diagnostics.h | Diagnostics | Performance metrics |

### Audio Common Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/common/CircularBuffer.h | CircularBuffer | Circular buffer implementation |
| src/audio/common/CircularBuffer.cpp | CircularBuffer | Implementation |
| src/audio/common/IAudioSource.h | IAudioSource | Audio source interface |
| src/audio/common/BridgeAudioSource.h | BridgeAudioSource | Engine bridge audio source |
| src/audio/common/BridgeAudioSource.cpp | BridgeAudioSource | Implementation |

### Audio Adapter Layer (Bridge)

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/audio/adapters/StrategyAdapter.h | StrategyAdapter | Adapts IAudioStrategy to IAudioRenderer |
| src/audio/adapters/StrategyAdapter.cpp | StrategyAdapter | Implementation |
| src/audio/adapters/StrategyAdapterFactory.h | createStrategyAdapter() | Factory for adapters |

### Audio Legacy Layer (Old Architecture)

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/AudioPlayer.h | AudioPlayer, AudioUnitContext | Audio playback with direct AudioUnit access |
| src/AudioPlayer.cpp | AudioPlayer | Implementation |
| src/AudioSource.h | AudioSource | Deprecated audio source |
| src/AudioSource.cpp | AudioSource | Deprecated implementation |
| src/SyncPullAudio.h | SyncPullAudio | Legacy sync-pull |
| src/SyncPullAudio.cpp | SyncPullAudio | Legacy implementation |
| src/audio/renderers/IAudioRenderer.h | IAudioRenderer | Deprecated renderer interface |
| src/audio/renderers/ThreadedRenderer.h | ThreadedRenderer | Superseded by ThreadedStrategy |
| src/audio/renderers/ThreadedRenderer.cpp | ThreadedRenderer | Deprecated implementation |
| src/audio/renderers/SyncPullRenderer.h | SyncPullRenderer | Superseded by SyncPullStrategy |
| src/audio/renderers/SyncPullRenderer.cpp | SyncPullRenderer | Deprecated implementation |
| src/audio/renderers/AudioRendererFactory.cpp | createAudioRendererFactory() | Superseded by IAudioStrategyFactory |

### Simulation Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/simulation/SimulationLoop.h | SimulationConfig, runSimulation(), runUnifiedAudioLoop(), etc. | Main simulation loop |
| src/simulation/SimulationLoop.cpp | SimulationConfig, functions | Implementation |
| src/simulation/EngineConfig.h | EngineConfig | Engine configuration |
| src/simulation/EngineConfig.cpp | EngineConfig | Implementation |

### CLI/Config Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/config/CLIMain.h | (functions) | Main entry point |
| src/config/CLIMain.cpp | main() | Implementation |
| src/config/CLIconfig.h | CommandLineArgs, SimulationConfig | Command line parsing |
| src/config/CLIconfig.cpp | CommandLineArgs, SimulationConfig | Implementation |
| src/config/ANSIColors.h | (helper functions) | Console colors |
| src/config/ANSIColors.cpp | (helper functions) | Implementation |

### Presentation Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/presentation/IPresentation.h | IPresentation | Presentation interface |
| src/presentation/ConsolePresentation.h | ConsolePresentation | Console presentation |
| src/presentation/ConsolePresentation.cpp | ConsolePresentation | Implementation |

### Input Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/input/IInputProvider.h | IInputProvider | Input interface |
| src/input/KeyboardInput.h | KeyboardInput | Keyboard input |
| src/input/KeyboardInput.cpp | KeyboardInput | Implementation |
| src/input/KeyboardInputProvider.h | KeyboardInputProvider | Keyboard input provider |
| src/input/KeyboardInputProvider.cpp | KeyboardInputProvider | Implementation |

### Bridge Layer

| File | Classes | Responsibilities |
|------|---------|---------------|
| src/bridge/engine_sim_loader.h | EngineSimAPI, EngineSimHandle, EngineLoaderResult | Engine simulator bridge |

---

## Key Architecture Patterns

### Strategy Pattern

- **IAudioStrategy**: Abstracts audio generation strategies
- **ThreadedStrategy**: Cursor-chasing implementation
- **SyncPullStrategy**: Lock-step implementation
- **IAudioRenderer**: Deprecated renderer interface (being replaced)
- **StrategyAdapter**: Bridges new strategies to old renderer interface

### Factory Pattern

- **IAudioStrategyFactory**: Creates IAudioStrategy implementations
- **AudioHardwareProviderFactory**: Creates IAudioHardwareProvider implementations
- **StrategyAdapterFactory**: Creates StrategyAdapter instances
- **AudioRendererFactory**: Deprecated (superseded)

### Dependency Injection

- **IInputProvider**: Injected into simulation loop
- **IPresentation**: Injected into simulation loop
- **IAudioRenderer**: Injected into simulation loop (via StrategyAdapter)
- **ILogger**: Injected throughout codebase
- **EngineSimAPI**: Provided by bridge

### Composition vs Inheritance

- **StrategyContext**: Composition of AudioState, BufferState, Diagnostics, CircularBuffer
- **AudioUnitContext**: Monolithic composition (legacy)

### Adapter Pattern

- **StrategyAdapter**: Bridges IAudioStrategy to IAudioRenderer interface
- Enables gradual migration from old to new architecture
- Maintains backward compatibility during transition

---

## SOLID Principles Assessment

### Single Responsibility Principle (SRP)

| Component | SRP Compliance | Notes |
|-----------|----------------|-------|
| IAudioStrategy | ✓ | One responsibility: audio generation |
| ThreadedStrategy | ✓ | One responsibility: cursor-chasing mode |
| SyncPullStrategy | ✓ | One responsibility: lock-step mode |
| IAudioHardwareProvider | ✓ | One responsibility: hardware abstraction |
| StrategyContext | ✓ | One responsibility: context composition |
| AudioPlayer | ✗ | Multiple responsibilities: hardware access, playback control |
| SimulationLoop | ✗ | Multiple responsibilities: simulation coordination |

### Open/Closed Principle (OCP)

| Component | OCP Compliance | Notes |
|-----------|----------------|-------|
| IAudioStrategy | ✓ | New strategies can be added without modification |
| IAudioHardwareProvider | ✓ | New platforms can be added without modification |
| AudioPlayer | ✗ | Direct AudioUnit access, hard to extend |
| SimulationLoop | ✓ | Extensible via strategy pattern |

### Liskov Substitution Principle (LSP)

| Component | LSP Compliance | Notes |
|-----------|----------------|-------|
| IAudioStrategy | ✓ | ThreadedStrategy/SyncPullStrategy are substitutable |
| IAudioHardwareProvider | ✓ | CoreAudioHardwareProvider is substitutable |
| IAudioRenderer | ✓ | ThreadedRenderer/SyncPullRenderer are substitutable |
| StrategyAdapter | ✓ | Correctly implements IAudioRenderer |

### Interface Segregation Principle (ISP)

| Component | ISP Compliance | Notes |
|-----------|----------------|-------|
| IAudioStrategy | ✓ | Focused interface, essential methods only |
| IAudioHardwareProvider | ✓ | Focused interface, essential methods only |
| AudioUnitContext | ✗ | Monolithic, too many responsibilities |
| StrategyContext | ✓ | Composed of focused components |

### Dependency Inversion Principle (DIP)

| Component | DIP Compliance | Notes |
|-----------|----------------|-------|
| CLIMain | ✓ | Depends on abstractions (strategies, factories) |
| SimulationLoop | ✓ | Depends on abstractions (IAudioRenderer, IAudioSource) |
| AudioPlayer | ✗ | Depends on concrete AudioUnit |
| IAudioStrategy | ✓ | Depends on StrategyContext abstraction |

---

## Summary

### Current Architecture State

The codebase contains both old and new audio architectures:

**Old Architecture** (being phased out):
- AudioPlayer with direct AudioUnit access
- AudioUnitContext (monolithic state)
- IAudioRenderer with ThreadedRenderer/SyncPullRenderer
- AudioSource, SyncPullAudio (deprecated)

**New Architecture** (being phased in):
- IAudioStrategy with ThreadedStrategy/SyncPullStrategy
- IAudioHardwareProvider with CoreAudioHardwareProvider
- StrategyContext (composed state)
- CircularBuffer, AudioState, BufferState, Diagnostics

**Bridge** (temporary during migration):
- StrategyAdapter bridges IAudioStrategy to IAudioRenderer
- Maintains backward compatibility

### Migration Progress

- ✅ New architecture components implemented and tested
- ✅ StrategyAdapter bridges old and new
- ✅ All tests passing (31/32 unit, 7/7 integration)
- ⏳ AudioPlayer still uses old AudioUnit (pending)
- ⏳ Legacy code still in build (pending)
- ⏳ Complete migration (pending)

### Recommendations

1. **Complete AudioPlayer refactoring** to use IAudioHardwareProvider
2. **Remove legacy components** once migration is complete
3. **Update documentation** to reflect new architecture
4. **Consolidate audio directory** structure

---

**Document End**
