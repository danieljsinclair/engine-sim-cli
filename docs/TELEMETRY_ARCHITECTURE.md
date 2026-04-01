# Telemetry Architecture (Reader/Writer Split)

**Document Version:** 2.1
**Date:** 2026-04-01
**Status:** Architecture Decision Record (ADR)
**Author:** Solution Architect

---

## Executive Summary

This document defines the telemetry architecture for the engine-sim CLI using a Reader/Writer interface split for proper ISP compliance. Telemetry is distinct from logging - logging captures operational messages, while telemetry captures structured data for presentation and analysis.

**IMPORTANT:** This architecture is **pure C++** from the bridge upward. The C API is only at the engine-sim boundary (which we don't control). All target platforms (macOS, iOS, ESP32) support C++ natively, so we use C++ interfaces throughout.

### Key Architectural Decisions

| Decision | Rationale |
|----------|-----------|
| **Pure C++ architecture** | All target platforms support C++ - no C API needed for telemetry |
| **Reader/Writer interface split** | ISP compliance - bridge only writes, presentation only reads |
| **ITelemetryWriter interface** | Bridge writes telemetry (memory, file, network) |
| **ITelemetryReader interface** | Presentation reads telemetry for display |
| **Distinct from ILogging** | Logging = messages, telemetry = structured data |
| **Default: In-memory atomic struct** | Thread-safe, zero-copy, suitable for real-time display |
| **Transition strategy** | Bridge pushes to both ILogging (immediate) and ITelemetryWriter (structured) |

### Reader/Writer Split (ISP Compliance)

**Interface Segregation Principle (ISP):** Clients should not depend on interfaces they don't use.

**Problem with single ITelemetryProvider:**
- Bridge only needs to WRITE telemetry
- Presentation only needs to READ telemetry
- File/Network telemetry don't need readback (LSP violation to implement getSnapshot())

**Solution:** Split into two focused interfaces:
- `ITelemetryWriter` - for bridge (write operations only)
- `ITelemetryReader` - for presentation (read operations only)

---

## Pure C++ Architecture (No C API Wrapper)

### Architecture Decision: Pure C++ from Bridge Upward

**Decision:** The telemetry architecture uses **pure C++ interfaces** with no C API wrapper functions.

**Rationale:**

| Aspect | Decision | Why |
|--------|----------|-----|
| **Interface Language** | C++ | All target platforms support C++ natively |
| **Bridge Integration** | C++ class accepts C++ interface | EngineConfig is C++, no wrapper needed |
| **CLI Integration** | C++ code uses C++ interfaces directly | No C API indirection overhead |
| **C API Boundary** | Only at engine-sim level | We don't control engine-sim, its C API is not our concern |

### Platform Support

All target platforms support C++ natively:

| Platform | C++ Support | Notes |
|----------|-------------|-------|
| **macOS** | ✅ Full C++17+ | CoreAudio C++ APIs available |
| **iOS** | ✅ Full C++17+ | AVAudioEngine C++ APIs available |
| **ESP32** | ✅ Full C++17+ | ESP-IDF supports C++ natively |

### Bridge Integration Example

**Pure C++ (No C API):**

```cpp
// EngineConfig.h (C++ class)
class EngineConfig {
public:
    // Accept C++ interface directly - no C API wrapper
    void setTelemetryWriter(ITelemetryWriter* writer) {
        telemetryWriter_ = writer;
    }

    // Write telemetry during simulation
    void writeTelemetry(const TelemetryData& data) {
        if (telemetryWriter_) {
            telemetryWriter_->write(data);
        }
    }

private:
    ITelemetryWriter* telemetryWriter_ = nullptr;
};
```

**CLI Wiring (Pure C++):**

```cpp
// CLIMain.cpp
int main(int argc, char** argv) {
    // Create telemetry storage (C++ class)
    auto telemetry = std::make_unique<InMemoryTelemetry>();

    // Get interfaces (C++ pointers)
    ITelemetryWriter* writer = telemetry.get();
    ITelemetryReader* reader = telemetry.get();

    // Configure engine (C++ class)
    EngineConfig config;
    config.setTelemetryWriter(writer);  // Direct C++ call

    // Configure presentation (C++ interface)
    presentation->setTelemetryReader(reader);  // Direct C++ call

    // Run simulation
    runSimulation(config);
}
```

### What We DON'T Need

**NO C API wrapper functions like:**

```cpp
// ❌ NOT NEEDED - Don't create these
extern "C" {
    EngineSimResult EngineSimSetTelemetryWriter(EngineSimHandle, ITelemetryWriter*);
    EngineSimResult EngineSimSetTelemetryReader(EngineSimHandle, ITelemetryReader*);
}
```

**Why these aren't needed:**
1. EngineConfig is already C++ - can accept C++ interfaces directly
2. No platform constraints - all targets support C++
3. No external consumers - telemetry is internal to our architecture
4. C API boundary is at engine-sim level (not our concern)

### Benefits of Pure C++ Architecture

1. **Type Safety:** Compile-time type checking for all interfaces
2. **No Wrapper Overhead:** Direct function calls, no indirection
3. **Simpler Code:** No C API boilerplate, no `extern "C"` wrappers
4. **Better Tooling:** C++ IDE support, refactoring tools work better
5. **Future-Proof:** Easy to extend with C++ features (templates, smart pointers, etc.)

---

## Motivation: Why Reader/Writer Split?

### Problem 1: Mixing Telemetry with Logging

Current architecture mixes operational logging with telemetry data:

```cpp
// Current: ConsolePresentation uses ILogging for everything
logger->info("RPM: %f, throttle: %f", rpm, throttle);  // Telemetry disguised as logging
```

**Problems:**
1. Telemetry data (RPM, load, exhaust flow) mixed with operational messages
2. Parsing structured data from log strings is fragile
3. Future TMUX TUI needs structured data, not text streams
4. Can't suppress logging without losing telemetry

### Problem 2: ISP Violation with Single Interface

```cpp
// Single interface - ISP violation
class ITelemetryProvider {
    virtual void write(const TelemetryData& data) = 0;  // Bridge needs this
    virtual TelemetryData getSnapshot() const = 0;      // Presentation needs this
};
```

**Problems:**
1. Bridge depends on getSnapshot() it never uses (ISP violation)
2. Presentation depends on write() it never uses (ISP violation)
3. File/Network implementations must fake getSnapshot() (LSP violation)
4. Future network telemetry can't read back from network

### Solution: Reader/Writer Interface Split

```cpp
// Writer interface - bridge uses this
class ITelemetryWriter {
    virtual void write(const TelemetryData& data) = 0;
    virtual void reset() = 0;
};

// Reader interface - presentation uses this
class ITelemetryReader {
    virtual TelemetryData getSnapshot() const = 0;
};
```

**Benefits:**
1. **ISP Compliance:** Each client depends only on methods it uses
2. **LSP Compliance:** File/Network telemetry implement only Writer (no fake methods)
3. **Clear Data Flow:** Bridge → Writer → Storage → Reader → Presentation
4. **Future-Proof:** Network telemetry doesn't need fake readback

---

## Architecture Overview

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           CLI (Ultra-Thin)                              │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  CLIMain.cpp - Wires providers together                          │    │
│  │  - Creates ILogging (StdErrLogging by default)                   │    │
│  │  - Creates ITelemetryWriter/Reader (InMemoryTelemetry)           │    │
│  │  - Creates IPresentation (ConsolePresentation by default)        │    │
│  │  - Creates IInputProvider (KeyboardInputProvider by default)     │    │
│  │  - Passes Writer to bridge, Reader to presentation              │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                    ┌───────────────┴───────────────┐
                    │ Injects dependencies          │
                    ▼                               ▼
┌───────────────────────────────┐    ┌──────────────────────────────────┐
│      Bridge (runSimulation)   │    │     Presentation Layer            │
│                               │    │                                  │
│  ┌─────────────────────────┐  │    │  ┌────────────────────────────┐ │
│  │ - Runs physics (Update) │  │    │  │ - ITelemetryReader* input  │ │
│  │ - Generates audio       │  │    │  │ - Reads snapshot for display│ │
│  │ - Produces telemetry    │  │    │  └────────────┬───────────────┘ │
│  └──────────┬──────────────┘  │    │               │                   │
│             │                 │    └───────────────┼───────────────────┘
│             │ Uses            │                    │ Reads from         │
│             ▼                 │                    ▼                   │
│  ┌─────────────────────┐     │    ┌──────────────────────────────────┐
│  │ ITelemetryWriter*   │     │    │   ITelemetryReader*              │
│  │  - Writes telemetry  │     │    │  - getSnapshot()                │
│  │  - reset()           │     │    │  - Zero-copy atomic access      │
│  └──────────┬──────────┘     │    └────────────┬─────────────────────┘
│             │                │                 │
│             │ write()        │                 │
│             ▼                │                 │
│  ┌─────────────────────────────────────────────────────────────────┐   │
│  │                 Telemetry Storage                               │   │
│  │  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐    │   │
│  │  │ InMemory       │  │ File           │  │ Network        │    │   │
│  │  │ Implements:    │  │ Implements:    │  │ Implements:    │    │   │
│  │  │ Writer + Reader│  │ Writer only    │  │ Writer only    │    │   │
│  │  └────────────────┘  └────────────────┘  └────────────────┘    │   │
│  └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘

Data Flow:
  Bridge ──write()──▶ Telemetry Storage ◀──getSnapshot()──▶ Presentation
```

---

## Interface Definitions: Reader/Writer Split

**Location:** `engine-sim-bridge/include/ITelemetryProvider.h`

```cpp
#ifndef I_TELEMETRY_PROVIDER_H
#define I_TELEMETRY_PROVIDER_H

#include <atomic>
#include <cstdint>

namespace telemetry {

// ============================================================================
// TelemetryData - Structured engine telemetry (non-atomic snapshot)
// ============================================================================

struct TelemetryData {
    // Engine state
    double currentRPM;
    double currentLoad;           // 0.0 - 1.0
    double exhaustFlow;           // m^3/s
    double manifoldPressure;      // Pa
    int32_t activeChannels;

    // Performance metrics
    double processingTimeMs;      // Last frame processing time

    // Audio diagnostics
    int32_t underrunCount;
    double bufferHealthPct;       // 0-100 (buffer fullness)

    // Control inputs (echo back for display)
    double throttlePosition;      // 0.0 - 1.0
    bool ignitionOn;
    bool starterMotorEngaged;

    // Timestamp
    double timestamp;             // Seconds since start
};

// ============================================================================
// ITelemetryWriter - Bridge writes telemetry here
// Used by: Bridge (runSimulation, Update)
// ============================================================================

class ITelemetryWriter {
public:
    virtual ~ITelemetryWriter() = default;

    /**
     * Write telemetry data from bridge.
     * Called from bridge's simulation thread.
     *
     * @param data Telemetry data to write
     *
     * Thread Safety: Implementation must be thread-safe.
     * Bridge calls this from simulation thread, storage handles synchronization.
     */
    virtual void write(const TelemetryData& data) = 0;

    /**
     * Reset telemetry counters (e.g., on simulation restart).
     */
    virtual void reset() = 0;

    /**
     * Get writer name for diagnostics.
     */
    virtual const char* getName() const = 0;
};

// ============================================================================
// ITelemetryReader - Presentation reads telemetry from here
// Used by: IPresentation implementations (Console, TUI, GUI)
// ============================================================================

class ITelemetryReader {
public:
    virtual ~ITelemetryReader() = default;

    /**
     * Get current telemetry snapshot.
     * Called from presentation layer (main thread).
     *
     * @return Copy of current telemetry data
     *
     * Thread Safety: Implementation must be thread-safe.
     * Returns snapshot to avoid race conditions during reads.
     */
    virtual TelemetryData getSnapshot() const = 0;

    /**
     * Get reader name for diagnostics.
     */
    virtual const char* getName() const = 0;
};
};

// ============================================================================
// InMemoryTelemetry - Default implementation (Writer + Reader)
// Zero-copy, thread-safe, suitable for real-time display
// ============================================================================

class InMemoryTelemetry : public ITelemetryWriter, public ITelemetryReader {
public:
    InMemoryTelemetry();
    ~InMemoryTelemetry() override = default;

    // ITelemetryWriter implementation
    void write(const TelemetryData& data) override;
    void reset() override;
    const char* getName() const override { return "InMemoryTelemetry"; }

    // ITelemetryReader implementation
    TelemetryData getSnapshot() const override;

private:
    // Atomic storage for thread-safe write/read
    struct AtomicData {
        std::atomic<double> currentRPM;
        std::atomic<double> currentLoad;
        std::atomic<double> exhaustFlow;
        std::atomic<double> manifoldPressure;
        std::atomic<int32_t> activeChannels;
        std::atomic<double> processingTimeMs;
        std::atomic<int32_t> underrunCount;
        std::atomic<double> bufferHealthPct;
        std::atomic<double> throttlePosition;
        std::atomic<bool> ignitionOn;
        std::atomic<bool> starterMotorEngaged;
        std::atomic<double> timestamp;

        AtomicData();  // Initializes atomics
    };

    AtomicData data_;
};

// ============================================================================
// FileTelemetry - Future implementation (Writer only)
// Logs telemetry to file for post-analysis
// ============================================================================

class FileTelemetry : public ITelemetryWriter {
public:
    explicit FileTelemetry(const char* filePath);
    ~FileTelemetry() override;

    // ITelemetryWriter implementation
    void write(const TelemetryData& data) override;
    void reset() override;
    const char* getName() const override { return "FileTelemetry"; }

private:
    int fileDescriptor_;
    char filePath_[256];
};

// Note: FileTelemetry does NOT implement ITelemetryReader
// Rationale: Reading back from file is not needed for real-time display
// File telemetry is for post-analysis, not live presentation

// ============================================================================
// NetworkTelemetry - Future implementation (Writer only)
// Streams telemetry to network for remote monitoring
// ============================================================================

class NetworkTelemetry : public ITelemetryWriter {
public:
    NetworkTelemetry(const char* host, uint16_t port);
    ~NetworkTelemetry() override;

    // ITelemetryWriter implementation
    void write(const TelemetryData& data) override;
    void reset() override;
    const char* getName() const override { return "NetworkTelemetry"; }

private:
    int socket_;
    char host_[64];
    uint16_t port_;
};

// Note: NetworkTelemetry does NOT implement ITelemetryReader
// Rationale: Remote endpoint has its own reader, we don't read back
// This avoids LSP violation (no fake getSnapshot() implementation)

    void update(const TelemetryData& data) override;
    TelemetryData getSnapshot() const override;
    void reset() override;
    const char* getProviderName() const override { return "NetworkTelemetry"; }

private:
    int socket_;
    char host_[64];
    uint16_t port_;
    TelemetryData lastSnapshot_;  // For getSnapshot()
};

} // namespace telemetry

