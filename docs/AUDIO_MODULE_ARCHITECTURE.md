# Audio Module Architecture

**Document Version:** 3.5
**Date:** 2026-04-08
**Status:** ✅ Current Architecture (Production)
**Author:** Audio Systems Architect

---

## Executive Summary

This document describes the **current production audio architecture** for the macOS CLI. The architecture uses a unified Strategy pattern that consolidates the previous IAudioMode and IAudioRenderer interfaces into a single IAudioStrategy interface, providing clean separation of concerns while preserving all diagnostic features.

### Key Architectural Decisions (Updated for v3.2)

| Decision | Rationale |
|----------|-----------|
| **IAudioStrategy unified interface** | Single strategy class replaces coupled mode+renderer pair |
| **StrategyContext composed state** | Separates AudioState, BufferState, Diagnostics for SRP compliance |
| **IAudioHardwareProvider abstraction** | Platform-specific audio hardware (CoreAudio, AVAudioEngine, I2S) |
| **AudioPlayer as orchestrator** | Manages AudioUnit lifecycle, delegates to injected strategy |
| **Platform-specific** | macOS CoreAudio - optimized for current use case |
| **Rich diagnostics** | Stateful StrategyContext tracks timing, buffer health, underruns |
| **SOLID compliance** | Improved SRP, OCP, LSP, ISP, DIP through composition and abstraction |

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           CLI Application                               │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  CLIMain.cpp - Entry point, DI wiring                          │    │
│  │  Creates: IAudioStrategy, IInputProvider, IPresentation         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                    │                                     │
│                                    ▼                                     │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  SimulationLoop.cpp - Main loop orchestration                   │    │
│  │  runSimulation() → runUnifiedAudioLoop()                        │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     Audio Strategy Layer (IAudioStrategy)                 │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  IAudioStrategy Interface (Unified)                           │    │
│  │  - getName() / isEnabled() / getModeString()                    │    │
│  │  - render(context, ioData, numberFrames)                        │    │
│  │  - AddFrames(context, buffer, frameCount)                       │    │
│  │  - configure() / reset()                                        │    │
│  │  - shouldDrainDuringWarmup()                                    │    │
│  │  - getDiagnostics() / getProgressDisplay()                        │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────┐  ┌─────────────────────────────┐        │
│  │   ThreadedStrategy        │  │   SyncPullStrategy         │        │
│  │   (cursor-chasing)         │  │   (on-demand rendering)     │        │
│  │                            │  │                             │        │
│  │ • Pre-fill buffer          │  │ • No pre-buffer             │        │
│  │ • Main loop generates      │  │ • Audio callback generates  │        │
│  │ • Cursor-chasing logic     │  │ • Low latency               │        │
│  │ • Robust to physics spikes │  │ • Sensitive to timing       │        │
│  │                            │  │                             │        │
│  │ Reads from CircularBuffer  │  │ Calls RenderOnDemand       │        │
│  │ Tracks cursors            │  │ Measures timing            │        │
│  │ Detects underruns         │  │ Reports budget             │        │
│  └─────────────────────────────┘  └─────────────────────────────┘        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        AudioPlayer (Orchestrator)                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  AudioPlayer                                                    │    │
│  │  - initialize(IAudioStrategy*, ...)                           │    │
│  │  - start() / stop()                                            │    │
│  │  - setVolume(float)                                            │    │
│  │  - playBuffer() / addToCircularBuffer()                         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  StrategyContext (Composed State - SRP Compliant)             │    │
│  │  - AudioState: isPlaying, sampleRate, volume                  │    │
│  │  - BufferState: readPointer, writePointer, frameCount           │    │
│  │  - Diagnostics: timing, buffer health, underruns               │    │
│  │  - CircularBuffer*: Non-owning pointer to audio data           │    │
│  │  - strategy*: Current rendering strategy                          │    │
│  │  - engineHandle, engineAPI: Bridge access                        │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                    Audio Hardware Layer (IAudioHardwareProvider)            │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  IAudioHardwareProvider Interface                               │    │
│  │  - initialize() / shutdown()                                   │    │
│  │  - start() / stop()                                            │    │
│  │  - setVolume(float)                                            │    │
│  │  - setCallback(renderCallback)                                   │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────┐                                        │
│  │  CoreAudioHardwareProvider │                                        │
│  │  (macOS implementation)  │                                        │
│  │                            │                                        │
│  │ • AudioUnit setup         │                                        │
│  │ • Audio callback wiring   │                                        │
│  │ • Volume control         │                                        │
│  └─────────────────────────────┘                                        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                     Bridge (engine-sim-bridge)                           │
│  (Platform-agnostic C API - physics and audio generation)                │
│                                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Lifecycle: Create, LoadScript, SetLogging, Destroy            │    │
│  │  Control: SetThrottle, SetIgnition, Update                      │    │
│  │  Audio: RenderOnDemand, ReadAudioBuffer, GetStats               │    │
│  │  Telemetry: ITelemetryWriter integration (C++)                    │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Bridge Utility Pattern

### CLI Utilities vs Bridge Services

**IMPORTANT:** Audio utilities like `FillSilence()` live in the **CLI**, not the bridge.

**Rationale:**
- Bridge is a platform-agnostic C API for physics and audio generation
- CLI contains all platform-specific audio operations (CoreAudio, buffering, utilities)
- Both CLI and any future clients use CLI utilities for common operations
- Bridge remains focused on simulation and audio synthesis

