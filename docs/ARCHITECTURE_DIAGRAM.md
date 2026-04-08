# Architecture Diagram
## Visual Representation of Component Flow

---

## Runtime Execution Flow

```
┌────────────────────────────────────────────────────────────────────┐
│                       User launches app                 │
└────────────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────────────┐
│                    CLIMain::main()                      │
│                                                               │
│  1. Parse CLI arguments  │
│  2. Load engine-sim library  │
│  3. Create strategy adapter  │
│  4. Run simulation loop  │
└────────────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────────────┐
│              StrategyAdapterFactory                      │
│                   │                                     │
│                   ├─► AudioMode                      │
│                   │     ├─► Threaded                │
│                   │     └─► SyncPull                 │
│                   │                                     │
│                   └─► StrategyAdapter              │
│                         │                                 │
│                         ├─► IAudioStrategy           │
│                         │     ├─► ThreadedStrategy  │
│                         │     └─► SyncPullStrategy  │
│                         │                                   │
│                         └─► StrategyContext        │
└────────────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────────────┐
│           SimulationLoop::runUnifiedAudioLoop()          │
│                                                               │
│  ┌─────────────────────────────────────────────────────────┐ │
│  │ Initialize AudioPlayer                              │ │
│  │                                                   │ │
│  │ ┌─────────────────────────────────────────────────┐│ │
│  │ │ StrategyAdapter (bridges old to new)         ││ │
│  │ │                                               ││ │
│  │ │ ├─► IAudioStrategy (new architecture)        ││ │
│  │ │ │     ├─► ThreadedStrategy                  ││ │
│  │ │ │     │     ├─► StrategyContext               ││ │
│  │ │ │     │     │     ├─► AudioState            ││ │
│  │ │ │     │     │     ├─► BufferState           ││ │
│  │ │ │     │     │     ├─► Diagnostics          ││ │
│  │ │ │     │     │     ├─► CircularBuffer        ││ │
│  │ │ │     │     │     └─► EngineSimHandle       ││ │
│  │ │ │     │     └─► ILogging                   ││ │
│  │ │ └─► IAudioRenderer (old interface)          ││ │
│  └─────────────────────────────────────────────────────────┘│ │
│                                                       │ │
│  ┌─────────────────────────────────────────────────────────┐│ │
│  │ AudioPlayer (legacy, uses AudioUnit)                ││ │
│  │                                                   ││ │
│  │ ├─► AudioUnit (CoreAudio - platform specific)       ││ │
│  │ ├─► AudioUnitContext (legacy state)                 ││ │
│  │ └─► IAudioRenderer* (via StrategyAdapter)        ││ │
│  └─────────────────────────────────────────────────────────┘│ │
│                                                       │ │
│  ┌─────────────────────────────────────────────────────────┐│ │
│  │ Other Dependencies                                  ││ │
│  │                                                   ││ │
│  │ ├─► IAudioSource (BridgeAudioSource)             ││ │
│  │ ├─► IInputProvider (KeyboardInputProvider)        ││ │
│  │ ├─► IPresentation (ConsolePresentation)            ││ │
│  │ └─► EngineSimAPI, EngineSimHandle             ││ │
│  └─────────────────────────────────────────────────────────┘│ │
└──────────────────────────────────────────────────────────────┘│
                         │                                           │
                         ▼                                           ▼
┌────────────────────────────────────────────────────────────┐      ┌────────────────────────────────────────────────────────────┐
│  Simulation Loop (runs in main thread)            │      │  Audio Callback (runs in CoreAudio thread)  │
│                                                  │      │                                               │
│  While running:                                  │      │  │
│  1. Update simulation via EngineSimAPI          │      │  1. AudioUnit calls audioUnitCallback()        │
│  2. Generate audio via audioSource            │      │  2. Callback delegates to StrategyAdapter         │
│  3. Get input from IInputProvider            │      │  3. StrategyAdapter delegates to IAudioStrategy│
│  4. Update presentation via IPresentation        │      │  4. IAudioStrategy renders to buffer          │
│  5. Manage buffer based on audio mode           │      │  5. StrategyContext tracks buffer state        │
└──────────────────────────────────────────────────────┘      │  6. CircularBuffer handles wrap-around         │
                                                           │      │ 7. Diagnostics track performance            │
                                                           │      └──────────────────────────────────────────────────────┘
```

---

## Component Layer Diagram