#endif // I_TELEMETRY_PROVIDER_H
```

---

## Provider Pattern Summary

### All Providers (with Console Defaults)

| Provider | Interfaces | Default Implementation | Purpose |
|----------|------------|------------------------|---------|
| **ILogging** | ILogging | StdErrLogging | Operational messages (init, errors, warnings) |
| **ITelemetryWriter** | ITelemetryWriter | InMemoryTelemetry | Bridge writes telemetry |
| **ITelemetryReader** | ITelemetryReader | InMemoryTelemetry | Presentation reads telemetry |
| **IPresentation** | IPresentation | ConsolePresentation | User output (text, TUI, GUI) |
| **IInputProvider** | IInputProvider | KeyboardInputProvider | User input (keyboard, upstream, automation) |

### Telemetry Implementation Table

| Implementation | Implements | Rationale |
|----------------|------------|-----------|
| **InMemoryTelemetry** | Writer + Reader | Writes to memory, reads from memory (real-time display) |
| **FileTelemetry** | Writer only | Writes to file, no readback needed (post-analysis) |
| **NetworkTelemetry** | Writer only | Sends to network, no readback needed (remote monitoring) |

**Note:** FileTelemetry and NetworkTelemetry only implement Writer interface. They don't need Reader because:
- File telemetry is for post-analysis, not live presentation
- Network telemetry remote endpoint has its own reader
- This avoids LSP violation (no fake getSnapshot() implementation)

### Provider Dependencies (Updated)

```
Bridge (runSimulation)
    ├── ILogging*           (for operational messages)
    ├── ITelemetryWriter*   (for writing telemetry - Writer interface only!)
    └── IInputProvider*     (for control inputs)

