# Audio Module Architecture

**Document Version:** 2.0
**Date:** 2026-04-02
**Status:** Current Architecture (Production)
**Author:** Audio Systems Architect

---

## Executive Summary

This document describes the **current production audio architecture** for the macOS CLI. The architecture uses a Strategy pattern for audio modes (threaded vs sync-pull) and rendering strategies, providing clean separation of concerns while preserving all diagnostic features.

### Key Architectural Decisions

| Decision | Rationale |
|----------|-----------|
| **IAudioRenderer strategy** | Abstracts rendering mode (sync-pull vs threaded/cursor-chasing) |
| **IAudioMode strategy** | Abstracts audio mode behavior (buffer management, warmup) |
| **AudioPlayer as orchestrator** | Manages AudioUnit lifecycle, delegates to injected renderer |
| **Platform-specific** | macOS CoreAudio - optimized for current use case |
| **Rich diagnostics** | Stateful AudioUnitContext tracks timing, buffer health, underruns |

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           CLI Application                               │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  CLIMain.cpp - Entry point, DI wiring                          │    │
│  │  Creates: IAudioMode, IInputProvider, IPresentation             │    │
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
│                        Audio Mode Strategy Layer                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  IAudioMode Interface                                           │    │
│  │  - updateSimulation()                                           │    │
│  │  - generateAudio()                                              │    │
│  │  - startAudioThread()                                           │    │
│  │  - prepareBuffer() / resetBufferAfterWarmup()                   │    │
│  │  - createContext() - DI: injects renderer into context          │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────┐  ┌─────────────────────────────┐        │
│  │   ThreadedAudioMode        │  │   SyncPullAudioMode         │        │
│  │   (cursor-chasing)         │  │   (on-demand rendering)     │        │
│  │                            │  │                             │        │
│  │ • Pre-fill buffer          │  │ • No pre-buffer             │        │
│  │ • Main loop generates      │  │ • Audio callback generates  │        │
│  │ • Cursor-chasing logic     │  │ • Low latency               │        │
│  │ • Robust to physics spikes │  │ • Sensitive to timing       │        │
│  │                            │  │                             │        │
│  │ DI: ThreadedRenderer       │  │ DI: SyncPullRenderer        │        │
│  └─────────────────────────────┘  └─────────────────────────────┘        │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        AudioPlayer (Orchestrator)                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  AudioPlayer                                                    │    │
│  │  - initialize(IAudioMode&, ...)                                 │    │
│  │  - start() / stop()                                            │    │
│  │  - setVolume(float)                                            │    │
│  │  - addToCircularBuffer()                                       │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  AudioUnitContext (Stateful, injected to renderer)              │    │
│  │  - engineHandle (bridge simulator)                              │    │
│  │  - circularBuffer (for ThreadedRenderer)                        │    │
│  │  - syncPullAudio (for SyncPullRenderer)                         │    │
│  │  - writePointer, readPointer (cursor tracking)                  │    │
│  │  - underrunCount (diagnostics)                                  │    │
│  │  - lastRenderMs, lastHeadroomMs (timing diagnostics)            │    │
│  │  - lastBudgetPct, lastBufferTrendPct (performance tracking)     │    │
│  │  - audioRenderer (DI: injected renderer strategy)               │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      Renderer Strategy Layer                             │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  IAudioRenderer Interface                                       │    │
│  │  - render(ctx, ioData, numberFrames)                            │    │
│  │  - AddFrames(ctx, buffer, frameCount)                           │    │
│  │  - isEnabled()                                                  │    │
│  │  - getName()                                                    │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │  ThreadedRenderer   │  │  SyncPullRenderer   │  │  SilentRenderer │  │
│  │  (cursor-chasing)   │  │  (on-demand)        │  │  (silence)      │  │
│  │                     │  │                     │  │                 │  │
│  │ Reads from          │  │ Calls RenderOnDemand│  │ Zeros buffer    │  │
│  │ circularBuffer      │  │ in audio callback   │  │                 │  │
│  │ Tracks cursors      │  │ Measures timing     │  │ Utility class   │  │
│  │ Detects underruns   │  │ Reports budget      │  │                 │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
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
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Component Details

### IAudioMode Strategy

**Purpose:** Abstracts audio mode behavior (buffer management, threading, warmup)

**Implementations:**
- `ThreadedAudioMode`: Cursor-chasing mode with pre-filled buffer
- `SyncPullAudioMode`: On-demand rendering in audio callback

