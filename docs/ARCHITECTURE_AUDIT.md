# CLI Architecture Audit

**Date:** 2026-04-02
**Purpose:** Comprehensive audit of CLI/src classes for SOLID compliance and architecture quality
**Method:** Systematic analysis of 42 source files (~4,508 LOC)
**Status:** INITIAL FINDINGS - Not yet integrated into codebase

---

## Executive Summary

**Overall Assessment:** ⚠️ ARCHITECTURE IN TRANSITION

**Key Findings:**
- ✅ **Good:** Modular folder structure established
- ✅ **Good:** Core interfaces (IInputProvider, IPresentation, ITelemetryWriter) follow SOLID
- ✅ **Good:** Audio strategy pattern partially implemented
- ⚠️ **Critical Issue:** AudioUnitContext is massive SRP violation (76 lines, 20+ responsibilities)
- ⚠️ **Critical Issue:** IAudioMode + IAudioRenderer coupling (WET state - Write Everything Twice)
- ⚠️ **Issue:** AudioPlayer has direct CoreAudio coupling (not using IAudioHardwareProvider)
- ⚠️ **Minor Issue:** SimulationLoop has borderline SRP compliance

**Code Quality:**
- Total Files: 42
- Total LOC: ~4,508
- Build Status: ✅ GREEN
- Test Status: 25/25 unit tests passing (1 failing integration test)

---

## SOLID Compliance Analysis

### ✅ SRP (Single Responsibility Principle)

| Class | Status | Evidence |
|-------|--------|----------|
| `IInputProvider` | ✅ PASS | Single responsibility: user input abstraction |
| `KeyboardInputProvider` | ✅ PASS | Implements IInputProvider cleanly |
| `IPresentation` | ✅ PASS | Single responsibility: display abstraction |
| `ConsolePresentation` | ✅ PASS | Implements IPresentation cleanly |
| `ITelemetryWriter` | ✅ PASS | Single responsibility: telemetry output |
| `EngineConfig` | ✅ PASS | Wrapper over bridge C API |
| `SimulationConfig` | ✅ PASS | Configuration struct |
| `CircularBuffer` | ✅ PASS | Single responsibility: ring buffer |

| Class | Status | Evidence |
|-------|--------|----------|
| `AudioUnitContext` | ❌ CRITICAL VIOLATION | 76 lines, 20+ atomic vars, mixed concerns (see details below) |
| `IAudioMode` | ⚠️ BORDERLINE | Overlaps with IAudioRenderer (duplicate responsibilities) |
| `IAudioRenderer` | ⚠️ BORDERLINE | Overlaps with IAudioMode (duplicate responsibilities) |
| `AudioPlayer` | ⚠️ BORDERLINE | Mixes orchestration + platform-specific code |
| `SimulationLoop` | ⚠️ BORDERLINE | Mixes orchestration + some audio control logic |

### ✅ OCP (Open-Closed Principle)

| Component | Status | Evidence |
|----------|--------|----------|
| IInputProvider | ✅ PASS | Can add new input sources without modifying existing code |
| IPresentation | ✅ PASS | Can add new presentation types without modifying existing code |
| IAudioRenderer | ✅ PASS | Can add new renderers without modifying existing code |
| AudioModeFactory | ✅ PASS | Factory pattern enables extension |
| IAudioMode | ⚠️ PARTIAL | Coupled to IAudioRenderer makes independent changes difficult |

| Component | Status | Evidence |
|----------|--------|----------|
| AudioPlayer | ⚠️ VIOLATION | Direct CoreAudio coupling, not using IAudioHardwareProvider |

### ✅ LSP (Liskov Substitution Principle)

| Interface | Status | Evidence |
|-----------|--------|----------|
| IInputProvider | ✅ PASS | All implementations honor interface contracts |
| IPresentation | ✅ PASS | All implementations honor interface contracts |
| IAudioRenderer | ✅ PASS | ThreadedRenderer, SyncPullRenderer honor contracts |
| IAudioMode | ✅ PASS | ThreadedAudioMode, SyncPullAudioMode honor contracts |

### ✅ ISP (Interface Segregation Principle)