**CLI Utilities (src/audio/utils/):**
- `AudioUtils.cpp/h` - DRY helpers for common audio operations
  - `FillSilence(float* buffer, int frames)` - Zero float buffer
  - `FillSilence(AudioBufferList* bufferList, int frames)` - Zero AudioBufferList
- Used by: ThreadedRenderer, SyncPullRenderer
- Test coverage: `test/unit/AudioUtilsTest.cpp`

**Bridge C API (engine-sim-bridge):**
- `EngineSimRenderOnDemand()` - Generate audio samples from simulation
- `EngineSimGetStats()` - Read engine state (RPM, load, etc.)
- `EngineSimUpdate()` - Advance simulation
- Platform-agnostic C interface

**Usage Pattern:**
```cpp
// CLI (ThreadedRenderer.cpp)
#include "audio/utils/AudioUtils.h"

void handleUnderrun() {
    audio::utils::FillSilence(buffer, frames);  // CLI utility
}

void generateAudio() {
    EngineSimRenderOnDemand(handle, buffer, frames);  // Bridge API
}
```

---

## Platform Abstraction Layer (Phase 4)

### IAudioHardwareProvider Interface

**Purpose:** Abstracts platform-specific audio hardware for cross-platform support

**Architectural Decision:**
- **OCP Compliance:** New platforms can be added without modifying existing code
- **DIP Compliance:** Strategies and AudioPlayer depend on abstraction, not concrete implementations
- **Cross-Platform:** Enables iOS (AVAudioEngine) and ESP32 (I2S) implementations
- **Clean Separation:** Platform-specific code isolated in hardware layer

**Current Implementation:**
- `CoreAudioHardwareProvider`: macOS CoreAudio implementation (production)
- Uses AudioUnit for real-time audio playback
- Handles CoreAudio-specific setup, callbacks, and lifecycle

**Future Implementations:**
- `AVAudioPlatform`: iOS AVAudioEngine implementation
- `I2SPlatform`: ESP32 I2S driver implementation

**Key Methods:**
```cpp
class IAudioHardwareProvider {
    virtual bool initialize(int sampleRate) = 0;
    virtual void shutdown() = 0;
    virtual bool start() = 0;
    virtual void stop() = 0;
    virtual void setVolume(float volume) = 0;
    virtual void setCallback(AudioCallback callback, void* context) = 0;
    virtual const char* getName() const = 0;
};
```

**Integration with AudioPlayer:**
```cpp
// AudioPlayer delegates to IAudioStrategy for rendering logic
class AudioPlayer {
    IAudioHardwareProvider* hardware_;
    IAudioStrategy* strategy_;
    StrategyContext context_;

    void initialize() {
        hardware_->initialize(sampleRate);
        hardware_->setCallback(audioCallback, this);

        // Initialize strategy with context
        AudioStrategyConfig config{sampleRate, channels};
        strategy_->initialize(&context, config);
    }

    void startPlayback() {
        // Start playback via strategy
        strategy_->startPlayback(&context, engineHandle, engineAPI);
        hardware_->start();
    }

    void setVolume(float volume) {
        hardware_->setVolume(volume);
    }

    // Audio callback delegates to strategy
    void audioCallback(AudioBufferList* ioData, UInt32 numberFrames) {
        strategy_->render(&context, ioData, numberFrames);
    }
};
```

**Key Integration Points:**
1. **Initialization:** AudioPlayer creates `StrategyContext` and passes to strategy via `initialize()`
2. **Lifecycle:** AudioPlayer delegates `startPlayback()` and `stopPlayback()` to strategy
3. **Rendering:** Audio callback passes control to strategy's `render()` method
4. **Configuration:** Strategy receives `AudioStrategyConfig` with sample rate and channels
5. **State Management:** StrategyContext contains all shared state (audio, buffer, diagnostics)

**Strategy Context Lifecycle:**
1. **Creation:** AudioPlayer creates StrategyContext during initialization
2. **Configuration:** AudioStrategyConfig passed during strategy initialization
3. **Runtime:** Strategy reads/writes StrategyContext during render calls
4. **Cleanup:** Strategy can reset StrategyContext state when needed

**Benefits:**
1. **Cross-Platform:** Single codebase supports macOS, iOS, ESP32
2. **Testability:** Mock hardware providers for unit testing
3. **Maintainability:** Platform-specific code isolated and focused
4. **Extensibility:** New platforms added by implementing IAudioHardwareProvider

---

## Component Details

### IAudioStrategy (Unified Strategy Interface)

**Purpose:** Unified audio generation strategy interface - replaces previous IAudioMode + IAudioRenderer split

**Architectural Improvement:**
- **SRP Compliance:** Single interface replaces two coupled interfaces
- **OCP Compliance:** New strategies can be added without modifying existing code
- **SOLID Principles:** Clean separation of concerns through composition

**Implementations:**
- `ThreadedStrategy`: Cursor-chasing mode with pre-filled circular buffer
- `SyncPullStrategy`: On-demand rendering in audio callback

### ThreadedStrategy (Cursor-Chasing Implementation)

**Purpose:** Implements robust audio generation using circular buffer with cursor-chasing