Presentation Layer
    └── ITelemetryReader*   (for reading telemetry - Reader interface only!)

CLI (CLIMain.cpp)
    ├── Creates InMemoryTelemetry (implements both Writer + Reader)
    ├── Passes Writer interface to bridge
    ├── Passes Reader interface to presentation
    └── Wires them together
```

---

## Transition Strategy

### Phase 1: Dual Output (Current)

Bridge writes to BOTH ILogging and ITelemetryWriter:

```cpp
// In bridge simulation loop
void runSimulation(...) {
    // Update simulation
    api_->Update(handle_, dt);

    // Get stats
    EngineSimStats stats;
    api_->GetStats(handle_, &stats);

    // Write to logging (immediate output - ConsolePresentation uses this)
    if (logger_) {
        logger_->info("RPM: %.0f, Load: %.2f, Flow: %.5f",
                      stats.currentRPM, stats.currentLoad, stats.exhaustFlow);
    }

    // Write to telemetry Writer interface (structured data - future TUI will use this)
    if (telemetryWriter_) {
        TelemetryData data;
        data.currentRPM = stats.currentRPM;
        data.currentLoad = stats.currentLoad;
        data.exhaustFlow = stats.exhaustFlow;
        // ... etc
        telemetryWriter_->write(data);  // Writer interface
    }
}
```

CLI wires the Writer and Reader interfaces:

```cpp
// In CLIMain.cpp
int main(int argc, char** argv) {
    // Create telemetry storage (implements both Writer + Reader)
    auto telemetry = std::make_unique<InMemoryTelemetry>();

    // Extract interfaces
    ITelemetryWriter* telemetryWriter = telemetry.get();
    ITelemetryReader* telemetryReader = telemetry.get();

    // Pass Writer to bridge
    bridge->setTelemetryWriter(telemetryWriter);

    // Pass Reader to presentation
    presentation->setTelemetryReader(telemetryReader);

    // ... rest of wiring
}
```

ConsolePresentation uses ILogging for now (transition period):

```cpp
// ConsolePresentation::ShowEngineState
void ShowEngineState(const EngineState& state) override {
    // For now, just use the logging output
    // Bridge is already writing to ILogging, so nothing to do here
    // TODO: Later, read from ITelemetryReader instead
}
```

### Phase 2: Telemetry-Only (Future)

Once TMUX TUI is implemented:

1. Bridge stops writing to ILogging for telemetry data
2. TUI Presentation reads from ITelemetryReader
3. Logging reserved for operational messages only

```cpp
// Future: Bridge writes to telemetry Writer only
void runSimulation(...) {
    // Update simulation
    api_->Update(handle_, dt);

    // Get stats
    EngineSimStats stats;
    api_->GetStats(handle_, &stats);

    // Write to telemetry Writer ONLY
    if (telemetryWriter_) {
        telemetryWriter_->write(stats);  // Writer interface
    }
}
```

```cpp
// Future: TUI Presentation reads from telemetry Reader
void TUIPresentation::Update(double dt) {
    TelemetryData data = telemetryReader_->getSnapshot();  // Reader interface

    // Update TUI display with structured data
    rpmLabel_->setText(formatRPM(data.currentRPM.load()));
    loadBar_->setValue(data.currentLoad.load() * 100);
    flowLabel_->setText(formatFlow(data.exhaustFlow.load()));
    // ... etc
}
```

---

## SOLID Compliance (Updated for Reader/Writer Split)

| Principle | Application | Status |
|-----------|-------------|--------|
| **SRP** | Each provider has single responsibility | ✅ Logging = messages, Telemetry = structured data |
| **OCP** | New telemetry storage added via interface, no modification | ✅ InMemory, File, Network implementations |
| **LSP** | All storage honor interface contracts | ✅ Writer implementations write(), Reader implementations getSnapshot() |
| **ISP** | ✅ **PASS** - Focused Reader/Writer interfaces | ✅ Bridge depends only on Writer, Presentation only on Reader |
| **DIP** | CLI depends on abstractions, not concrete implementations | ✅ ITelemetryWriter*, ITelemetryReader* injection |
| **KISS** | CLI is trivial - just wires providers together | ✅ Ultra-thin CLI |

### ISP Compliance (Key Improvement)

**Before (Single Interface - ISP Violation):**
```cpp
class ITelemetryProvider {
    virtual void write(const TelemetryData& data) = 0;  // Bridge needs
    virtual TelemetryData getSnapshot() const = 0;      // Presentation needs
};
// File/Network telemetry forced to implement getSnapshot() they don't use (LSP violation)
```

**After (Reader/Writer Split - ISP Compliant):**
```cpp
class ITelemetryWriter {
    virtual void write(const TelemetryData& data) = 0;  // Bridge only
};