| Interface | Status | Evidence |
|-----------|--------|----------|
| IInputProvider | ✅ PASS | Focused interface for input abstraction |
| IPresentation | ✅ PASS | Focused interface for display abstraction |
| IAudioRenderer | ✅ PASS | Focused interface for rendering |
| IAudioMode | ⚠️ PARTIAL | Some methods may not be used by all implementations |

### ✅ DIP (Dependency Inversion Principle)

| Component | Status | Evidence |
|----------|--------|----------|
| SimulationLoop | ✅ PASS | Depends on IInputProvider, IPresentation (abstractions) |
| AudioPlayer | ⚠️ PARTIAL | Depends on IAudioRenderer (abstraction) but AudioUnit is concrete |
| IAudioMode | ✅ PASS | Depends on abstractions (EngineSimHandle, IAudioSource) |
| IAudioRenderer | ✅ PASS | Depends on abstractions (EngineSimHandle, IAudioSource) |

---

## Critical Architecture Issues

### 1. AudioUnitContext - Massive SRP Violation

**Current Implementation:**
```cpp
struct AudioUnitContext {
    EngineSimHandle engineHandle;         // Engine simulator
    std::atomic<bool> isPlaying;         // Playback state

    // Strategy pattern: injected rendering mode
    IAudioRenderer* audioRenderer;

    // Cursor-chasing buffer
    std::unique_ptr<CircularBuffer> circularBuffer;
    std::atomic<int> writePointer;
    std::atomic<int> readPointer;
    std::atomic<int> underrunCount;
    int bufferStatus;

    // Cursor-chasing state
    std::atomic<int64_t> totalFramesRead;
    int sampleRate;

    // Sync pull model
    std::unique_ptr<SyncPullAudio> syncPullAudio;

    // Sync-pull timing diagnostics
    std::atomic<int> lastReqFrames;
    std::atomic<int> lastGotFrames;
    std::atomic<double> lastRenderMs;
    std::atomic<double> lastHeadroomMs;
    std::atomic<double> lastBudgetPct;
    std::atomic<double> lastFrameBudgetPct;
    std::atomic<double> lastBufferTrendPct;
    std::atomic<double> lastCallbackIntervalMs;
    std::atomic<bool> preBufferDepleted;

    // 16ms budget tracking
    std::atomic<double> windowStartTimeMs;
    std::atomic<int> framesServedInWindow;

    // Real-time performance tracking
    std::atomic<double> perfWindowStartTimeMs;
    std::atomic<int> framesRequestedInWindow;
    std::atomic<int> framesGeneratedInWindow;
    std::atomic<double> lastCallbackTimeMs;

    // Master volume
    float volume;
};
```

**Problems:**
- **76 lines** for a single struct
- **20+ atomic variables** (excessive state management)
- **Mixes multiple concerns:**
  - Engine state (engineHandle)
  - Playback state (isPlaying, volume, sampleRate)
  - Rendering state (audioRenderer, circularBuffer, syncPullAudio)
  - Diagnostics (underrunCount, lastRenderMs, lastHeadroomMs, etc.)
  - Performance tracking (windowStartTimeMs, perfWindowStartTimeMs, etc.)

**Should Be (3-4 focused structs):**
```cpp
struct EngineState {
    EngineSimHandle handle;
    std::atomic<bool> isRunning;
};

struct BufferState {
    int writePointer;
    int readPointer;
    int capacity;
    int fillLevel;
};

struct Diagnostics {
    int underrunCount;
    double lastRenderMs;
    double lastHeadroomMs;
    double lastBudgetPct;
    double lastFrameBudgetPct;
    double lastBufferTrendPct;
    double lastCallbackIntervalMs;
    double windowStartTimeMs;
    int framesServedInWindow;
    double perfWindowStartTimeMs;
    int framesRequestedInWindow;
    int framesGeneratedInWindow;
};

struct AudioContext {
    EngineState engine;
    BufferState buffer;
    Diagnostics diagnostics;
    IAudioRenderer* renderer;
    int sampleRate;
    float volume;
};
```

**Impact:**
- SRP violation makes code harder to test (too much state to mock)
- SRP violation makes code harder to maintain (too many responsibilities)
- SRP violation makes code harder to understand (mixed concerns)