**Key Characteristics:**
- **Pre-fill Strategy:** Fills circular buffer with ~100ms of audio before playback starts
- **Cursor-Chasing:** Main loop generates ahead of playback cursor to maintain buffer lead
- **Robustness:** Buffer absorbs physics simulation spikes (~16ms typical)
- **Thread Management:** Internally manages audio generation thread (main loop)
- **Latency:** ~100ms initial latency, then stable cursor-chasing operation

**Implementation Details:**
```cpp
class ThreadedStrategy : public IAudioStrategy {
public:
    // Core rendering: Read from circular buffer at read pointer
    bool render(StrategyContext* context, AudioBufferList* ioData, UInt32 numberFrames) override;

    // Frame addition: Write to circular buffer at write pointer
    bool AddFrames(StrategyContext* context, float* buffer, int frameCount) override;

    // Lifecycle: Internal thread management
    bool startPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
    void stopPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;

    // Buffer management
    void prepareBuffer(StrategyContext* context) override;  // Pre-fill buffer
    void resetBufferAfterWarmup(StrategyContext* context) override;  // Drain warmup audio
};
```

**Internal State:**
- Uses StrategyContext::BufferState for read/write pointers and underrun tracking
- Calculates available frames and free space for buffer management
- Updates diagnostics with render timing and buffer health metrics
- Handles buffer wrap-around correctly for continuous operation

**Use Case:** Production playback where robustness to physics spikes is critical

### SyncPullStrategy (On-Demand Implementation)

**Purpose:** Implements low-latency audio generation using lock-step simulation

**Key Characteristics:**
- **On-Demand Rendering:** Generates audio directly in audio callback (no separate thread)
- **Lock-Step Simulation:** Simulation advances in sync with audio playback
- **Low Latency:** Minimal latency (~10ms callback interval)
- **Sensitivity:** Directly affected by physics simulation timing
- **No Buffer Management:** No circular buffer or cursor-chasing needed

**Implementation Details:**
```cpp
class SyncPullStrategy : public IAudioStrategy {
public:
    // Core rendering: Generate audio on-demand from engine simulator
    bool render(StrategyContext* context, AudioBufferList* ioData, UInt32 numberFrames) override;

    // Frame addition: Not used (generates directly in render)
    bool AddFrames(StrategyContext* context, float* buffer, int frameCount) override;

    // Lifecycle: No thread management (renders in callback)
    bool startPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
    void stopPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;

    // Buffer management: No-ops (no buffer to manage)
    void prepareBuffer(StrategyContext* context) override;  // No-op
    void resetBufferAfterWarmup(StrategyContext* context) override;  // No-op
};
```

**Internal State:**
- Minimal internal state (no buffer management needed)
- Uses StrategyContext::engineHandle and StrategyContext::engineAPI for on-demand rendering
- Provides detailed timing diagnostics (render time, headroom, budget)
- Measures callback timing for real-time performance monitoring

**Use Case:** Testing, low-latency scenarios where immediate audio response is critical

**Key Methods:**
```cpp
class IAudioStrategy {
    // Core strategy methods
    virtual const char* getName() const = 0;
    virtual bool isEnabled() const = 0;
    virtual bool render(StrategyContext* context, AudioBufferList* ioData, UInt32 numberFrames) = 0;
    virtual bool AddFrames(StrategyContext* context, float* buffer, int frameCount) = 0;

    // Strategy-specific behavior
    virtual bool shouldDrainDuringWarmup() const = 0;
    virtual std::string getDiagnostics() const = 0;
    virtual std::string getProgressDisplay() const = 0;
    virtual std::string getModeString() const = 0;

    // Lifecycle
    virtual void configure(const AudioStrategyConfig& config) = 0;
    virtual void reset() = 0;
};
```

### StrategyContext (Composed State Model)

**Purpose:** Composed context containing focused state components for audio strategies

**Architectural Improvement:**
- **SRP Compliance:** Replaces massive AudioUnitContext with focused state structs
- **Composition:** AudioState, BufferState, Diagnostics composed together
- **Testability:** Individual state components can be tested independently

**Composition:**
```cpp
struct StrategyContext {
    AudioState audioState;           // Core playback state (isPlaying, sampleRate, volume)
    BufferState bufferState;         // Buffer management (pointers, counters)
    Diagnostics diagnostics;           // Performance metrics and timing
    CircularBuffer* circularBuffer;   // Non-owning pointer to audio data
    IAudioStrategy* strategy;        // Current rendering strategy
    EngineSimHandle engineHandle;     // Bridge simulator handle
    const EngineSimAPI* engineAPI;  // Bridge API for sync-pull mode
};
```

### State Components (Focused SRP-Compliant Structs)

**AudioState:**
```cpp
struct AudioState {
    std::atomic<bool> isPlaying;   // Current playback state (true = playing, false = stopped)
    int sampleRate;                  // Sample rate in Hz (e.g., 44100, 48000)

    void reset();                    // Reset state to initial values
};
```

**BufferState:**
```cpp
struct BufferState {
    std::atomic<int> writePointer;       // Write pointer in circular buffer (next write position)
    std::atomic<int> readPointer;        // Read pointer in circular buffer (cursor-chasing)
    std::atomic<int> underrunCount;       // Count of buffer underrun events
    int fillLevel;                       // Current fill level of buffer (0 to capacity)
    int capacity;                        // Buffer capacity in frames

    // Helper methods
    int availableFrames() const;  // Calculate available frames in buffer
    int freeSpace() const;      // Calculate free space in buffer
    void reset();              // Reset all state to initial values
};
```

