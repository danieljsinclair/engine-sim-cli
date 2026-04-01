# CLI Thinning Plan (Refined)

**Document Version:** 2.0
**Date:** 2026-04-01
**Status:** Architecture Decision Record (ADR)
**Authors:** Solution Architect, with Test Architect and Tech Architect consensus

---

## Executive Summary

This document defines the refined CLI architecture based on user clarification. The CLI is an **ultra-thin veneer** that only parses arguments and wires together dependency injection providers. All heavy lifting (simulation, audio, buffering) is handled by the bridge or platform-specific modules.

### Key Architectural Decisions (Refined 2026-04-01)

| Decision | Rationale |
|----------|-----------|
| **CLI = Ultra-thin veneer** | Parse args, wire providers, call bridge - NOTHING else |
| **NO threading in CLI** | Bridge/platform handles threading |
| **NO buffering in CLI** | Platform-specific modules handle their own buffering |
| **NO physics in CLI** | Bridge handles all simulation |
| **DI Provider Pattern** | All providers injectable with console defaults |
| **ITelemetryProvider** | NEW: Separate telemetry from logging |

### DI Providers (All with Console Defaults)

| Provider | Interface | Default Implementation | Purpose |
|----------|-----------|------------------------|---------|
| **ILogging** | ILogging | StdErrLogging | Operational messages (init, errors, warnings) |
| **ITelemetryProvider** | ITelemetryProvider | InMemoryTelemetry | Structured data (RPM, load, flow) for display |
| **IPresentation** | IPresentation | ConsolePresentation | User output (text, TUI, GUI) |
| **IInputProvider** | IInputProvider | KeyboardInputProvider | User input (keyboard, upstream, automation) |

---

## Refined CLI Responsibilities (Ultra-Thin)

### CLI DOES (Minimal - Ultra-Thin Veneer)

| Responsibility | Description | Location |
|----------------|-------------|----------|
| **Parse arguments** | Convert CLI args to SimulationConfig | `CLIMain.cpp`, `CLIConfig.cpp/h` |
| **Wire providers** | Create and inject all DI providers | `CLIMain.cpp` |
| **Signal handling** | Handle Ctrl+C for graceful shutdown | `CLIMain.cpp` |
| **Call bridge** | Invoke bridge APIs for simulation | `CLIMain.cpp` → bridge |

### CLI DOES NOT DO (Bridge/Platform Responsibilities)

| Responsibility | Who Handles It | Why |
|----------------|----------------|-----|
| **Threading** | Bridge/Platform modules | Real-time audio requires platform threading |
| **Audio buffering** | Platform-specific modules | Different platforms buffer differently |
| **Physics simulation** | Bridge | Platform-agnostic simulation |
| **Audio generation** | Bridge | Platform-agnostic audio synthesis |
| **Audio playback** | Platform modules (CoreAudio/AVAudioEngine/I2S) | Platform-specific APIs |
| **Telemetry storage** | ITelemetryProvider implementations | Memory/file/network options |

### CLI Data Flow (Ultra-Thin)

```
┌─────────────────────────────────────────────────────────────┐
│                     CLIMain.cpp                             │
│  1. Parse CLI args → SimulationConfig                       │
│  2. Create providers (ILogging, ITelemetryProvider, etc.)   │
│  3. Call bridge.runSimulation(config, providers)           │
│  4. Return exit code                                        │
└─────────────────────────────────────────────────────────────┘
                           │
                           │ Passes providers
                           ▼
┌─────────────────────────────────────────────────────────────┐
│              Bridge (runSimulation)                         │
│  - Runs physics (Update)                                    │
│  - Generates audio (Render)                                 │
│  - Writes telemetry (ITelemetryProvider)                    │
│  - Logs operations (ILogging)                               │
└─────────────────────────────────────────────────────────────┘
```

**Key Point:** CLI is just wiring code. All logic is in bridge or platform modules.

---

## ITelemetryProvider (NEW - Critical Addition)

### Problem: Telemetry vs Logging

**Current Issue:** Telemetry data (RPM, load, exhaust flow) is mixed with operational logging:

```cpp
// Current: Telemetry disguised as logging
logger->info("RPM: %f, Load: %f", rpm, load);
```

**Problems:**
- Can't suppress logging without losing telemetry
- Parsing structured data from text strings is fragile
- Future TMUX TUI needs structured data, not text streams

### Solution: Separate Telemetry Provider

```cpp
// New: Structured telemetry
telemetry->updateStats(stats);  // Structured data
```

**Benefits:**
- Clear separation: logging = messages, telemetry = data
- Thread-safe atomic access (InMemoryTelemetry)
- Future-proof: TUI/GUI can consume structured data
- Logging can be suppressed independently

### Transition Strategy

**Phase 1 (Current): Dual Output**
- Bridge writes to BOTH ILogging and ITelemetryProvider
- ConsolePresentation uses ILogging for now
- No breaking changes

**Phase 2 (Future): Telemetry-Only**
- Bridge writes to ITelemetryProvider only
- TUI Presentation reads structured telemetry
- Logging reserved for operational messages