class ITelemetryReader {
    virtual TelemetryData getSnapshot() const = 0;      // Presentation only
};
// File/Network telemetry implement only Writer (no LSP violation)
```

---

## Data Flow Diagram (Reader/Writer Split)

```
┌─────────────────────────────────────────────────────────────┐
│                      CLI (CLIMain.cpp)                      │
│  ┌─────────────────────────────────────────────────────┐   │
│  │  auto telemetry = std::make_unique<InMemoryTelemetry>() │
│  │  ITelemetryWriter* writer = telemetry.get()          │   │
│  │  ITelemetryReader* reader = telemetry.get()          │   │
│  │  bridge->setTelemetryWriter(writer)  ────┐          │   │
│  │  presentation->setTelemetryReader(reader) │          │   │
│  └─────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────┘
                           │                │
                           ▼                ▼
┌─────────────────────────────────┐  ┌──────────────────────────┐
│      Bridge Simulation          │  │   Presentation Layer      │
│                                 │  │                          │
│  ┌──────────┐    ┌───────────┐  │  │  ┌────────────────────┐ │
│  │  Update  │───▶│ GetStats  │  │  │  │ getSnapshot()      │ │
│  │ (physics)│    │(telemetry)│  │  │  │  (Reader interface)│ │
│  └──────────┘    └─────┬─────┘  │  │  └─────────┬──────────┘ │
│                       │          │  │            │            │
│                       ▼          │  │            ▼            │
│              ┌────────────────┐  │  │   ┌────────────────┐   │
│              │ write()        │  │  │   │ Display RPM,    │   │
│              │(Writer interface)│ │  │   │ Load, Flow,    │   │
│              └────────┬───────┘  │  │   │ etc.           │   │
│                       │          │  │   └────────────────┘   │
└───────────────────────┼──────────┘  └──────────────────────────┘
                        │
                        ▼
        ┌───────────────────────────────┐
        │     Telemetry Storage         │
        │  ┌─────────────────────────┐  │
        │  │ InMemoryTelemetry       │  │
        │  │ (Implements Writer+Reader)│
        │  │  - Atomic storage       │  │
        │  │  - Thread-safe reads   │  │
        │  │  - Zero-copy access    │  │
        │  └─────────────────────────┘  │
        └───────────────────────────────┘