**Diagnostics:**
```cpp
struct Diagnostics {
    std::atomic<double> lastRenderMs;          // Last render time in milliseconds
    std::atomic<double> lastHeadroomMs;        // Last headroom time in milliseconds
    std::atomic<double> lastBudgetPct;         // Last render time budget percentage used
    std::atomic<double> lastFrameBudgetPct;    // Last frame count requested vs available
    std::atomic<int64_t> totalFramesRendered;  // Total number of frames rendered

    // Helper methods
    void recordRender(double renderTimeMs, int framesRendered);
    Snapshot getSnapshot() const;  // Get thread-safe snapshot of all metrics
    void reset();               // Reset all diagnostic counters
};
```

### IAudioStrategyFactory (Factory Pattern)

**Purpose:** Creates appropriate IAudioStrategy implementations based on requested AudioMode

**Architectural Improvement:**
- **OCP Compliance:** New strategies can be added without modifying factory switch statement
- **DI Compliance:** Dependencies (logger) injected via factory parameters
- **Clean Separation:** Factory handles strategy creation, main code doesn't need to know concrete implementations

**Implementations:**
- `IAudioStrategyFactory::createStrategy(AudioMode mode, ILogging* logger)` - Factory method
- Returns `std::unique_ptr<IAudioStrategy>` for automatic memory management
- Supports two modes: `AudioMode::Threaded` and `AudioMode::SyncPull`

**Key Methods:**
```cpp
class IAudioStrategyFactory {
public:
    // Factory method for creating strategies
    static std::unique_ptr<IAudioStrategy> createStrategy(
        AudioMode mode,
        ILogging* logger = nullptr
    );
};
```

**Usage Pattern:**
```cpp
// Create strategy via factory
auto strategy = IAudioStrategyFactory::createStrategy(
    AudioMode::Threaded,
    logger  // Optional logger injection
);

// Strategy is now ready for use
strategy->initialize(context, config);
```

### IAudioHardwareProvider (Platform Abstraction - Phase 4)

**Purpose:** Abstracts platform-specific audio hardware for cross-platform support

**Phase 4 Implementation Status:**
- ✅ `IAudioHardwareProvider` interface implemented (`src/audio/hardware/IAudioHardwareProvider.h`)
- ✅ `CoreAudioHardwareProvider` macOS implementation implemented (`src/audio/hardware/CoreAudioHardwareProvider.cpp/h`)
- ✅ `AudioHardwareProviderFactory` for platform detection implemented (`src/audio/hardware/AudioHardwareProviderFactory.cpp`)
- ✅ Integration with IAudioStrategy patterns established
- ⚠️ AudioPlayer integration in progress (still using IAudioRenderer from older pattern)

**Architectural Improvement:**
- **OCP Compliance:** New platforms can be added without modifying existing code
- **DIP Compliance:** Strategies depend on abstraction, not concrete hardware
- **Cross-Platform:** Enables iOS (AVAudioEngine) and ESP32 (I2S) implementations
- **SRP Compliance:** Single responsibility for hardware lifecycle management

**Implementations:**
- `CoreAudioHardwareProvider`: macOS CoreAudio implementation (current - commit e8a1987)
- `AVAudioPlatform`: iOS AVAudioEngine implementation (future - blocked by iOS hardware)
- `I2SPlatform`: ESP32 I2S driver implementation (future - blocked by ESP32 hardware)

**Key Methods:**
```cpp
class IAudioHardwareProvider {
    // Lifecycle Methods
    virtual bool initialize(const AudioStreamFormat& format) = 0;
    virtual void cleanup() = 0;

    // Playback Control Methods
    virtual bool startPlayback() = 0;
    virtual void stopPlayback() = 0;

    // Volume Control Methods
    virtual void setVolume(double volume) = 0;
    virtual double getVolume() const = 0;

    // Callback Registration Methods
    virtual bool registerAudioCallback(const AudioCallback& callback) = 0;

    // Diagnostic Methods
    virtual AudioHardwareState getHardwareState() const = 0;
    virtual void resetDiagnostics() = 0;
};
```

**Integration Patterns with Strategies:**

**ThreadedStrategy Integration:**
- ThreadedStrategy uses hardware callback for cursor-chasing feedback
- Hardware provides callback with read pointer position updates
- Strategy maintains 100ms buffer lead ahead of playback cursor
- Enables robust playback despite physics simulation spikes

**SyncPullStrategy Integration:**
- SyncPullStrategy uses hardware callback for on-demand rendering
- Hardware provides callback with frame requests from audio system
- Strategy generates audio directly in callback (no separate thread)
- Provides lowest latency but sensitive to timing variations

**Callback Signature:**
```cpp
// Platform-agnostic callback signature
using AudioCallback = std::function<int(void* refCon, void* actionFlags,
                                           const void* timeStamp,
                                           int busNumber, int numberFrames,
                                           PlatformAudioBufferList* ioData)>;
```

**Hardware State Structure:**
```cpp
struct AudioHardwareState {
    bool isInitialized;      // Hardware has been initialized
    bool isPlaying;          // Hardware is currently playing
    bool isCallbackActive;   // Audio callback is currently active
    double currentVolume;     // Current volume level (0.0 to 1.0)
    int underrunCount;       // Number of buffer underruns
    int overrunCount;        // Number of buffer overruns
};
```

