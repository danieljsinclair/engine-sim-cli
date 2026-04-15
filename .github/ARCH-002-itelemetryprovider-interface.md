# ARCH-002: ITelemetryProvider Interface Design and Implementation

**Priority:** P1 - High Priority
**Status:** PARTIALLY COMPLETE -- interface exists as `ITelemetryWriter`, no concrete implementation
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @product-owner

## Overview

Design and implement a telemetry interface to separate telemetry concerns from logging. This provides clean abstraction for telemetry data production and consumption, enabling future TMUX presentation and cross-platform telemetry support.

## Problem Statement (Original)

Previous implementation mixed logging and telemetry concerns with no clean abstraction for telemetry data production, storage, or consumption. This violated SRP and made it difficult to implement different telemetry consumers (CLI, TMUX, etc.).

## As-Is State (Current Codebase)

### What Exists
- `telemetry::ITelemetryWriter` -- forward-declared in `src/simulation/SimulationLoop.h`
- Used in `SimulationLoop.cpp` via `#include "ITelemetryProvider.h"`
- `SimulationConfig` has a `telemetry::ITelemetryWriter* telemetryWriter = nullptr` member
- `writeTelemetry()` function in `SimulationLoop.cpp` writes `TelemetryData` structs to the writer
- `runUnifiedAudioLoop()` accepts `telemetry::ITelemetryWriter*` parameter

### What Does NOT Exist
- No concrete implementation of `ITelemetryWriter` in the source tree
- No `ITelemetryProvider` interface (the original proposal name)
- No `TelemetryData` struct definition visible in headers (likely in the `ITelemetryProvider.h` include)
- No in-memory telemetry storage
- No thread-safe telemetry collection

### Current Data Flow
```
SimulationLoop::runUnifiedAudioLoop()
  -> writeTelemetry(telemetryWriter, stats, ...)
     -> telemetryWriter->write(data)  // No-op when telemetryWriter is nullptr
```

In `CLIMain.cpp`, `telemetryWriter` is never set (always `nullptr`), so telemetry is effectively disabled.

### Gaps from Original Design
1. The interface is called `ITelemetryWriter` not `ITelemetryProvider` -- narrower scope
2. No `recordMetric()` or `getAudioMetrics()` methods -- only `write(TelemetryData)`
3. No event system for telemetry
4. No in-memory storage or retrieval API
5. No concrete implementation exists

## To-Be State

### Remaining Work
1. **Locate/consolidate `ITelemetryProvider.h`** -- the file is included but its location relative to src/ is unclear
2. **Create concrete `TelemetryWriter` implementation** or decide this is deferred
3. **Wire `telemetryWriter` in `CLIMain.cpp`** if concrete implementation is created
4. **Consider expanding** to the richer `ITelemetryProvider` design if TMUX/UI needs require it

### Assessment
The telemetry plumbing (interface, data flow) is in place in `SimulationLoop`, but no concrete implementation exists. The current `ITelemetryWriter` is a simpler write-only interface compared to the original `ITelemetryProvider` proposal. This may be intentional -- a minimal viable interface.

## Acceptance Criteria Status

### Interface Design
- [x] `ITelemetryWriter` interface exists (simpler than proposed `ITelemetryProvider`)
- [x] `TelemetryData` struct used in `writeTelemetry()`
- [x] Thread-safe API design (pointer passed to simulation loop)
- [x] Clear separation from ILogging interface
- [ ] Rich metrics API (`recordMetric`, `getAudioMetrics`) -- NOT implemented

### Implementation
- [ ] Concrete `ITelemetryWriter` implementation -- NOT created
- [ ] In-memory telemetry storage -- NOT created
- [ ] Memory-bounded storage -- NOT created

### Integration
- [x] Simulation loop accepts `ITelemetryWriter*` parameter
- [x] Telemetry is optional (nullptr check in `writeTelemetry()`)
- [x] No impact on audio thread (telemetry written from main loop)
- [ ] Wired in CLIMain (always nullptr currently)

### Testing
- [ ] Thread safety tests -- N/A (no implementation yet)
- [ ] Memory bounds tests -- N/A (no implementation yet)

## References

- `/Users/danielsinclair/vscode/escli.refac7/src/simulation/SimulationLoop.h` -- forward declaration
- `/Users/danielsinclair/vscode/escli.refac7/src/simulation/SimulationLoop.cpp` -- usage
- `/Users/danielsinclair/vscode/escli.refac7/src/config/CLIMain.cpp` -- entry point (telemetryWriter = nullptr)

---

**Created:** 2026-04-08
**Last Updated:** 2026-04-15
**Estimate:** 1-2 days (interface), additional for implementation