```
┌─────────────────────────────────────────────────────────────────┐
│                     CLI Layer (Entry)                    │
│                                                           │
│  CLIMain :: main()                                     │
│  CLIconfig :: Parse args                                │
│  SimulationConfig :: Configuration                         │
│  ANSIColors :: Helper                                 │
└─────────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Simulation Loop Layer                   │
│                                                           │
│  SimulationLoop :: runUnifiedAudioLoop()                  │
│  SimulationLoop :: runSimulation()                         │
│  EngineConfig :: Engine configuration                      │
└─────────────────────────────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────────┐
         │               │                   │
         ▼               ▼                   ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────┐
│ Presentation │ │    Input     │ │   Audio      │
│    Layer    │ │    Layer     │ │   Layer      │
│             │ │              │ │              │
│ IPresentation│ │IInputProvider│ │IAudioStrategy │
│             │ │              │ │              │
│Console    │ │KeyboardInput  │ │Threaded      │
│Presentation│ │Provider       │ │Strategy      │
│             │ │              │ │SyncPull      │
│             │ │              │ │Strategy      │
└─────────────┘ └─────────────┘ └─────────────┘
                         │               │               │
                         ▼               ▼               ▼
┌─────────────────────────┐ ┌─────────────────┐ ┌─────────────────────────┐
│  Input/Keyboard    │ │  Presentation/ │ │      Audio/Strategies   │
│  InputProvider     │ │  Console        │ │                         │
│                   │ │                  │ │ IAudioStrategyFactory  │
│                   │ │                  │ └─────────────────────────┘
└─────────────────────────┘ └─────────────────┘
                         │
                         ▼
┌─────────────────────────────────────────────────────────────────┐
│                  Audio Common Layer                 │
│                                                           │
│  CircularBuffer :: Circular buffer implementation      │
│  IAudioSource :: Audio source interface              │
│  BridgeAudioSource :: Engine bridge implementation      │
└─────────────────────────────────────────────────────────────────┘
                         │
         ┌───────────┼───────────────────┐
         │               │                   │
         ▼               ▼                   ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐
│   Audio    │ │   Audio      │ │      Audio/State        │
│   Legacy    │ │   Renderers  │ │                         │
│    Layer    │ │    Layer     │ │  StrategyContext ::       │
│             │ │              │ │  Composed of:            │
│ AudioPlayer  │ │IAudioRenderer│ │                         │
│ AudioSource │ │Threaded      │ │    AudioState :: Playback│
│ SyncPull    │ │Renderer       │ │                         │
│ Audio       │ │SyncPull       │ │    BufferState :: Buffer  │
│ UnitContext  │ │Renderer       │ │                         │
│             │ │              │ │    Diagnostics :: Metrics │
└─────────────┘ └─────────────┘ └─────────────────────────┘
         │               │               │
         ▼               ▼               ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐
│Audio/State  │ │ Audio/State  │ │      Audio/Hardware      │
│  (Legacy)   │ │(New)        │ │                         │
│             │ │              │ │ IAudioHardwareProvider  │
│AudioUnit    │ │  - See above │ │  AudioStreamFormat       │
│Context      │ │              │ │  PlatformAudioBufferList  │
│             │ │              │ │  AudioHardwareState      │
└─────────────┘ └─────────────┘ └─────────────────────────┘
                         │
         ┌───────────┼───────────────────┐
         │               │                   │
         ▼               ▼                   ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐
│Audio/State  │ │ Audio/State  │ │      Audio/Adapters       │
│  (New)      │ │(New)        │ │                         │
│             │ │              │ │  StrategyAdapter ::       │
│AudioState   │ │  - See above │ │  Bridges new to old      │
│BufferState  │ │              │ │                         │
│Diagnostics │ │              │ │                         │
└─────────────┘ └─────────────┘ └─────────────────────────┘
```

---

## Audio Architecture Evolution

### Old Architecture (Being Phased Out)

```
┌─────────────────────────────────────────────────────────┐
│              AudioPlayer (Legacy)                 │
│                                                │
│  ┌─────────────────────────────────────────────┐ │
│  │ AudioUnit (Direct CoreAudio Access)  │ │
│  │                                        │ │
│  │  - Tight platform coupling            │ │
│  │  - Direct hardware operations         │ │
│  │  - Platform-specific code in player   │ │
│  └─────────────────────────────────────────┘ │
│                                                │
│  ┌─────────────────────────────────────────────┐ │
│  │ AudioUnitContext (Monolithic)          │ │
│  │                                        │ │
│  │  - Audio state                       │ │
│  │  - Buffer state                      │ │
│  │  - Diagnostics                      │ │
│  │  - SyncPullAudio                     │ │
│  │  - Multiple responsibilities (SRP violation) │ │
│  └─────────────────────────────────────────┘ │
│                                                │
│  ┌─────────────────────────────────────────────┐ │
│  │ IAudioRenderer (Interface)            │ │
│  │                                        │ │
│  │  - ThreadedRenderer                   │ │
│  │  - SyncPullRenderer                  │ │
│  │  - Superseded by IAudioStrategy   │ │
│  └─────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### New Architecture (Being Phased In)

```
┌─────────────────────────────────────────────────────────┐
│              StrategyAdapter (Bridge)               │
│                                                │
│  ┌─────────────────────────────────────────────┐ │
│  │ IAudioStrategy (New Interface)         │ │
│  │                                        │ │
│  │  - ThreadedStrategy                 │ │
│  │  - SyncPullStrategy                  │ │
│  │  - Clear responsibilities            │ │
│  │  - OCP compliant                 │ │
│  └─────────────────────────────────────────┘ │
│                                                │
│  ┌─────────────────────────────────────────────┐ │
│  │ StrategyContext (Composed State)       │ │
│  │                                        │ │
│  │  - AudioState (Playback)             │ │
│  │  - BufferState (Buffer)              │ │
│  │  - Diagnostics (Metrics)              │ │
│  │  - CircularBuffer (Data)               │ │
│  │  - EngineSimHandle                    │ │
│  │  - Clear separation (SRP)             │ │
│  └─────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