**Platform Detection Factory:**
```cpp
class AudioHardwareProviderFactory {
    // Platform detection logic
    static std::unique_ptr<IAudioHardwareProvider> createProvider(
        ILogging* logger = nullptr
    );
};
```

**Usage Pattern:**
```cpp
// Create hardware provider via factory
auto hardware = AudioHardwareProviderFactory::createProvider(logger);

// Configure audio format
AudioStreamFormat format{44100, 2, 32, true, true};
hardware->initialize(format);

// Register callback for rendering
hardware->registerAudioCallback(audioCallback);

// Start playback
hardware->startPlayback();
```

**Relationship to IAudioStrategy:**
- `IAudioStrategy` implementations delegate platform operations to `IAudioHardwareProvider`
- ThreadedStrategy: Uses hardware callback for cursor-chasing feedback
- SyncPullStrategy: Uses hardware callback for on-demand rendering
- Clear separation: Strategy handles rendering logic, hardware handles platform specifics

**Integration with AudioPlayer (Current State):**
- **Status:** AudioPlayer still uses legacy `IAudioRenderer` interface (line 21 in AudioPlayer.h)
- **Transition:** `IAudioHardwareProvider` ready for integration when AudioPlayer is refactored
- **Pattern:** Use `StrategyAdapter` (in `src/audio/adapters/`) to bridge new strategy interface to old AudioPlayer
- **Path Forward:** Full AudioPlayer integration with `IAudioHardwareProvider` planned for future phase

### Legacy Components (Deprecated)

**Note:** The following components have been replaced by the new strategy architecture:

**IAudioMode (Deprecated):**
- Replaced by `IAudioStrategy`
- Previous split between IAudioMode and IAudioRenderer was SRP violation
- New unified interface provides cleaner separation of concerns

**IAudioRenderer (Deprecated):**
- Functionality consolidated into `IAudioStrategy`
- Previous coupling with IAudioMode prevented independent swapping
- New architecture allows true strategy pattern implementation

**AudioUnitContext (Deprecated):**
- Replaced by `StrategyContext` composed state model
- Previous monolithic context was SRP violation
- New composition enables focused, testable state components

**IAudioPlatform (Deprecated - Phase 1-2):**
- Replaced by `IAudioHardwareProvider` (Phase 4)
- Previous `IAudioPlatform` interface was less flexible than current `IAudioHardwareProvider`
- Location: `src/audio/platform/IAudioPlatform.h` (deprecated)
- Implementation: `src/audio/platform/macos/CoreAudioPlatform.h/cpp` (deprecated)

**Key Differences Between Old and New:**
| Aspect | IAudioPlatform (Deprecated) | IAudioHardwareProvider (Current) |
|---------|---------------------------|-------------------------------|
| **Location** | `src/audio/platform/` | `src/audio/hardware/` |
| **Interface Focus** | Basic platform abstraction | Comprehensive hardware lifecycle management |
| **Callback Support** | Basic callback registration | Platform-agnostic callback with full hardware state |
| **Volume Control** | Basic setVolume() | setVolume() + getVolume() + persistence |
| **Diagnostics** | Limited | Comprehensive `AudioHardwareState` structure |
| **Format Support** | Simple sample rate | Full `AudioStreamFormat` configuration |
| **Factory Pattern** | Manual instantiation | `AudioHardwareProviderFactory` with platform detection |
| **Integration** | Not integrated with strategies | Ready for IAudioStrategy integration |

**Migration Path:**
1. **Phase 1-2 (Completed):** Created `IAudioPlatform` and `CoreAudioPlatform` (commit e8a1987)
2. **Phase 4 (Completed):** Replaced with `IAudioHardwareProvider` and `CoreAudioHardwareProvider`
3. **Phase 6 (Completed):** Created `IAudioStrategy` with better integration points
4. **Future Phase:** Full AudioPlayer integration with `IAudioHardwareProvider`

**Current Status:**
- `IAudioHardwareProvider` is production-ready and fully implemented
- CoreAudioHardwareProvider wraps macOS CoreAudio correctly
- AudioHardwareProviderFactory provides platform detection
- Integration with IAudioStrategy patterns established
- AudioPlayer legacy code still uses old interfaces (gradual migration path)

---

## Architecture Migration Guide

### Transition from IAudioMode/IAudioRenderer to IAudioStrategy

**Old Pattern (Deprecated):**
```cpp
// Separate interfaces required coupling
class IAudioMode {
    virtual void startPlayback() = 0;
    virtual IAudioRenderer* getRenderer() = 0;
};

class IAudioRenderer {
    virtual void render(AudioBufferList* ioData, UInt32 numberFrames) = 0;
};
```

**New Pattern (Current):**
```cpp
// Unified interface with clear lifecycle
class IAudioStrategy {
    virtual bool initialize(StrategyContext* context, const AudioStrategyConfig& config) = 0;
    virtual bool render(StrategyContext* context, AudioBufferList* ioData, UInt32 numberFrames) = 0;
    virtual bool AddFrames(StrategyContext* context, float* buffer, int frameCount) = 0;
    virtual void startPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void stopPlayback(StrategyContext* context, EngineSimHandle handle, const EngineSimAPI* api) = 0;
};
```