### 2. IAudioMode + IAudioRenderer - WET Coupling (Write Everything Twice)

**Current State:**
- Two interfaces with overlapping responsibilities
- Both have lifecycle methods (`updateSimulation`, `generateAudio`, etc.)
- Both create `AudioUnitContext`
- Both are tightly coupled (cannot use independently)

**Evidence from code:**
```cpp
// IAudioMode.h
class IAudioMode {
    virtual void updateSimulation(...) = 0;
    virtual void generateAudio(...) = 0;
    virtual bool startAudioThread(...) = 0;
    virtual void prepareBuffer(...) = 0;
    virtual void resetBufferAfterWarmup(...) = 0;
    virtual void startPlayback(...) = 0;
    virtual bool shouldDrainDuringWarmup() const = 0;
    virtual std::unique_ptr<AudioUnitContext> createContext(...) = 0;
};

// IAudioRenderer.h
class IAudioRenderer {
    virtual void updateSimulation(...) = 0;
    virtual void generateAudio(...) = 0;
    virtual bool startAudioThread(...) = 0;
    virtual void prepareBuffer(...) = 0;
    virtual void resetBufferAfterWarmup(...) = 0;
    virtual void startPlayback(...) = 0;
    virtual bool shouldDrainDuringWarmup() const = 0;
    virtual std::unique_ptr<AudioUnitContext> createContext(...) = 0;
    // Plus: render(), AddFrames(), isEnabled(), getName()
};
```

**Problems:**
- **Write Everything Twice:** Same functionality in two interfaces
- **Duplicate Responsibilities:** Both manage lifecycle, both create contexts
- **Tight Coupling:** Cannot swap renderers independently from modes
- **Testing Complexity:** Need to mock both interfaces for full coverage

**Should Be:**
- Single `IAudioStrategy` interface (already designed in build cache)
- Each strategy (ThreadedStrategy, SyncPullStrategy) owns its state
- No coupling between mode and rendering

**Evidence of Fix in Progress:**
- New `IAudioStrategy.h` exists (found in build cache)
- New `ThreadedStrategy.h/cpp` exists (found in build cache)
- New `SyncPullStrategy.h/cpp` exists (found in build cache)
- These need to be moved from build cache to src/audio/strategies/

### 3. AudioPlayer - Platform Coupling (OCP Violation)

**Current Implementation:**
```cpp
class AudioPlayer {
private:
    AudioUnit audioUnit;  // CONCRETE COREAUDIO TYPE
    AudioDeviceID deviceID;
    // ... other members
};
```

**Problems:**
- Direct CoreAudio coupling (`AudioUnit` type)
- Hard to mock for testing (requires CoreAudio headers)
- Violates OCP (cannot add iOS/ESP32 platforms easily)
- Hard to test in isolation (needs actual CoreAudio)

**Should Be:**
```cpp
class AudioPlayer {
private:
    IAudioHardwareProvider* hardwareProvider;  // ABSTRACTION
    // ... other members
};
```

**Evidence of Fix in Progress:**
- New `IAudioHardwareProvider.h` exists (found in build cache)
- New `CoreAudioHardwareProvider.h/cpp` exists (found in build cache)
- These need to be moved to src/audio/hardware/

### 4. SimulationLoop - Borderline SRP Compliance

**Current State:**
```cpp
class SimulationConfig {
    std::unique_ptr<IAudioRenderer> audioMode;  // Direct renderer manipulation
    ILogging* logger;
    telemetry::ITelemetryWriter* telemetryWriter;
    // ... other config
};

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const SimulationConfig& config,
    AudioPlayer* audioPlayer,
    IAudioRenderer& audioMode,  // Direct renderer reference
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter
);
```

**Problems:**
- SimulationConfig manipulates IAudioRenderer directly (should use IAudioStrategy)
- Loop takes IAudioRenderer directly (should use IAudioStrategy)
- Mixes orchestration with audio control logic

**Should Be:**
- Use `IAudioStrategy*` for both config and loop
- Pure orchestration in SimulationLoop
- Let strategies manage their own rendering

---