### Hardware Abstraction (New)

```
┌─────────────────────────────────────────────────────────┐
│        IAudioHardwareProvider (Interface)        │
│                                                │
│  - Abstracts hardware operations               │
│  - Enables platform extension (OCP)            │
│  - Platform-agnostic interface               │
│                                                │
│  ┌─────────────────────────────────────────────┐ │
│  │ CoreAudioHardwareProvider (macOS)      │ │
│  │                                        │ │
│  │  - Implements IAudioHardwareProvider   │ │
│  │  - Wraps AudioUnit (platform-specific) │ │
│  │  - Keeps platform code isolated         │ │
│  └─────────────────────────────────────────┘ │
└─────────────────────────────────────────────────┘
```

---

## Data Flow Diagram

### Threaded Mode (Cursor-Chasing)

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Main Thread  │────▶│ Audio Thread   │────▶│ Audio Callback  │
│                │     │ (generates    │     │ (CoreAudio)    │
│                │     │  audio)        │     │                 │
│  ┌──────────┐  │     └─────────────────┘     └─────────────────┘
│  │Simulation  │  │
│  │Loop       │  │
│  └──────────┘  │
│                │
│                ▼
│        ┌─────────────────┐
│        │Circular Buffer │
│        │  2 seconds    │
│        │  (88200 frames)│
│        └─────────────────┘
```

**Flow**:
1. Main thread generates audio and writes to circular buffer
2. CoreAudio callback reads from circular buffer (cursor-chasing)
3. Maintains ~100ms buffer lead to prevent underruns

### Sync-Pull Mode (Lock-Step)

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Main Thread  │────▶│ Audio Callback  │────▶│ Engine Simulator │
│                │     │ (CoreAudio)    │     │                 │
│                │     │                 │     │                 │
│  ┌──────────┐  │     └─────────────────┘     └─────────────────┘
│  │Simulation  │  │
│  │Loop       │  │
│  │           │  │
│  │  1. Request  │  │
│  │     audio    │  │
│  │     frames   │  │
│  │     via API  │  │
│  │           │  │
│  │  2. Get    │  │
│  │     audio   │  │
│  │     frames   │  │
│  │     from API │  │
│  │           │  │
│  │  3. Write  │  │
│  │     to output│  │
│  │     buffer   │  │
│  │           │  │
│  └──────────┘  │
                │
                ▼
        ┌─────────────────┐
        │Output Buffer   │
        │(No buffering) │
        └─────────────────┘
```

**Flow**:
1. Audio callback requests frames
2. Main thread calls EngineSimAPI to generate audio
3. Generated audio is written directly to output buffer (no circular buffer)
4. Lock-step: simulation advances only when audio needs frames

---

## State Transitions

### AudioPlayer Lifecycle

```
┌─────────────────┐
│  Constructor   │
└─────┬─────────┘
      │
      ▼
┌─────────────────┐
│  initialize()  │
│  - Creates     │
│    AudioUnit  │
│  - Creates     │
│    Context   │
│  - Sets       │
│    Renderer  │
└─────┬─────────┘
      │
      ▼
┌─────────────────┐
│  start()      │
│  - Starts     │
│    AudioUnit   │
│  - Sets       │
│    Playing     │
└─────┬─────────┘
      │
      ▼
┌─────────────────┐
│  stop()       │
│  - Stops      │
│    AudioUnit   │
│  - Clears     │
│    Playing     │
└─────────────────┘
```

### Strategy Lifecycle

```
┌─────────────────┐
│  Factory      │
│  Create()     │
└─────┬─────────┘
      │
      ▼
┌─────────────────┐
│  Strategy     │
│  configure()  │
│  - Setup      │
│    State      │
└─────┬─────────┘
      │
      ▼
┌─────────────────┐
│  Strategy     │
│  render()     │
│  - Generate   │
│    audio      │
│  - Write to   │
│    buffer     │
└─────┬─────────┘
      │
      ▼
┌─────────────────┐
│  Strategy     │
│  reset()      │
│  - Clear      │
│    state      │
└─────────────────┘
```