**Key Methods:**
```cpp
class IAudioMode {
    virtual std::string getModeName() const = 0;
    virtual std::string getModeString() const = 0;
    virtual void updateSimulation(EngineSimHandle, const EngineSimAPI&, AudioPlayer*) = 0;
    virtual void generateAudio(IAudioSource&, AudioPlayer*) = 0;
    virtual bool startAudioThread(EngineSimHandle, const EngineSimAPI&, AudioPlayer*) = 0;
    virtual void prepareBuffer(AudioPlayer*) = 0;
    virtual void resetBufferAfterWarmup(AudioPlayer*) = 0;
    virtual void startPlayback(AudioPlayer*) = 0;
    virtual bool shouldDrainDuringWarmup() const = 0;

    // DI: Creates context with renderer pre-injected
    virtual std::unique_ptr<AudioUnitContext> createContext(
        int sampleRate, EngineSimHandle, const EngineSimAPI*
    ) = 0;
};
```

### IAudioRenderer Strategy

**Purpose:** Abstracts audio rendering strategy (how audio gets to CoreAudio callback)

**Implementations:**
- `ThreadedRenderer`: Reads from cursor-chasing circular buffer
- `SyncPullRenderer`: Calls RenderOnDemand in audio callback
- `SilentRenderer`: Outputs silence (utility)

**Key Methods:**
```cpp
class IAudioRenderer {
    virtual bool render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) = 0;
    virtual bool AddFrames(void* ctx, float* buffer, int frameCount) = 0;
    virtual bool isEnabled() const = 0;
    virtual const char* getName() const = 0;
};
```

### AudioUnitContext (State Container)

**Purpose:** Holds all audio state, passed to renderer via `void* ctx`

**Key Members:**
```cpp
struct AudioUnitContext {
    // Engine
    EngineSimHandle engineHandle;

    // Strategy
    IAudioRenderer* audioRenderer;

    // Threaded mode
    std::unique_ptr<CircularBuffer> circularBuffer;
    std::atomic<int> writePointer;
    std::atomic<int> readPointer;

    // Sync-pull mode
    std::unique_ptr<SyncPullAudio> syncPullAudio;

    // Diagnostics (shared)
    std::atomic<int> underrunCount;
    std::atomic<double> lastRenderMs;
    std::atomic<double> lastHeadroomMs;
    std::atomic<double> lastBudgetPct;
    std::atomic<double> lastBufferTrendPct;

    // Lifecycle
    std::atomic<bool> isPlaying;
    int sampleRate;
    float volume;
};
```

---

## Mode Behavior Comparison

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
| **SRP** | Each class has single responsibility: AudioPlayer (lifecycle), IAudioMode (mode behavior), IAudioRenderer (rendering strategy) |
| **OCP** | Strategy pattern allows adding new modes/renderers without modifying existing code |
| **LSP** | All implementations honor their interface contracts |
| **ISP** | Interfaces are focused and minimal |
| **DIP** | High-level modules (AudioPlayer) depend on abstractions (IAudioRenderer, IAudioMode) |
| **DI** | Dependencies injected via createContext(), constructor injection |

---

## File Organization

```
src/audio/
├── common/
│   ├── CircularBuffer.cpp/h          # Thread-safe ring buffer
│   ├── BridgeAudioSource.cpp/h       # Bridge API wrapper (for IAudioSource interface - unused)
│   └── SyncPullAudio.cpp/h           # Sync-pull state and diagnostics
├── modes/
│   ├── IAudioMode.h                  # Audio mode strategy interface
│   ├── ThreadedAudioMode.cpp/h       # Threaded mode implementation
│   ├── SyncPullAudioMode.cpp/h       # Sync-pull mode implementation
│   └── AudioModeFactory.cpp/h        # Factory for creating modes
├── renderers/
│   ├── IAudioRenderer.h              # Renderer strategy interface
│   ├── ThreadedRenderer.cpp/h        # Threaded renderer (cursor-chasing)
│   ├── SyncPullRenderer.cpp/h        # Sync-pull renderer (on-demand)
│   └── SilentRenderer.cpp/h          # Silence renderer (utility)
└── platform/
    ├── IAudioPlatform.h              # [UNUSED] Abandoned platform abstraction
    └── macos/
        └── CoreAudioPlatform.cpp/h   # [UNUSED] Unimplemented macOS platform
```

**Note:** The `platform/` folder contains abandoned IAudioPlatform code. See ARCHITECTURE_TODO.md for rationale.

---

## Historical Note: IAudioPlatform (Abandoned)

An earlier attempt was made to create a platform-agnostic audio abstraction (`IAudioPlatform` interface with `CoreAudioPlatform`, `AVAudioPlatform`, `I2SPlatform` implementations). This was **abandoned** because:

1. **Functionality Loss**: The simple `IAudioSource::generateAudio()` interface could not support the rich diagnostics (sync-pull timing, cursor-chasing state, buffer health tracking)

2. **Complexity vs Benefit**: The current IAudioRenderer strategy provides clean separation with all features preserved. Platform abstraction would require significant work for minimal benefit in the current macOS-only use case.

3. **Working Architecture**: The current architecture is production-tested, SOLID-compliant, and maintainable.

See `docs/ARCHITECTURE_TODO.md` section "Audio Architecture Analysis (2026-04-02)" for detailed analysis.

---

*End of Audio Module Architecture Document*
