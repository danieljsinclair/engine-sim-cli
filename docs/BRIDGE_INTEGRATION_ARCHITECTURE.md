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

## Appendix: File Locations

| File | Path |
|------|------|
| IInputProvider | `src/interfaces/IInputProvider.h` |
| IPresentation | `src/interfaces/IPresentation.h` |
| KeyboardInputProvider | `src/interfaces/KeyboardInputProvider.cpp` |
| ConsolePresentation | `src/interfaces/ConsolePresentation.cpp` |
| SimulationLoop | `src/SimulationLoop.cpp` |
| CLIMain | `src/CLIMain.cpp` |

---

*End of Architecture Document*