```
│                               │ ITelemetryProvider  │      │
│                               │  (InMemoryTelemetry)│      │
│                               └──────────┬──────────┘      │
│                                          │                 │
└──────────────────────────────────────────┼─────────────────┘
                                           │
                                           │ getSnapshot()
                                           ▼
                               ┌─────────────────────┐
                               │  IPresentation      │
                               │  (Console/TUI/GUI)  │
                               │                     │
                               │  Display RPM, Load, │
                               │  Flow, etc.         │
                               └─────────────────────┘
```

---

## Implementation Plan

### Phase 1: Create Reader/Writer Interfaces (Week 1)

1. Create `engine-sim-bridge/include/ITelemetryProvider.h` with:
   - `ITelemetryWriter` interface (write, reset, getName)
   - `ITelemetryReader` interface (getSnapshot, getName)
   - `TelemetryData` struct (non-atomic snapshot)
2. Implement `InMemoryTelemetry` class (implements both Writer + Reader)
3. Add bridge header guards and namespace (telemetry)

### Phase 2: Bridge Integration (Week 2)

1. Add `ITelemetryWriter*` member to `EngineConfig` (C++ class)
2. Add setter: `EngineConfig::setTelemetryWriter(ITelemetryWriter*)`
3. In `runSimulation()` / `Update()`, write telemetry data via Writer interface
4. Keep dual output (ILogging + ITelemetryWriter) for transition

