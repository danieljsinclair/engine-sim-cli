# Engine-Sim CLI Bridge Integration Architecture

**Document Version:** 2.0  
**Date:** 2026-03-19  
**Status:** Architecture Decision Record (ADR)

---

## 1. Executive Summary

This document defines the architecture for integrating the engine-sim CLI with external controllers and multiple platform targets. The architecture emphasizes injectable interfaces for input and presentation, enabling flexibility across different use cases.

### System Context

- **engine-sim**: 3rd party engine simulation library (minimally modified to enable bridge construction)
- **engine-sim-bridge**: Our API wrapper around engine-sim
- **CLI**: Thin shell using the bridge API - currently proof-of-concept with keyboard input and console output
- **Future targets**: iOS app, ESP32, TMUX TUI, headless mode

### Key Architectural Decisions

| Decision | Recommendation |
|----------|----------------|
| **Input Abstraction** | IInputProvider - abstracts input source (keyboard, upstream data) |
| **Output Abstraction** | IPresentation - abstracts output (console, TUI, headless) |
| **Upstream Model** | ODB2 and VirtualICE are upstream - we receive data via IInputProvider, don't distinguish |
| **Platform Targets** | macOS CLI → iOS, ESP32, TUI |
| **Backward Compatibility** | CLI defaults to keyboard input when no external controller connected |

---

## 2. High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Platform Targets                                │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐           │
│  │ macOS    │   │   iOS    │   │  ESP32   │   │ TMUX TUI  │           │
│  │   CLI    │   │   App    │   │ Firmware │   │  (next)   │           │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └────┬─────┘           │
│       │              │              │              │                   │
└───────┼──────────────┼──────────────┼──────────────┼───────────────────┘
        │              │              │              │
        ▼              ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                           Engine-Sim Bridge                              │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  runUnifiedAudioLoop()                                         │    │
│  │  - 60Hz simulation tick                                        │    │
│  │  - IAudioMode strategy (sync-pull / threaded)                 │    │
│  │  - IAudioRenderer strategy                                     │    │
│  └────────────────────────┬────────────────────────────────────────┘    │
│                           │                                              │
│  ┌────────────────────────▼────────────────────────────────────────┐    │
│  │  EngineSimAPI (engine-sim-bridge)                              │    │
│  │  - SetThrottle(), SetIgnition(), SetStarterMotor()            │    │
│  │  - GetStats(), Render(), Update()                              │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
        │                                              │
        │ Injected Dependencies                        │
        ▼                                              ▼