See `docs/TELEMETRY_ARCHITECTURE.md` for full details.

---

## Critical Architectural Understanding

**The bridge (`engine-sim-bridge`) is a platform-agnostic C API wrapper around engine-sim.**

The bridge provides:
- `EngineSimCreate/Destroy` - Simulator lifecycle
- `EngineSimLoadScript` - Script loading with path resolution
- `EngineSimUpdate/Render` - Physics and audio generation
- `EngineSimSetThrottle/Ignition/StarterMotor` - Control inputs
- `EngineSimGetStats` - State readout

The bridge should NOT contain:
- **Platform-specific audio playback** (CoreAudio, AVAudioEngine, I2S) - These are CLIENT responsibilities
- **Platform-specific input handling** (keyboard, touch, CAN bus) - CLIENT responsibilities
- **UI/presentation logic** (console, TUI, GUI) - CLIENT responsibilities

**Rationale:** Different platforms implement these differently. Moving them to the bridge would violate the Single Responsibility Principle and make the bridge platform-specific, defeating its purpose.

---

## Current State Analysis

### What the CLI Currently Does (Appropriate Responsibilities)

| Responsibility | File(s) | Status | Assessment |
|----------------|---------|--------|------------|
| Argument parsing | `CLIConfig.cpp/h`, `CLIMain.cpp` | ✅ Keep | CLI's core purpose |
| Signal handling | `CLIMain.cpp` | ✅ Keep | Platform-specific |
| macOS CoreAudio playback | `AudioPlayer.cpp/h` | ✅ Keep | Platform-specific client code |
| Audio buffering | `CircularBuffer.cpp/h` | ✅ Keep | Used by AudioPlayer |
| Sync-pull audio rendering | `SyncPullAudio.cpp/h` | ✅ Keep | Platform-specific client code |
| Audio mode strategies | `audio/modes/*` | ✅ Keep | Client strategies for using bridge |
| Audio renderer strategies | `audio/renderers/*` | ✅ Keep | Client-side rendering strategies |
| Keyboard input handling | `KeyboardInput.cpp/h` | ✅ Keep | Used by KeyboardInputProvider |
| Input provider interface | `interfaces/IInputProvider.h` | ✅ Keep | Client-side abstraction |
| Keyboard input provider | `interfaces/KeyboardInputProvider.cpp/h` | ✅ Keep | CLI-specific keyboard input |
| Console presentation | `interfaces/ConsolePresentation.cpp/h` | ✅ Keep | CLI-specific presentation |
| Simulation orchestration | `SimulationLoop.cpp/h` | ✅ Keep | Client-side orchestration |
| Engine config wrapper | `EngineConfig.cpp/h` | ✅ Keep | C++ wrapper over bridge C API |
| Bridge API loader | `engine_sim_loader.h` | ✅ Keep | Bridge API wrapper |
| Logging interface default | `ILogging.cpp` | ✅ Keep | Default impl of ILogging |
| Console colors | `ConsoleColors.cpp/h` | ✅ Keep | CLI-specific formatting |

### Dead Code Identified (2 items)

| File | Status | Action Required |
|------|--------|-----------------|
| `RPMController.cpp/h` | ❌ NEVER used | DELETE - Verified via grep, no call sites |
| `displayHUD()` in `CLIconfig.cpp` | ❌ NEVER called | DELETE - Verified via grep, no call sites |

**Note:** `KeyboardInput.cpp/h` is NOT dead code - it IS used by `KeyboardInputProvider` (verified via grep).

---

## What NOT To Do (Critical - Avoid These Mistakes)

### ❌ DO NOT Move Audio Infrastructure to Bridge

**Incorrect:**
- Moving `AudioPlayer.cpp/h` to bridge
- Moving `CircularBuffer.cpp/h` to bridge
- Moving `audio/modes/*` to bridge
- Moving `audio/renderers/*` to bridge

**Why this is wrong:**
- `AudioPlayer` uses macOS CoreAudio - other platforms need different implementations
- iOS would use AVAudioEngine
- ESP32 would use I2S driver
- The bridge should remain platform-agnostic

### ❌ DO NOT Move Input/Presentation Interfaces to Bridge

**Incorrect:**
- Moving `IInputProvider` to bridge
- Moving `IPresentation` to bridge
- Moving `KeyboardInputProvider` to bridge

**Why this is wrong:**
- These are client-side abstractions
- Different platforms need different implementations
- The bridge doesn't use these interfaces - the CLI does

### ❌ DO NOT Move SimulationLoop to Bridge

**Incorrect:**
- Moving `SimulationLoop.cpp/h` to bridge

**Why this is wrong:**
- `SimulationLoop` is client-side orchestration
- The bridge provides the API; the client decides how to use it
- Moving it would make the bridge into the CLI, defeating the purpose

### ❌ DO NOT Delete These Files (They Are Used)

| File | Why It's Needed |
|------|-----------------|
| `KeyboardInput.cpp/h` | Used by `KeyboardInputProvider` |
| `AudioPlayer.cpp/h` | macOS CoreAudio platform code |
| `EngineConfig.cpp/h` | Type-safe C++ wrapper over bridge C API |
| `ILogging.cpp` | Default implementation of ILogging interface |