## Proposed Simplified Architecture

### Goal: Eliminate WET (Write Everything Twice) State

### Phase 1: Strategy Integration (Ready for Implementation)

**New Audio Architecture:**
```
src/audio/
├── strategies/
│   ├── IAudioStrategy.h           # Unified interface (lifecycle + rendering)
│   ├── ThreadedStrategy.h/cpp      # Self-contained: owns buffer, state, rendering
│   └── SyncPullStrategy.h/cpp        # Self-contained: owns rendering logic
├── hardware/
│   ├── IAudioHardwareProvider.h     # Platform abstraction
│   └── CoreAudioHardwareProvider.h/cpp # macOS implementation
├── diagnostics/
│   └── AudioDiagnostics.h           # Unified diagnostics struct
└── utils/
    └── AudioUtils.h/cpp              # DRY helpers
```

**Benefits:**
- ✅ Single `IAudioStrategy` interface (eliminates IAudioMode + IAudioRenderer coupling)
- ✅ Each strategy is self-contained (owns its state)
- ✅ Platform abstraction via `IAudioHardwareProvider`
- ✅ Clean separation: orchestration (SimulationLoop) vs implementation (strategies)
- ✅ Easier to test (mock single interface)
- ✅ Easier to extend (add new strategies)

### Phase 2: State Management Simplification

**Replace AudioUnitContext with 3-4 Focused Structs:**
```cpp
// State (not implementation details)
struct AudioState {
    int sampleRate;
    float volume;
    bool isPlaying;
};

struct BufferState {
    int writePointer;
    int readPointer;
    int underrunCount;
    int fillLevel;
};

struct Diagnostics {
    double lastRenderMs;
    double lastHeadroomMs;
    double lastBudgetPct;
    double lastBufferTrendPct;
    double lastCallbackIntervalMs;
    int framesServedInWindow;
    int framesRequestedInWindow;
    int framesGeneratedInWindow;
};

// Performance tracking
struct PerformanceMetrics {
    double windowStartTimeMs;
    double perfWindowStartTimeMs;
    int framesRequestedInWindow;
    int framesGeneratedInWindow;
    double lastCallbackTimeMs;
};

// Combined context
struct StrategyContext {
    AudioState audioState;
    BufferState bufferState;
    Diagnostics diagnostics;
    PerformanceMetrics performance;
};
```

**Benefits:**
- ✅ Each struct has single responsibility
- ✅ Easier to test (mock individual structs)
- ✅ Easier to maintain (clear responsibilities)
- ✅ Clear separation of concerns

### Phase 3: Integration Layer

**Refactored AudioPlayer:**
```cpp
class AudioPlayer {
public:
    // Constructor with injected strategy and provider
    AudioPlayer(IAudioStrategy* strategy,
                IAudioHardwareProvider* provider,
                ILogging* logger = nullptr);

private:
    IAudioStrategy* strategy_;              // Injected (not owned)
    IAudioHardwareProvider* hardwareProvider_;  // Injected (not owned)
    StrategyContext context_;                // Internal state

    // Delegate all audio operations to strategy
    bool initialize(...) { return strategy_->initialize(...); }
    bool start() { return hardwareProvider_->start(); }
    void stop() { hardwareProvider_->stop(); }
    void setVolume(float v) { hardwareProvider_->setVolume(v); }
    void setEngineHandle(...) { strategy_->setEngineHandle(...); }
};
```

**Benefits:**
- ✅ AudioPlayer is pure orchestrator (SRP compliance)
- ✅ Platform-agnostic via `IAudioHardwareProvider` (OCP compliance)
- ✅ Easy to test (mock both interfaces)
- ✅ Easy to extend (new platforms, new strategies)

---

## Class Responsibility Tabulation

### Current Classes by Responsibility