┌──────────────────────┐              ┌──────────────────────────────────┐
│   IInputProvider     │              │       IPresentation             │
│  ┌────────────────┐  │              │  ┌────────────────────────┐    │
│  │ KeyboardInput  │  │              │  │   ConsolePresentation  │    │
│  │ Provider       │  │              │  │   (current)           │    │
│  └────────────────┘  │              │  └────────────────────────┘    │
│  ┌────────────────┐  │              │  ┌────────────────────────┐    │
│  │ Upstream       │  │              │  │   TUIPresentation      │    │
│  │ Provider       │◄─┼──────────────┼──│   (TMUX-based)         │    │
│  │ (ODB2/Virtual │  │              │  └────────────────────────┘    │
│  │  ICE abstracted│  │              │  ┌────────────────────────┐    │
│  └────────────────┘  │              │  │   HeadlessLogger      │    │
└──────────────────────┘              │  │   (logging only)      │    │
                                      │  └────────────────────────┘    │
                                      └──────────────────────────────────┘

                         ▲
                         │
                         │ Input from upstream
                         │ (ODB2 or VirtualICE)
                         │
┌────────────────────────┴────────────────────────────────────────────────┐
│                         Upstream Systems                                  │
│  ┌──────────────────────┐   ┌────────────────────────────────────────┐  │
│  │   ODB2 Adapter       │   │        VirtualICE Twin                │  │
│  │   (real car)         │   │   (simulates petrol car behavior)     │  │
│  │   RPM, throttle,     │   │   receives: throttle, gear, clutch   │  │
│  │   load, etc.         │   │   outputs: RPM, torque, etc.          │  │
│  └──────────────────────┘   └────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## 3. Interface Definitions

### 3.1 IInputProvider (See: `src/interfaces/IInputProvider.h`)

```cpp
namespace input {

enum class InputProviderType {
    Keyboard,   // Interactive keyboard input (default CLI mode)
    Upstream,   // Data from upstream (ODB2, virtual sim - abstracted)
    Mock        // Test/mock data provider
};

struct EngineInput {
    double throttle;        // 0.0 - 1.0
    bool ignition;         // true = on
    bool starterMotor;     // true = engaged
    int gear;              // -1=reverse, 0=neutral, 1+=forward
    double clutchPressure; // 0.0 - 1.0
};

class IInputProvider {
public:
    virtual ~IInputProvider() = default;
    
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsConnected() const = 0;
    
    virtual EngineInput GetEngineInput() const = 0;
    virtual double GetThrottle() const = 0;
    virtual bool GetIgnition() const = 0;
    virtual bool GetStarterMotor() const = 0;
    
    virtual void SetIgnition(bool enabled) = 0;
    virtual void SetStarterMotor(bool enabled) = 0;
    
    virtual void Update(double dt) = 0;
    virtual std::string GetProviderName() const = 0;
};

} // namespace input
```

### 3.2 IPresentation (See: `src/interfaces/IPresentation.h`)

```cpp
namespace presentation {

struct EngineState {
    double timestamp;
    double rpm;
    double throttle;
    double load;
    double speed;
    int underrunCount;
    std::string audioMode;
    bool ignition;
    bool starterMotor;
};

enum class PresentationType {
    Console,    // Text output to console (default)
    TUI,        // TMUX-based character UI
    Headless,   // No output, just logging
    GUI         // Future: smartphone app GUI
};

class IPresentation {
public:
    virtual ~IPresentation() = default;
    
    virtual bool Initialize(const PresentationConfig& config) = 0;
    virtual void Shutdown() = 0;
    
    virtual void ShowEngineState(const EngineState& state) = 0;
    virtual void ShowMessage(const std::string& message) = 0;
    virtual void ShowError(const std::string& error) = 0;
    
    virtual bool ShouldContinue() const = 0;
    virtual void Update(double dt) = 0;
};

} // namespace presentation
```

### 3.3 ILogging (See: `engine-sim-bridge/include/ILogging.h`)

**Policy: Neither the bridge nor the CLI shall write directly to `cout` or `cerr`. All diagnostic output goes through `ILogging`.**

- The bridge receives an `ILogging*` via `EngineSimSetLogging(handle, logger)` at startup.
- The bridge must call `logger->info(...)` / `logger->debug(...)` etc. and never `std::cout` or `std::cerr` directly.
- The CLI must inject an `ILogging` implementation and route its own diagnostic output through the same interface — not `cout`/`cerr`. `StdErrLogging` (the default `ILogging` implementation) writes to `stderr`, keeping diagnostic output separate from any data piped on `stdout`.
- `IPresentation` is for **engine state output** (RPM, throttle, audio mode). `ILogging` is for **diagnostic and operational messages** (errors, warnings, init steps, debug traces). They are not interchangeable.

**Design intent — functional-area selectors:**

The current `LogLevel` enum (Debug / Info / Warning / Error) is a baseline. The intended direction is bitwise functional-area flags so callers can enable exactly the subsystems they want to trace without drowning in noise:

```cpp
// Intended (not yet implemented — requires ILogging redesign):
enum LogArea : uint32_t {
    AUDIO   = 1 << 0,
    PHYSICS = 1 << 1,
    BUFFER  = 1 << 2,
    CONFIG  = 1 << 3,
    BRIDGE  = 1 << 4,
};
// logger->log(PHYSICS | BUFFER, "underrun count=%d", n);
```

Until that redesign lands, use `LogLevel::Debug` for per-frame traces and `LogLevel::Info` for init/shutdown messages.

**Current implementation status:**
- [x] `ILogging` interface + `StdErrLogging` default implementation
- [x] `EngineSimSetLogging()` C API for bridge injection
- [ ] Bridge internals still use `cout`/`cerr` in places — must be migrated
- [ ] CLI still uses `cout`/`cerr` in places — must be migrated
- [ ] Bitwise area-selector redesign (future)

---

## 4. Current Implementation Status

### Completed ✅
- [x] IInputProvider interface (`src/interfaces/IInputProvider.h`)
- [x] IPresentation interface (`src/interfaces/IPresentation.h`)
- [x] KeyboardInputProvider (`src/interfaces/KeyboardInputProvider.cpp`)
- [x] ConsolePresentation (`src/interfaces/ConsolePresentation.cpp`)
- [x] SimulationLoop refactored to use injectable interfaces

### TODO
- [ ] TUIPresentation (TMUX-based character UI)
- [ ] HeadlessLogger (logging only)
- [ ] UpstreamInputProvider (receives data from upstream - ODB2 or VirtualICE)

---

## 5. Platform Targets

| Platform | Input | Output | Status |
|----------|-------|--------|--------|
| **macOS CLI** | Keyboard | Console | ✅ Working |
| **iOS App** | Touch/GUI | Audio + GUI | Future |
| **ESP32** | CAN bus | Audio output | Future |
| **TMUX TUI** | Keyboard | Terminal UI | Next |
| **Headless** | Config/API | Log file | Future |

---

## 6. Upstream Integration

The system receives input from upstream systems:

### ODB2 Adapter
- Reads real vehicle data: RPM, throttle position, load, speed
- Polls at ODB2 rate (typically 10Hz)
- Data is interpolated to 60Hz for simulation

### VirtualICE Twin
- Simulates what a petrol car would be doing
- Inputs: throttle position, gear, clutch, speed, acceleration
- Outputs: simulated RPM, torque, engine sounds
- Uses engine-sim's dyno mode for realistic behavior

**Key Point**: We don't distinguish between ODB2 and VirtualICE - they're both "upstream" data providers. The VirtualICE twin hides the implementation detail from us.

---

## 7. Migration Plan

### Phase 1: Interfaces (COMPLETED ✅)
- [x] Create IInputProvider interface
- [x] Create IPresentation interface
- [x] Implement KeyboardInputProvider
- [x] Implement ConsolePresentation
- [x] Refactor SimulationLoop to use interfaces

### Phase 2: TMUX TUI (Next)
- [ ] Implement TUIPresentation
- [ ] Add --tui command line flag
- [ ] Test terminal UI rendering

### Phase 3: Upstream Provider
- [ ] Implement UpstreamInputProvider
- [ ] Add configuration for upstream connection
- [ ] Test with mock upstream data

### Phase 4: Platform Ports
- [ ] iOS: Create iOS project, integrate bridge
- [ ] ESP32: Port audio output, reduce features

---

## 8. Testing Strategy

### Without Real Hardware
- MockDataProvider - synthetic RPM/throttle/load
- MockInputProvider - test different input scenarios

### Integration Testing
- Bridge + mock engine + mock input provider
- Verify audio pipeline works

---

## 9. Engine Provider Architecture (Future)

> **Note**: This is a **REFACTORING** (internal architecture change), NOT a feature change. The external CLI behavior remains identical.

### 9.1 Bridge Architecture: Static Linking with C++ Wrappers

#### Architecture Decision: Static Linking Over Dynamic DLL Loading

**DECISION**: The bridge should be statically linked to the CLI and expose C++ interfaces (not fragile DLL entry points).

**Rationale**:
1. **Compile-time type safety** - Linker catches all errors at build time
2. **No runtime path resolution** - Eliminates DYLD_LIBRARY_PATH issues
3. **SOLID principles** - C++ interfaces enable proper dependency injection
4. **Simpler deployment** - Single binary, no library dependencies
5. **Better testing** - Easy to swap implementations via DI

#### Current Bridge Implementation (Correct)

The bridge (`engine_sim_bridge.cpp`) **properly wraps** engine-sim:
- Presents clean C API (`EngineSimCreate`, `EngineSimRender`, etc.)
- Internally delegates to engine-sim C++ classes (`PistonEngineSimulator`, `Engine`, etc.)
- Compiled statically with engine-sim source into `libenginesim.dylib`
- escli loads the bridge, NOT engine-sim directly

**This is the correct wrapper pattern** - static linking with proper abstraction.

#### Transition Plan: Direct C++ Interface

Reference implementation exists in `/Users/danielsinclair/vscode/escli/escli` (uncommitted):

```cpp
// engine_sim_loader.h (static linking version)
struct EngineSimAPI {
    EngineSimResult Create(const EngineSimConfig* config, EngineSimHandle* outHandle) const {
        return EngineSimCreate(config, outHandle);
    }
    // ... direct function calls, no function pointers
};
```

This eliminates:
- `dlopen()` / `dlsym()` complexity
- Function pointer indirection
- Runtime symbol resolution failures

```cpp
// src/interfaces/IEngineProvider.h (planned)
class IEngineProvider {
public:
    virtual ~IEngineProvider() = default;
    virtual void Initialize(const EngineConfig& config) = 0;
    virtual void SetThrottle(double position) = 0;
    virtual void SetIgnition(bool enabled) = 0;
    virtual void Update(double deltaTime) = 0;
    virtual void Render(float* buffer, int frames) = 0;
    virtual EngineStats GetStats() const = 0;
    // ... etc
};

// Implementations:
class RealEngineProvider : public IEngineProvider { /* wraps engine-sim */ };
class MockEngineProvider : public IEngineProvider { /* sine wave generation */ };
```

Benefits:
- **Compile-time type safety** - All errors caught at compile time
- **Easy testing** - Swap providers in tests via DI
- **Single source of truth** - One implementation path
- **No runtime loading** - Simpler deployment

---

### 9.2 SineGenerator Consolidation: PRIMARY GOAL

> **This is the PRIMARY refactoring goal.** Before implementing IEngineProvider, we must consolidate the two existing SineGenerator implementations.

#### The Problem: Two Separate Implementations

There are currently **TWO** independent SineGenerator implementations:

| Aspect | `src/SineGenerator.cpp` | `mock_engine_sim.cpp` Internal Class |
|--------|-------------------------|--------------------------------------|
| **Location** | `src/SineGenerator.cpp` | `engine-sim-bridge/src/mock_engine_sim.cpp` |
| **Purpose** | CLI `--sine` mode standalone | Used by mock bridge internally |
| **Threading** | None - synchronous only | Full cursor-chasing, std::thread |
| **Ring Buffer** | None | `MockRingBuffer<T>` (matches engine-sim) |
| **Audio Thread** | No | Yes - exact replica of `Synthesizer::audioRenderingThread()` |
| **Phase Continuity** | Basic phase tracking | Advanced phase in simulation context |
| **Stereo Output** | Yes (float*) | Yes (float* and int16_t*) |
| **RPM Linking** | Yes (`generateRPMLinked`) | Yes (via `writeToSynthesizer`) |
| **Controls** | Frequency, amplitude, phase | Frequency, enabled state |

#### Why mock_engine_sim.cpp is a Hack

The current `mock_engine_sim.cpp` contains:
- **~1400 lines** of complex mock code
- An internal `SineGenerator` class that should be shared
- A `MockRingBuffer` that duplicates engine-sim's `RingBuffer<T>`
- A `MockSynthesizer` that replicates engine-sim's threading model

This is a **temporary hack** to allow development without the real engine-sim library. Once SineGenerator is consolidated, this complexity should be dramatically reduced.

#### Consolidation Approach

We need **ONE** implementation that:
1. Does cursor-chasing/threading like real engine-sim
2. Best mimics real engine behavior
3. Can be used by both CLI `--sine` mode and mock bridge

The choice should be based on which implementation **best mimics real engine behavior**:
- **Option A**: Extend `src/SineGenerator.cpp` with threading support (less invasive)
- **Option B**: Move internal class from mock_engine_sim.cpp to shared location (more accurate)

Recommendation: **Option A** - Extend the simpler `src/SineGenerator.cpp` to support threading, since it has the cleaner API.

---

### 9.3 Implementation Plan

| Phase | Task | Notes |
|-------|------|-------|
| 1 | **Consolidate SineGenerator** | PRIMARY GOAL - merge two implementations |
| 2 | Create IEngineProvider interface | `src/interfaces/IEngineProvider.h` |
| 3 | Create RealEngineProvider | Wraps real engine-sim (static link) |
| 4 | Create MockEngineProvider | Uses consolidated SineGenerator |
| 5 | Refactor CLI to use IEngineProvider via DI | Remove dlopen |
| 6 | Update CMakeLists.txt | Remove mock dylib build |
| 7 | Delete/preserve mock_engine_sim.cpp | Archive or remove hack |

---

### 9.4 Risks and Mitigations

| Risk | Mitigation |
|------|------------|
| Regression in audio behavior | Test both --sine mode and mock bridge after consolidation |
| Threading complexity | Start with synchronous mode, add threading incrementally |
| Breaking existing CLI users | Keep `--sine` flag working identically |

---

## Appendix: File Locations

| File | Path |
|------|------|
| IInputProvider | `src/interfaces/IInputProvider.h` |
| IPresentation | `src/interfaces/IPresentation.h` |
| IEngineProvider | `src/interfaces/IEngineProvider.h` (future) |
| KeyboardInputProvider | `src/interfaces/KeyboardInputProvider.cpp` |
| ConsolePresentation | `src/interfaces/ConsolePresentation.cpp` |
| SineGenerator | `src/SineGenerator.cpp` |
| SimulationLoop | `src/SimulationLoop.cpp` |
| CLIMain | `src/CLIMain.cpp` |

---

*End of Architecture Document*
