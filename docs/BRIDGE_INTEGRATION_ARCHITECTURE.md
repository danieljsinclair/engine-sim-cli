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

### 9.2 SineGenerator Consolidation: COMPLETED

> **Status: RESOLVED** - Current architecture properly uses `SineWaveSimulator` which derives from `Simulator` and can be DI'd in place of `PistonEngineSimulator`.

#### Original Problem (Historical)

There were previously two independent SineGenerator implementations, but this has been resolved:

| Aspect | Previous State | Current State |
|--------|----------------|---------------|
| **CLI --sine mode** | `src/SineGenerator.cpp` standalone | Uses `SineWaveSimulator` via bridge |
| **Mock bridge** | Internal class in `mock_engine_sim.cpp` | Uses `SineWaveSimulator` via DI |
| **Threading** | Inconsistent | Consistent across both modes |
| **Testability** | Limited | Full DI support for testing |

#### Current Architecture (Correct)

The current implementation properly uses `SineWaveSimulator`:
- Derives from `Simulator` base class
- Can be injected in place of `PistonEngineSimulator`
- Generates deterministic outputs for diagnostics and testing
- No duplicate implementations
- Bridge handles both real engine and sine wave scenarios uniformly

**No further consolidation work needed** - the architecture is correct.

---

### 9.3 IEngineProvider Implementation: YAGNI

> **Status: NOT NEEDED** - Current static linking architecture with C++ wrapper is correct.

#### Original Plan (Historical)

The original plan was to create an `IEngineProvider` interface to abstract engine-sim, but this is unnecessary complexity.

#### Current Architecture (Correct)

The current bridge implementation is correct:
- Static linking with engine-sim source
- Clean C API wrapper (`EngineSimCreate`, `EngineSimRender`, etc.)
- Internal delegation to engine-sim C++ classes
- Compiled into `libenginesim.dylib`
- CLI loads bridge, NOT engine-sim directly

**This is the correct wrapper pattern** - static linking with proper abstraction.

#### Why IEngineProvider is YAGNI

- Current architecture already provides proper abstraction via bridge C API
- Static linking eliminates runtime complexity
- Compile-time type safety is already achieved
- DI is achieved through `SineWaveSimulator` vs `PistonEngineSimulator` selection
- Adding another interface layer would be unnecessary complexity

**No IEngineProvider implementation needed** - current architecture is correct.

---

### 9.4 Implementation Plan: COMPLETED

| Phase | Task | Status | Notes |
|-------|------|--------|-------|
| 1 | **Consolidate SineGenerator** | ✅ DONE | Uses `SineWaveSimulator` via DI |
| 2 | Create IEngineProvider interface | ✅ N/A | YAGNI - current C API is correct |
| 3 | Create RealEngineProvider | ✅ N/A | Bridge wrapper is correct |
| 4 | Create MockEngineProvider | ✅ DONE | `SineWaveSimulator` via DI |
| 5 | Refactor CLI to use IEngineProvider via DI | ✅ N/A | Current architecture correct |
| 6 | Static linking | ✅ DONE | Bridge statically linked |
| 7 | Path consolidation to bridge | ✅ DONE | CLI is thin shell, bridge owns path logic |

#### Completed Work (2026-04-01)

**Path Resolution (Phase 5):**
- CLI passes raw paths to bridge (thin shell pattern)
- Bridge handles all path normalization and resolution
- Comprehensive test coverage added
- SRP and DRY compliance improved

**Logger DI (Phase 2):**
- Logger injected via `SimulationConfig`
- No static storage limitations
- Thread-safe by design

**Dead Code Removal (Phase 1):**
- Unused path resolution methods removed
- YAGNI compliance improved

---

### 9.5 Current Architecture Summary

The engine-sim bridge integration is now complete with proper SOLID compliance:

- **Static linking**: Bridge statically linked with engine-sim, no runtime loading
- **Proper abstraction**: Clean C API wrapper around engine-sim C++ classes
- **DI support**: `SineWaveSimulator` can be injected in place of `PistonEngineSimulator`
- **Thin shell CLI**: CLI parses args and passes raw input to bridge
- **Bridge ownership**: Bridge handles all path resolution, normalization, and validation
- **Test coverage**: Comprehensive path resolution tests ensure regression protection
- **No YAGNI violations**: Dead code removed, unnecessary complexity avoided

**Status**: Architecture is complete and correct. No further refactoring needed.

---

## Appendix: File Locations

| File | Path |
|------|------|
| IInputProvider | `src/interfaces/IInputProvider.h` |
| IPresentation | `src/interfaces/IPresentation.h` |
| KeyboardInputProvider | `src/interfaces/KeyboardInputProvider.cpp` |
| ConsolePresentation | `src/interfaces/ConsolePresentation.cpp` |
| SimulationLoop | `src/SimulationLoop.cpp` |
| CLIMain | `src/CLIMain.cpp` |
| Bridge C API | `engine-sim-bridge/include/engine_sim_bridge.h` |
| Path Resolution Tests | `test/smoke/test_path_resolution.cpp` |

---

*End of Architecture Document*