---

## Corrected Execution Plan

### Phase 1 (High Priority): Delete Dead Code

**Action Items:**
1. DELETE `src/RPMController.cpp` - NEVER used (verified via grep)
2. DELETE `src/RPMController.h` - NEVER used (verified via grep)
3. DELETE `displayHUD()` function from `src/CLIConfig.cpp` - NEVER called (verified via grep)

**Test Strategy:**
- Run existing smoke tests to ensure no regression
- Verify build succeeds with no missing symbol errors

**Commit Strategy:**
- Use `git rm` to delete files (preserves history)
- Commit message: "chore: remove dead code (RPMController, displayHUD)"

**Rollback Strategy:**
- If tests fail: `git reset --soft HEAD~1` to undo commit

**Acceptance Criteria:**
- [ ] All files deleted successfully
- [ ] Build succeeds with no errors
- [ ] All tests pass
- [ ] No missing symbol errors

### Phase 2 (Low Priority, Optional): SimulationLoop Review

**Action Items:**
1. Analyze `SimulationLoop.cpp` (400+ lines) for SRP compliance
2. Determine if refactoring provides clear benefit
3. Only make changes if benefit is clear

**Note:** Current implementation may be acceptable - orchestration is inherently complex. This phase is OPTIONAL and should only proceed if clear benefit exists.

### Phase 3 (Medium Priority): Documentation Update

**Action Items:**
1. Update `BRIDGE_INTEGRATION_ARCHITECTURE.md` if needed to clarify CLI vs bridge responsibilities
2. Ensure `ARCHITECTURE_TODO.md` reflects completed state

**Acceptance Criteria:**
- [ ] Documentation accurately reflects architecture
- [ ] No misleading statements about "moving files to bridge"

---

## SOLID Compliance Assessment

### Current Architecture Scorecard

| Principle | Status | Notes |
|-----------|--------|-------|
| **SRP** | ✅ PASS | Good separation - CLI handles platform-specific code, bridge handles simulation |
| **OCP** | ✅ PASS | Strategy pattern via IAudioMode and IAudioRenderer |
| **LSP** | ✅ PASS | Interface contracts honored |
| **ISP** | ✅ PASS | Focused interfaces (IInputProvider, IPresentation) |
| **DIP** | ✅ PASS | Dependencies inverted (high-level modules depend on abstractions) |
| **DRY** | ✅ PASS | Path resolution consolidated to bridge (Phase 5 completed) |
| **YAGNI** | ✅ PASS | Dead code removal in progress (Phase 1) |

---

## Success Criteria

1. ✅ All dead code removed (RPMController, displayHUD)
2. ✅ All tests passing (no regression)
3. ✅ Clear separation of concerns maintained
4. ✅ Bridge remains platform-agnostic
5. ✅ CLI remains appropriately structured
6. ✅ SOLID compliance maintained

---

## Bridge C API Status (No Changes Required)

The current bridge C API is already complete and appropriate:

```cpp
// Lifecycle
EngineSimCreate()
EngineSimLoadScript()
EngineSimSetLogging()
EngineSimDestroy()

// Control
EngineSimSetThrottle()
EngineSimSetIgnition()
EngineSimSetStarterMotor()
EngineSimUpdate()

// Audio
EngineSimRender()              // Synchronous rendering
EngineSimRenderOnDemand()      // Sync-pull mode
EngineSimReadAudioBuffer()     // Async mode (with audio thread)
EngineSimStartAudioThread()    // Start async audio thread

// Diagnostics
EngineSimGetStats()
EngineSimGetLastError()
```

**No new Bridge API functions are needed** because the CLI is already thin and properly delegates to the bridge.

---

## Conclusion

**Initial plan (INCORRECT):** Move 20+ files to bridge
**Corrected plan:** Delete 2 dead files, keep everything else

The CLI is appropriately structured. The bridge is a thin platform-agnostic C API, and the CLI contains platform-specific client code as it should.

### Summary of Required Actions

| Phase | Priority | Effort | Status |
|-------|----------|--------|--------|
| Phase 1: Delete dead code | HIGH | Low | TODO |
| Phase 2: SimulationLoop review | LOW | Medium | OPTIONAL |
| Phase 3: Documentation update | MEDIUM | Low | TODO |

---

## Appendix: File Locations

| Category | Files |
|----------|-------|
| **To Delete** | `src/RPMController.cpp`, `src/RPMController.h`, `displayHUD()` in `src/CLIConfig.cpp` |
| **CLI (Keep)** | All files in `src/`, `src/audio/`, `src/interfaces/` |
| **Bridge (Keep)** | All files in `engine-sim-bridge/` |
| **Tests (Keep)** | All files in `test/` |

---

## Team Consensus

**Solution Architect:** Daniel Sinclair
**Test Architect:** [Pending]
**Tech Architect:** [Pending]

*Date:* 2026-04-01

---

*End of CLI Thinning Plan*
