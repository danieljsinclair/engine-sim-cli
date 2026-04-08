# Audio Module Architecture

**Document Version:** 3.0
**Date:** 2026-04-08
**Status:** Current Architecture (Production)
**Author:** Audio Systems Architect

---

## Executive Summary

This document describes the **current production audio architecture** for the macOS CLI. The architecture uses a unified Strategy pattern that consolidates the previous IAudioMode and IAudioRenderer interfaces into a single IAudioStrategy interface, providing clean separation of concerns while preserving all diagnostic features.

### Key Architectural Decisions (Updated for v3.0)

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
// AudioPlayer uses IAudioHardwareProvider abstraction
class AudioPlayer {
    IAudioHardwareProvider* hardware_;
    IAudioStrategy* strategy_;

    void initialize() {
        hardware_->initialize(sampleRate);
        hardware_->setCallback(audioCallback, this);
    }

    void startPlayback() {
        hardware_->start();
    }

    void setVolume(float volume) {
        hardware_->setVolume(volume);
    }
};
```

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
    bool isPlaying;
    int sampleRate;
    float volume;
    void reset();
};
```

**BufferState:**
```cpp
struct BufferState {
    std::atomic<int> readPointer;
    std::atomic<int> writePointer;
    std::atomic<int> frameCount;
    void reset();
};
```

**Diagnostics:**
```cpp
struct Diagnostics {
    std::atomic<int> underrunCount;
    std::atomic<double> lastRenderMs;
    std::atomic<double> lastHeadroomMs;
    std::atomic<double> lastBudgetPct;
    std::atomic<double> lastBufferTrendPct;
    void reset();
};
```

### IAudioHardwareProvider (Platform Abstraction)

**Purpose:** Abstracts platform-specific audio hardware for cross-platform support

**Architectural Improvement:**
- **OCP Compliance:** New platforms can be added without modifying existing code
- **DIP Compliance:** Strategies depend on abstraction, not concrete hardware
- **Cross-Platform:** Enables iOS (AVAudioEngine) and ESP32 (I2S) implementations

**Implementations:**
- `CoreAudioHardwareProvider`: macOS CoreAudio implementation (current)
- `AVAudioPlatform`: iOS AVAudioEngine implementation (future)
- `I2SPlatform`: ESP32 I2S driver implementation (future)

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

### ISP Improvements (v3.0)
- **Before:** IAudioMode and IAudioRenderer had overlapping responsibilities
- **After:** Single IAudioStrategy interface provides focused, minimal contract
- **Benefit:** Clients depend only on methods they use

---

## File Organization (Updated for v3.0)

```
src/audio/
├── common/
│   ├── CircularBuffer.cpp/h          # Thread-safe ring buffer
│   └── IAudioSource.h              # Simple audio source interface
├── state/                         # NEW: Focused state components (SRP)
│   ├── StrategyContext.cpp/h         # Composed context for strategies
│   ├── AudioState.cpp/h            # Core playback state
│   ├── BufferState.cpp/h           # Buffer management state
│   └── Diagnostics.cpp/h           # Performance and timing metrics
├── strategies/
│   ├── IAudioStrategy.h            # Unified strategy interface
│   ├── ThreadedStrategy.cpp/h      # Cursor-chasing implementation
│   ├── SyncPullStrategy.cpp/h      # On-demand rendering implementation
│   └── StrategyAdapterFactory.cpp/h # Factory for creating strategies
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
- **state/** folder contains focused, SRP-compliant state components
- **strategies/** folder contains unified IAudioStrategy implementations
- **hardware/** folder contains platform abstraction for cross-platform support
- **adapters/** folder provides legacy compatibility with old IAudioRenderer interface
- **renderers/** folder contains deprecated IAudioRenderer interface (kept for compatibility)

**Cross-Platform Support:**
- IAudioHardwareProvider enables iOS (AVAudioEngine) and ESP32 (I2S) implementations
- Current implementation: CoreAudioHardwareProvider (macOS)
- Future implementations: AVAudioPlatform (iOS), I2SPlatform (ESP32)

---

## SOLID Compliance (Updated for v3.0)

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

### SRP Improvements (v3.0)
- **Before:** AudioUnitContext was a monolithic struct with mixed responsibilities
- **After:** StrategyContext composes focused AudioState, BufferState, Diagnostics structs
- **Benefit:** Each state component has single responsibility, easier to test and maintain

### OCP Improvements (v3.0)
- **Before:** Adding new platforms required modifying AudioPlayer directly
- **After:** IAudioHardwareProvider enables adding platforms without modifying existing code
- **Benefit:** iOS and ESP32 can be added by implementing IAudioHardwareProvider

---

*End of Audio Module Architecture Document*