| Class | Primary Responsibility | Secondary Responsibilities | SRP Status | Priority |
|--------|---------------------|------------------------|-------------|----------|
| AudioPlayer | Audio playback orchestration | - State management | ❌ VIOLATION | HIGH |
| AudioUnitContext | State container | - Everything | ❌ CRITICAL | HIGH |
| IAudioMode | Mode behavior abstraction | - Rendering | ⚠️ VIOLATION | HIGH |
| IAudioRenderer | Rendering abstraction | - Mode behavior | ⚠️ VIOLATION | HIGH |
| ThreadedAudioMode | Threaded mode lifecycle | - Rendering | ⚠️ VIOLATION | MEDIUM |
| SyncPullAudioMode | Sync-pull mode lifecycle | - Rendering | ⚠️ VIOLATION | MEDIUM |
| ThreadedRenderer | Threaded rendering | - Mode lifecycle | ⚠️ VIOLATION | MEDIUM |
| SyncPullRenderer | Sync-pull rendering | - Mode lifecycle | ⚠️ VIOLATION | MEDIUM |
| SimulationLoop | Orchestration | - Audio control | ⚠️ BORDERLINE | LOW |
| CircularBuffer | Ring buffer management | - None | ✅ PASS | N/A |
| EngineConfig | Bridge API wrapper | - None | ✅ PASS | N/A |
| IInputProvider | Input abstraction | - None | ✅ PASS | N/A |
| KeyboardInputProvider | Keyboard input implementation | - None | ✅ PASS | N/A |
| IPresentation | Display abstraction | - None | ✅ PASS | N/A |
| ConsolePresentation | Console display implementation | - None | ✅ PASS | N/A |
| ITelemetryWriter | Telemetry output abstraction | - None | ✅ PASS | N/A |
| ILogging | Logging abstraction | - None | ✅ PASS | N/A |

---

## Recommended Action Plan

### Priority 1: Complete Strategy Integration (Week 1-2)

1. **Move Strategy Files from Build Cache:**
   - Move `IAudioStrategy.h` from build cache to `src/audio/strategies/`
   - Move `ThreadedStrategy.h/cpp` from build cache to `src/audio/strategies/`
   - Move `SyncPullStrategy.h/cpp` from build cache to `src/audio/strategies/`

2. **Wire AudioPlayer to IAudioStrategy:**
   - Update `AudioPlayer::initialize()` to use `IAudioStrategy*`
   - Update `SimulationLoop` to use `IAudioStrategy*`
   - Remove `IAudioRenderer` references

3. **Delete Old IAudioMode Files:**
   - Delete `src/audio/modes/IAudioMode.h`
   - Delete `src/audio/modes/ThreadedAudioMode.h/cpp`
   - Delete `src/audio/modes/SyncPullAudioMode.h/cpp`
   - Delete `src/audio/modes/AudioModeFactory.h/cpp`

4. **Delete Old IAudioRenderer Files:**
   - Delete `src/audio/renderers/IAudioRenderer.h`
   - Delete `src/audio/renderers/ThreadedRenderer.h/cpp`
   - Delete `src/audio/renderers/SyncPullRenderer.h/cpp`
   - Delete `src/audio/renderers/AudioRendererFactory.h/cpp`

5. **Update Tests:**
   - Update all tests to use `IAudioStrategy` interface
   - Update all tests to mock `IAudioStrategy`

### Priority 2: Platform Abstraction (Week 2-3)

1. **Move Hardware Provider Files:**
   - Move `IAudioHardwareProvider.h` from build cache to `src/audio/hardware/`
   - Move `CoreAudioHardwareProvider.h/cpp` from build cache to `src/audio/hardware/`

2. **Wire AudioPlayer to IAudioHardwareProvider:**
   - Remove `AudioUnit` member from `AudioPlayer`
   - Add `IAudioHardwareProvider*` member
   - Delegate all CoreAudio calls to `IAudioHardwareProvider`

3. **Create Diagnostics Struct:**
   - Create `AudioDiagnostics.h` with unified diagnostics
   - Replace diagnostic atomics in `AudioUnitContext`

4. **Refactor State Management:**
   - Split `AudioUnitContext` into focused structs
   - Update all strategy implementations to use new state model
   - Test thoroughly (buffer math, timing calculations)

### Priority 3: Cleanup and Documentation (Week 3-4)

1. **Remove Dead Code:**
   - Delete unused files
   - Remove dead code paths
   - Update CMakeLists.txt