**Migration Benefits:**
1. **Single Responsibility:** One class handles all strategy concerns instead of split responsibilities
2. **Cleaner Lifecycle:** Clear initialization, configuration, and lifecycle methods
3. **Simpler Factory:** Create one strategy instead of mode + renderer pair
4. **Better Testing:** Easier to mock single interface vs coupled interfaces
5. **State Composition:** StrategyContext provides focused, composable state components

**Key Changes:**
- `IAudioMode` and `IAudioRenderer` interfaces → Replaced by `IAudioStrategy`
- `AudioUnitContext` monolithic struct → Replaced by `StrategyContext` (composed state)
- Separate mode+renderer factory → Replaced by `IAudioStrategyFactory`
- Thread management internal to strategies → No external thread control needed

---

## Strategy Behavior Comparison

| Aspect | Threaded Mode | Sync-Pull Mode |
|--------|--------------|----------------|
| **Buffer strategy** | Pre-filled 100ms+ buffer | No pre-buffer, generate on-demand |
| **Where audio is generated** | Main loop (separate thread) | Audio callback (real-time thread) |
| **Latency** | ~100ms (buffer size) | Minimal (~10ms) |
| **Robustness** | Excellent (buffer absorbs spikes) | Poor (physics spikes cause crackles) |
| **CPU usage** | Consistent | Bursty in audio callback |
| **Complexity** | Higher (cursor-chasing logic) | Lower (direct rendering) |
| **Use case** | Production, reliable playback | Testing, low-latency scenarios |

---

## SOLID Compliance

| Principle | Implementation |
|-----------|----------------|
| **SRP** | Each class has single responsibility: AudioPlayer (lifecycle), IAudioStrategy (unified rendering), StrategyContext (state composition), IAudioHardwareProvider (platform abstraction) |
| **OCP** | Strategy pattern allows adding new modes/renderers without modifying existing code. Platform abstraction allows adding new platforms (iOS, ESP32) without modifying AudioPlayer |
| **LSP** | All implementations honor their interface contracts |
| **ISP** | Interfaces are focused and minimal: IAudioStrategy, IAudioHardwareProvider, ITelemetryWriter/Reader |
| **DIP** | High-level modules (AudioPlayer) depend on abstractions (IAudioStrategy, IAudioHardwareProvider) |
| **DI** | Dependencies injected via constructor and factory patterns |
| **DRY** | Single IAudioStrategy interface replaces previous IAudioMode + IAudioRenderer split |
| **YAGNI** | Deprecated code removed, no unnecessary abstractions |

### SRP Improvements (v3.0)
- **Before:** AudioUnitContext was a monolithic struct with mixed responsibilities (audio state, buffer management, diagnostics)
- **After:** StrategyContext composes focused AudioState, BufferState, Diagnostics structs
- **Benefit:** Each state component has single responsibility, easier to test and maintain

### OCP Improvements (v3.0)
- **Before:** Adding new platforms required modifying AudioPlayer directly
- **After:** IAudioHardwareProvider enables adding platforms without modifying existing code
- **Benefit:** iOS and ESP32 can be added by implementing IAudioHardwareProvider interface

### ISP Improvements (v3.2)
- **Before:** IAudioMode and IAudioRenderer had overlapping responsibilities
- **After:** Single IAudioStrategy interface provides focused, minimal contract
- **Benefit:** Clients depend only on methods they use

### State Separation Benefits (v3.2)
- **AudioState:** Focused on playback state (isPlaying, sampleRate) - single responsibility
- **BufferState:** Focused on buffer management (pointers, counters, underruns) - single responsibility
- **Diagnostics:** Focused on performance metrics (timing, health, throughput) - single responsibility
- **Composition:** StrategyContext composes these focused components - clean separation
- **Testability:** Individual state components can be unit tested independently
- **Maintainability:** Changes to one state component don't affect others

---

## File Organization (Updated for v3.2)

```
src/audio/
├── common/
│   ├── CircularBuffer.cpp/h          # Thread-safe ring buffer
│   └── IAudioSource.h              # Simple audio source interface
├── state/                         # NEW: Focused state components (SRP)
│   ├── StrategyContext.h             # Composed context for strategies
│   ├── AudioState.h                # Core playback state (isPlaying, sampleRate, volume)
│   ├── BufferState.h               # Buffer management state (read/write pointers, counters)
│   └── Diagnostics.h               # Performance and timing metrics
├── strategies/
│   ├── IAudioStrategy.h            # Unified strategy interface
│   ├── IAudioStrategyFactory.cpp    # Factory for creating strategies
│   ├── ThreadedStrategy.cpp/h      # Cursor-chasing implementation
│   └── SyncPullStrategy.cpp/h      # On-demand rendering implementation
├── hardware/                      # NEW: Platform abstraction (OCP)
│   ├── IAudioHardwareProvider.h    # Platform-agnostic hardware interface
│   └── CoreAudioHardwareProvider.cpp/h # macOS CoreAudio implementation
├── adapters/
│   ├── StrategyAdapter.cpp/h        # Legacy compatibility adapter
│   └── StrategyAdapterFactory.cpp/h # Factory for adapters
├── utils/
│   └── AudioUtils.cpp/h          # DRY helpers (FillSilence) - CLI utilities
└── renderers/                     # DEPRECATED: Merged into strategies/
    └── IAudioRenderer.h           # Legacy interface (deprecated)
```