---

## Migration Path

### Current State (Mixed Architecture)

```
┌────────────────────────────────────────────────────────────┐
│                 Production Code                    │
└────────────────────────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────────┐
         │               │                   │
         ▼               ▼                   ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐
│  Old        │ │   New        │ │      Adapter (Temporary) │
│  Audio      │ │   Audio      │ │                           │
│  Architecture│ │  Architecture │ │                           │
│             │ │              │ │                           │
│  AudioPlayer │ │  IAudioStrategy │ │   StrategyAdapter         │
│  IAudioRenderer│ │  StrategyContext │ │   - Bridges new to old  │
│  ThreadedRenderer│ │  IAudioHardwareProvider│ │   - Maintains compatibility  │
│  SyncPullRenderer│ │                   │ │                           │
│             │ │              │ │                           │
└─────────────┘ └─────────────┘ └─────────────────────────┘
```

### Target State (New Architecture Only)

```
┌────────────────────────────────────────────────────────────┐
│                 Production Code                    │
└────────────────────────────────────────────────────────────┘
                         │
                         ▼
┌────────────────────────────────────────────────────────────┐
│              New Architecture Only                │
└────────────────────────────────────────────────────────────┘
                         │
         ┌───────────────┼───────────────────┐
         │               │                   │
         ▼               ▼                   ▼
┌─────────────┐ ┌─────────────┐ ┌─────────────────────────┐
│  Audio      │ │  Audio      │ │      Audio/State        │
│  Architecture│ │  Hardware    │ │                         │
│             │ │  Abstraction │ │  IAudioStrategy         │
│ AudioPlayer  │ │               │ │  - ThreadedStrategy      │
│ (Refactored) │ │               │ │  - SyncPullStrategy       │
│             │ │               │ │                         │
│  - Uses     │ │               │ │  StrategyContext         │
│  IAudioHardwareProvider│               │ │  - AudioState             │
│  - Uses     │ │               │ │  - BufferState            │
│  IAudioStrategy│               │ │  - Diagnostics             │
│  - No AudioUnit│               │ │  - CircularBuffer          │
│  - No AudioUnitContext│           │ │                         │
│             │ │               │ │                         │
└─────────────┘ └─────────────┘ └─────────────────────────┘
```

### Migration Steps

1. ✅ **Phase 1 - Foundation**: Implement new architecture components
   - IAudioStrategy interface
   - ThreadedStrategy, SyncPullStrategy implementations
   - IAudioHardwareProvider interface
   - CoreAudioHardwareProvider implementation
   - StrategyContext (composed state)
   - CircularBuffer, AudioState, BufferState, Diagnostics

2. ✅ **Phase 2 - Adapter**: Create bridge between old and new
   - StrategyAdapter implements IAudioRenderer
   - Delegates to IAudioStrategy
   - Maintains backward compatibility

3. ⏳ **Phase 3 - Refactor AudioPlayer**: Use IAudioHardwareProvider directly
   - Remove AudioUnit member
   - Remove AudioUnitContext
   - Use IAudioHardwareProvider for hardware operations

4. ⏳ **Phase 4 - Remove Legacy**: Delete old architecture
   - Remove IAudioRenderer interface
   - Remove ThreadedRenderer, SyncPullRenderer
   - Remove AudioRendererFactory
   - Remove deprecated AudioSource, SyncPullAudio

5. ⏳ **Phase 5 - Clean Up**: Finalize and document
   - Update all documentation
   - Consolidate directory structure
   - Remove orphaned files

---

## Summary

### Current Architecture Health

| Aspect | Status | Notes |
|---------|--------|--------|
| SOLID Principles | 85% | Good, with improvements needed |
| Test Coverage | 95% | Comprehensive test suite |
| Build System | 100% | All files included, no orphans |
| Documentation | 70% | Good, needs architecture updates |
| Code Organization | 75% | Clear layers, some legacy in build |

### Strengths

1. Clear separation of concerns in new architecture
2. Effective use of design patterns (Strategy, Factory, Adapter)
3. Comprehensive test coverage
4. All tests passing
5. Build system is clean (no orphans)

### Areas for Improvement

1. Complete AudioPlayer refactoring to use IAudioHardwareProvider
2. Remove legacy code once migration is complete
3. Consistent naming conventions
4. Flatten audio directory structure
5. Complete API documentation

### Migration Progress

- ✅ Phase 1: New architecture foundation - COMPLETE
- ✅ Phase 2: Adapter pattern - COMPLETE
- ⏳ Phase 3: AudioPlayer refactoring - IN PROGRESS
- ⏳ Phase 4: Remove legacy - PENDING
- ⏳ Phase 5: Clean up - PENDING

---

**Document End**