### Phase 3: Presentation Updates (Week 3)

1. Update `IPresentation` to accept `ITelemetryReader*`
2. `ConsolePresentation` continues using ILogging (transition)
3. Document future TUI Presentation will use ITelemetryReader
4. Update CLI wiring to pass Reader interface to presentation

### Phase 4: Testing (Week 4)

1. Unit tests for `InMemoryTelemetry` (thread safety, atomic operations)
2. Integration tests for bridge telemetry output
3. Verify thread safety (sim thread writes, main thread reads)

---

## Success Criteria

1. ✅ Bridge writes telemetry data via `ITelemetryWriter` interface
2. ✅ `InMemoryTelemetry` provides thread-safe, zero-copy access
3. ✅ Presentation layer reads telemetry via `ITelemetryReader` interface
4. ✅ ConsolePresentation still works (transition period)
5. ✅ No performance regression (atomic operations only)
6. ✅ SOLID compliance maintained (ISP: focused Reader/Writer interfaces)
7. ✅ Pure C++ architecture - no C API functions needed

---

## Open Questions

1. **Telemetry frequency:** Should telemetry update every frame or at fixed interval (e.g., 60Hz)?
   - **Recommendation:** Every frame for smoothest display, presentation can sample at its own rate

2. **Memory vs File telemetry:** When should we implement FileTelemetry?
   - **Recommendation:** YAGNI - wait for user request for telemetry logging