**Architecture Notes:**
- **state/** folder contains focused, SRP-compliant state components (header-only structs)
- **strategies/** folder contains unified IAudioStrategy implementations
- **hardware/** folder contains platform abstraction for cross-platform support
- **adapters/** folder provides legacy compatibility with old IAudioRenderer interface
- **renderers/** folder contains deprecated IAudioRenderer interface (kept for compatibility)

**Cross-Platform Support:**
- IAudioHardwareProvider enables iOS (AVAudioEngine) and ESP32 (I2S) implementations
- Current implementation: CoreAudioHardwareProvider (macOS)
- Future implementations: AVAudioPlatform (iOS), I2SPlatform (ESP32)

**Platform-Specific Implementation Requirements:**

**macOS (Current - CoreAudioHardwareProvider):**
- **Audio Framework:** CoreAudio AudioUnit
- **Device Management:** Default output device, can be extended for device selection
- **Volume Control:** `AudioUnitSetParameter(kHALOutputParam_Volume)`
- **Callback Handling:** `AudioUnitSetProperty(kAudioUnitProperty_SetRenderCallback, ...)`
- **Sample Rates:** 44100Hz, 48000Hz, 88200Hz (standard macOS rates)
- **Channels:** Stereo (2 channels) - can be extended to mono
- **Thread Safety:** Real-time callback must not block

**iOS (Future - AVAudioPlatform):**
- **Audio Framework:** AVAudioEngine (iOS 7.0+)
- **Device Management:** AVAudioSession routing, automatic device handling
- **Volume Control:** `AVAudioMixerNode` volume property
- **Callback Handling:** `AVAudioEngineManualRenderingBlock` for real-time rendering
- **Sample Rates:** Device-dependent, typically 44100Hz or 48000Hz
- **Channels:** Stereo (2 channels) typical for iOS devices
- **Considerations:** iOS audio session management, background handling

**ESP32 (Future - I2SPlatform):**
- **Audio Framework:** ESP-IDF I2S driver
- **Device Management:** I2S driver initialization, GPIO pin configuration
- **Volume Control:** I2S built-in volume or software gain in DSP
- **Callback Handling:** DMA-based audio streaming, interrupt-driven
- **Sample Rates:** Configurable, typical 44100Hz for embedded audio
- **Channels:** Stereo (2 channels) typical for I2S
- **Considerations:** Low memory constraints, real-time requirements

**Cross-Platform Compatibility Matrix:**

| Feature | macOS | iOS | ESP32 |
|---------|--------|-----|--------|
| **Audio Framework** | CoreAudio | AVAudioEngine | I2S Driver |
| **Callback Type** | Real-time | Real-time | DMA/Interrupt |
| **Volume Control** | Hardware | Mixer Node | Hardware/Software |
| **Thread Safety** | Critical | Critical | Critical |
| **Memory Constraints** | Normal | Limited | Very Limited |
| **Implementation Status** | ✅ Production | 🔲 Future | 🔲 Future |

**Benefits of IAudioHardwareProvider Abstraction:**
1. **Write Once, Run Everywhere:** Core audio strategy logic works across platforms
2. **Platform Optimization:** Each platform can use optimal audio framework
3. **Test Isolation:** Mock hardware providers for unit testing
4. **Easy Debugging:** Platform-specific issues isolated to provider implementations
5. **Future Proof:** Adding new platforms doesn't require strategy changes

---

## SOLID Compliance (Updated for v3.4)

| Principle | Status | Notes |
|-----------|--------|-------|
| **SRP** | ✅ PASS | StrategyContext composition separates AudioState, BufferState, Diagnostics |
| **OCP** | ✅ PASS | IAudioStrategy and IAudioHardwareProvider enable extension without modification |
| **LSP** | ✅ PASS | All implementations honor interface contracts |
| **ISP** | ✅ PASS | Focused interfaces (IAudioStrategy, IAudioHardwareProvider) |
| **DIP** | ✅ PASS | High-level modules depend on abstractions, not concrete implementations |
| **DI** | ✅ PASS | Dependencies injected via constructor and factory patterns |
| **DRY** | ✅ PASS | No duplicate code, single strategy interface |
| **YAGNI** | ✅ PASS | No unnecessary abstractions, deprecated code removed |

### SRP Improvements (v3.4)

**StrategyContext Composition (Phase 6):**
- **Before:** AudioUnitContext was a monolithic struct with mixed responsibilities (audio state, buffer management, diagnostics, circular buffer, callback management)
- **After:** StrategyContext composes focused AudioState, BufferState, Diagnostics structs
- **Benefit:** Each state component has single responsibility, easier to test and maintain
- **Composition Pattern:** State components composed together rather than mixed in monolithic context

**State Component Separation:**
- `AudioState`: Only core playback state (isPlaying, sampleRate) - SRP compliant
- `BufferState`: Only buffer management (read/write pointers, underrun tracking) - SRP compliant
- `Diagnostics`: Only performance metrics (render timing, buffer health) - SRP compliant
- **Testability:** Each component can be unit tested independently

### OCP Improvements (v3.4)

**Strategy Pattern (Phase 6):**
- **Before:** Adding new rendering modes required modifying AudioPlayer directly and coupled IAudioMode/IAudioRenderer interfaces
- **After:** IAudioStrategy interface enables adding new strategies without modifying existing code
- **Benefit:** New strategies can be added by implementing IAudioStrategy, no changes to core code needed
- **Factory Pattern:** IAudioStrategyFactory creates strategies based on AudioMode enum

**Platform Abstraction (Phase 4):**
- **Before:** Adding new platforms required modifying AudioPlayer directly with CoreAudio-specific code
- **After:** IAudioHardwareProvider enables adding platforms without modifying existing code
- **Benefit:** iOS (AVAudioPlatform) and ESP32 (I2SPlatform) can be added by implementing interface
- **Platform Detection:** AudioHardwareProviderFactory automatically selects appropriate implementation

### ISP Improvements (v3.4)

**Interface Segregation Principles:**

**IAudioStrategy Interface:**
- **Focused Methods:** Each method has single, clear responsibility
- **Lifecycle Methods:** initialize(), startPlayback(), stopPlayback() - distinct lifecycle concerns
- **Rendering Methods:** render(), AddFrames() - clear separation between buffer operations
- **Diagnostic Methods:** getDiagnostics(), getProgressDisplay() - focused reporting

**IAudioHardwareProvider Interface:**
- **Lifecycle Separation:** initialize(), cleanup(), startPlayback(), stopPlayback() - clear lifecycle
- **Volume Control:** setVolume(), getVolume() - dedicated volume interface
- **Callback Management:** registerAudioCallback() - isolated callback registration
- **Diagnostics:** getHardwareState(), resetDiagnostics() - focused diagnostics

**Benefits:**
- **Client Flexibility:** Clients only depend on methods they actually use
- **Implementation Freedom:** Implementations don't need to fake unused methods
- **Testability:** Easy to create focused mocks for specific interfaces
- **Evolution:** Interfaces can evolve independently without breaking implementations

### DIP Improvements (v3.4)

**Dependency Inversion Layers:**

**Layer 1 - Strategy Level:**
- **Dependency:** IAudioStrategy implementations depend on StrategyContext abstraction
- **Inversion:** Strategies don't depend on concrete AudioState, BufferState, Diagnostics
- **Benefit:** Strategy logic independent of state implementation details

**Layer 2 - Hardware Level:**
- **Dependency:** IAudioStrategy implementations depend on IAudioHardwareProvider abstraction
- **Inversion:** Strategies don't depend on CoreAudio-specific CoreAudioHardwareProvider
- **Benefit:** Platform-specific code isolated, strategies work on any platform

**Layer 3 - AudioPlayer Level:**
- **Dependency:** AudioPlayer depends on IAudioStrategy and IAudioHardwareProvider abstractions
- **Inversion:** AudioPlayer doesn't depend on concrete ThreadedStrategy/SyncPullStrategy or CoreAudioHardwareProvider
- **Benefit:** Easy to swap strategies and platforms without modifying AudioPlayer

**Factory Pattern Implementation:**
- **Strategy Factory:** IAudioStrategyFactory creates strategies without clients knowing concrete classes
- **Hardware Factory:** AudioHardwareProviderFactory creates providers without clients knowing platform details
- **Construction:** Factories handle dependency injection (logger injection)
- **Benefit:** Complete decoupling between creation and usage

### DI Improvements (v3.4)

**Constructor Injection:**
- **Logger Injection:** All strategies and hardware providers accept optional ILogging* parameter
- **Factory Injection:** Factories inject logger during object creation
- **Default Values:** Constructor parameters provide sensible defaults when injection not available
- **Benefit:** Testability through mock injection, production flexibility

**Factory Pattern Dependency Injection:**
- **Strategy Creation:** IAudioStrategyFactory::createStrategy(AudioMode, ILogging*)
- **Hardware Creation:** AudioHardwareProviderFactory::createProvider(ILogging*)
- **Benefits:** Centralized creation logic, consistent dependency injection, easy testing

### DRY Improvements (v3.4)

**Eliminated Duplicate Code:**
- **Single Interface:** IAudioStrategy replaces IAudioMode + IAudioRenderer split
- **Single Factory:** IAudioStrategyFactory replaces separate mode and renderer factories
- **Unified State:** StrategyContext replaces AudioUnitContext monolithic structure
- **Benefit:** Less code to maintain, consistent behavior, fewer bugs

**DRY Pattern Application:**
- **Strategy Logic:** ThreadedStrategy and SyncPullStrategy share common patterns via IAudioStrategy interface
- **Hardware Abstraction:** All platforms implement same IAudioHardwareProvider interface
- **State Management:** All strategies use same StrategyContext composed state
- **Benefit:** Write business logic once, apply across all implementations

### YAGNI Improvements (v3.4)

**Removed Unnecessary Code:**
- **Deprecated Interfaces:** IAudioMode, IAudioRenderer, IAudioPlatform removed
- **Legacy Code:** Old CoreAudioPlatform replaced by CoreAudioHardwareProvider
- **Dead Code:** Unnecessary adapter and wrapper patterns removed

**Current vs. Future Needs:**
- **Current:** IAudioStrategy, IAudioHardwareProvider sufficient for current needs
- **Future:** AVAudioPlatform (iOS), I2SPlatform (ESP32) - blocked by hardware availability
- **Principle:** Don't implement features until actual need arises (YAGNI)

---

*End of Audio Module Architecture Document*