2. **Update Documentation:**
   - Update `AUDIO_MODULE_ARCHITECTURE.md` with new structure
   - Update `ARCHITECTURE_TODO.md` with completed work
   - Create migration guide from old to new architecture

3. **Update Tests:**
   - Ensure all tests pass with new architecture
   - Add integration tests for `IAudioHardwareProvider`
   - Add unit tests for new state management

---

## Risk Assessment

### High-Risk Areas
1. **AudioUnitContext Refactoring:** Complex state, risk of bugs during split
2. **Integration of New Strategy:** Breaking changes to existing flow
3. **Platform Abstraction:** CoreAudio code needs careful extraction

### Medium-Risk Areas
1. **Test Updates:** Significant test changes needed
2. **Documentation Sync:** Multiple docs need updates
3. **Backwards Compatibility:** Breaking changes to existing interfaces

### Low-Risk Areas
1. **Cleanup:** Deleting old code is straightforward
2. **File Organization:** Moving files is safe
3. **New Interfaces:** Well-defined, low risk

---

## Conclusion

**Current State:**
- ✅ Foundation in place: Modular structure, good interfaces
- ⚠️ In transition: New strategy interface designed but not integrated
- ❌ Critical issues: AudioUnitContext SRP violation, IAudioMode coupling

**Recommendation:**
Proceed with Priority 1 (Strategy Integration) before addressing other concerns. The new strategy interface (`IAudioStrategy`) has been designed and partially implemented, and it directly addresses the WET coupling issue between `IAudioMode` and `IAudioRenderer`.

**Readiness:**
- ✅ Code analysis: Complete
- ✅ Issue identification: Complete
- ✅ Solution design: Complete
- ⏳ Implementation: Pending (files in build cache need to be integrated)
- ⏳ Testing: Pending (need to verify new architecture works)
- ⏳ Documentation: Pending (need to update docs after integration)

---

## Post-Audit Status Update (2026-04-15)

All three critical issues identified in this audit have been resolved:

### Issue 1: AudioUnitContext SRP Violation -- RESOLVED
- **Was:** 76-line monolithic struct with 20+ atomic variables
- **Now:** Replaced by composed `BufferContext` (AudioState + BufferState + Diagnostics + CircularBuffer)
- **Files:** `src/audio/state/BufferContext.h`, `AudioState.h`, `BufferState.h`, `Diagnostics.h`

### Issue 2: IAudioMode + IAudioRenderer WET Coupling -- RESOLVED
- **Was:** Two overlapping interfaces with duplicate responsibilities
- **Now:** Single `IAudioStrategy` interface with `ThreadedStrategy` and `SyncPullStrategy` implementations
- **Deleted:** `IAudioMode`, `IAudioRenderer`, all mode/renderer implementations, `StrategyAdapter` bridge
- **Files:** `src/audio/strategies/IAudioStrategy.h`, `ThreadedStrategy.h/.cpp`, `SyncPullStrategy.h/.cpp`

### Issue 3: AudioPlayer Platform Coupling -- RESOLVED
- **Was:** Direct `AudioUnit` member in AudioPlayer
- **Now:** `IAudioHardwareProvider` abstraction with `CoreAudioHardwareProvider` implementation
- **Files:** `src/audio/hardware/IAudioHardwareProvider.h`, `CoreAudioHardwareProvider.h/.cpp`

### Remaining Gaps
1. AudioPlayer creates `CoreAudioHardwareProvider` directly (should use factory)
2. Legacy `static audioUnitCallback` still declared in AudioPlayer.h but unused
3. Duplicate `IAudioSource` interfaces (one in `src/audio/common/` unused, one in `src/AudioSource.h` used)
4. `ITelemetryWriter` has no concrete implementation

### Architecture Docs Updated
- `.github/ARCH-001` -- IAudioStrategy consolidation: COMPLETE
- `.github/ARCH-002` -- ITelemetryProvider: PARTIALLY COMPLETE (interface exists, no implementation)
- `.github/ARCH-003` -- IAudioPlatform extraction: COMPLETE (as `IAudioHardwareProvider`)
- `.github/ARCH-004` -- Strategy Interface Unification: COMPLETE

---

*End of Architecture Audit*