3. **Performance impact:** Atomic operations on every frame?
   - **Assessment:** Minimal - std::atomic is lock-free on most platforms
   - **Verification needed:** Profile before/after telemetry integration

---

## Appendix: File Locations

| Component | Location |
|-----------|----------|
| ITelemetryWriter/Reader interfaces | `engine-sim-bridge/include/ITelemetryProvider.h` |
| TelemetryData struct | `engine-sim-bridge/include/ITelemetryProvider.h` |
| InMemoryTelemetry implementation | `engine-sim-bridge/src/InMemoryTelemetry.cpp` |
| FileTelemetry implementation (future) | `engine-sim-bridge/src/FileTelemetry.cpp` |
| NetworkTelemetry implementation (future) | `engine-sim-bridge/src/NetworkTelemetry.cpp` |
| EngineConfig (accepts ITelemetryWriter*) | `src/EngineConfig.h/cpp` |
| CLI wiring (creates telemetry, passes interfaces) | `src/CLIMain.cpp` |

### Architecture Notes

**Pure C++ Architecture:**
- All telemetry interfaces and implementations are pure C++
- No C API wrapper functions needed
- EngineConfig (C++ class) accepts ITelemetryWriter* directly
- CLI (C++) wires interfaces directly without C API indirection

**C API Boundary:**
- The C API is only at the engine-sim boundary (which we don't control)
- Our bridge is C++ and uses C++ interfaces throughout
- All target platforms (macOS, iOS, ESP32) support C++ natively

---

*End of Telemetry Architecture Document*
