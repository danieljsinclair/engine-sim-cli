# Architecture Refactoring TODO

## Status: IN PROGRESS - Refined Architecture (2026-04-01)

## Refined CLI Architecture (NEW)

### CLI = Ultra-Thin Veneer

**CLI Responsibilities (MINIMAL):**
- Parse command line arguments
- Configure and run the simulator
- Operate controls (throttle, clutch, ignition, starter) via simple bridge APIs
- NO threading, NO buffering, NO physics in CLI

**Audio Buffering Location:**
- NOT in CLI
- Platform-specific modules handle their own buffering
- Bridge provides high-level runSimulation() API

### DI Provider Pattern (All with Console Defaults)

| Provider | Interface | Default Implementation | Purpose |
|----------|-----------|------------------------|---------|
| **ILogging** | ILogging | StdErrLogging | Operational messages |
| **ITelemetryProvider** | ITelemetryProvider | InMemoryTelemetry | Structured data (NEW) |
| **IPresentation** | IPresentation | ConsolePresentation | User output |
| **IInputProvider** | IInputProvider | KeyboardInputProvider | User input |

### ITelemetryProvider (NEW INTERFACE)

**Purpose:** Separate telemetry from logging
- Bridge produces telemetry/stats (RPM, load, exhaust flow, etc.)
- DISTINCT from logging (logging = operational messages, telemetry = data)
- Bridge writes to ITelemetryProvider
- IPresentation reads from ITelemetryProvider for display
- Default: In-memory atomic struct (thread-safe reads)
- Future: TMUX IPresentation will consume this

**Transition Strategy:**
- For now: Bridge pushes to BOTH ILogging (immediate output) AND ITelemetry
- ConsolePresentation uses ILogging for now
- Future: Logging can be suppressed, IPresentation uses ITelemetry exclusively

**Documentation:** See `docs/TELEMETRY_ARCHITECTURE.md` for full details.

---

## Completed ✅
- [x] Git mv AudioMode.cpp → audio/modes/ThreadedAudioMode.cpp (history preserved)
- [x] Extract renderers to audio/renderers/ folder
- [x] Extract mode classes to audio/modes/ folder
- [x] One class per file for renderers
- [x] DRY helpers extracted (clampFramesToCapacity, handleUnderrun)
- [x] Remove AudioPlayerFactory - redundant
- [x] Create IInputProvider interface (injectable input: keyboard, upstream)
- [x] Create IPresentation interface (injectable output: console, TUI, headless)
- [x] Create interfaces/ folder with providers
- [x] SimulationConfig replaces CommandLineArgs in SimulationLoop
- [x] float volume replaces bool silent in SimulationConfig
- [x] GetSimulationConfig() moved to CLIMain.cpp
- [x] Phase 1: Remove dead code from EngineConfig (YAGNI)
- [x] Phase 2: Logger DI injection (eliminate static storage)
- [x] Phase 3: Path resolution tests (commit aba7cf8)
- [x] Phase 4: Bridge normalizeScriptPath fix (commits fbb56b7, e75c422)
- [x] Phase 5: Path consolidation to bridge (commit e43467f)

## In Progress
- [x] Refined CLI Architecture documentation (2026-04-01)
- [x] ITelemetryProvider interface design (TELEMETRY_ARCHITECTURE.md)
- [ ] Sample amplitude transform (multiply float samples by gain factor)
- [ ] iOS audio: AudioUnit alternative (AVAudioEngine?)

## Telemetry Implementation Tasks (NEW - 2026-04-01)

### Phase 1: Create ITelemetryProvider (Week 1)
- [ ] Create `engine-sim-bridge/include/ITelemetryProvider.h`
- [ ] Implement `InMemoryTelemetry` default (atomic/thread-safe)
- [ ] Add `EngineSimSetTelemetry()` C API function
- [ ] Update bridge C API docs

### Phase 2: Bridge Integration (Week 2)
- [ ] Add telemetry member to `EngineSimContext`
- [ ] In `runSimulation()` / `Update()`, write telemetry data
- [ ] Keep dual output (ILogging + ITelemetry) for transition
- [ ] Update bridge with telemetry calls

### Phase 3: Presentation Updates (Week 3)
- [ ] Update `IPresentation` to accept `ITelemetryProvider*`
- [ ] `ConsolePresentation` continues using ILogging (transition)
- [ ] Document future TUI Presentation will use telemetry

### Phase 4: Testing (Week 4)
- [ ] Unit tests for `InMemoryTelemetry`
- [ ] Integration tests for bridge telemetry output
- [ ] Verify thread safety (sim thread writes, main thread reads)

## SOLID Requirements (Refined 2026-04-01)

### SRP: Provider Separation
- ILogging: Operational messages (init, errors, warnings)
- ITelemetryProvider: Structured data (RPM, load, flow)
- IPresentation: User output display
- IInputProvider: User input handling

### CLI vs Bridge (Refined)
- **CLI**: Parse args, wire providers, call bridge (ultra-thin)
- **Bridge**: Physics, audio, telemetry production
- **Platform modules**: Audio playback, buffering, threading

## SOLID Requirements

### SRP: CLI vs SimulationLoop
- CLIMain.cpp: Parses CLI args, creates SimulationConfig, passes raw paths to bridge
- SimulationLoop.cpp: ONLY knows about SimulationConfig, NOT CommandLineArgs
- Bridge: Handles all path resolution logic (normalization, asset base path derivation)

### SimulationConfig struct
- configPath: Engine config path (raw - bridge handles resolution)
- assetBasePath: Asset base path (raw - bridge handles resolution)
- logger: ILogging* for proper DI (eliminates static storage)
- duration, interactive, playAudio, volume, sineMode, syncPull
- targetRPM, targetLoad, useDefaultEngine, outputWav
- audioMode: Injected (OCP compliance)

### GetSimulationConfig (in CLIMain.cpp)
- Converts CommandLineArgs → SimulationConfig
- Passes raw paths to bridge (bridge handles resolution)
- Maps args.silent → config.volume (0.0 or 1.0)

## Current Architecture

```
CLI (Thin shell)
├── Argument parsing (parseArguments)
├── Signal handling
└── Dependency Injection
    ├── IAudioMode (via factory)
    ├── IInputProvider (keyboard/upstream)
    └── IPresentation (console/TUI/headless)

SimulationLoop (Orchestration)
├── runSimulation()
└── runUnifiedAudioLoop()
    ├── Uses IInputProvider (not hardcoded KeyboardInput)
    └── Uses IPresentation (not hardcoded cout)
```

## Interfaces

### IInputProvider
- `KeyboardInputProvider` - wraps KeyboardInput
- `UpstreamProvider` - data from upstream (ODB2/virtual sim - abstracted)
- Future: ODB2, virtual simulator providers

### IPresentation  
- `ConsolePresentation` - text output to console
- `TUI Presentation` - TMUX-based character UI (TODO)
- `HeadlessLogger` - no output, just logging (TODO)

## Platform Targets
| Platform | Status |
|----------|--------|
| macOS CLI | ✅ Working |
| iOS | Future |
| ESP32 | Future |

## Bridge vs CLI
See `docs/BRIDGE_INTEGRATION_ARCHITECTURE.md` for detailed architecture.

## SOLID Status

### Overall Assessment
- SRP: ✅ Good (input/presentation extracted)
- OCP: ✅ Good (Strategy pattern for audio mode)
- LSP: ✅ Good
- ISP: ✅ Good (separate IInputProvider, IPresentation)
- DIP: ✅ Good (factory returns interfaces)
- DRY: ✅ **PASS** (duplicate path resolution removed)
- YAGNI: ✅ **PASS** (dead code removed)

### SOLID Pedant Analysis (2026-04-01)

#### ✅ SOLID Compliance (Good)
- **SRP**: `EngineConfig`, `SimulationLoop`, `AudioPlayer` each have single responsibilities
- **OCP**: Strategy pattern via `IAudioMode` and `IAudioRenderer` allows extension without modification
- **DIP**: High-level modules depend on abstractions (`IAudioMode`, `IInputProvider`, `IPresentation`)
- **DI**: Dependencies are properly injected throughout

#### ✅ Phase 1-5 Completed (2026-04-01)

**Phase 1: Dead Code Removal (commit 3f95af2)**
- Removed `EngineConfig::resolveAssetBasePath()` - unused, bridge handles path resolution
- Removed `EngineConfig::resolvePath()` - unused private method
- Removed `EngineConfig::ConfigPaths` struct - unused
- Removed unused includes from EngineConfig files
- YAGNI compliance improved

**Phase 2: Logger DI Fix (commit b52a5f7)**
- Added `ILogging* logger` to `SimulationConfig` for proper DI
- Created `StdErrLogging` in `CLIMain` and passed via config
- Removed static `StdErrLogging` from `SimulationLoop`
- Eliminated static storage limitation (only one instance)
- Enabled multiple simulator instances if needed
- Thread-safe by design

**Phase 3: Path Resolution Tests (commit aba7cf8)**
- Added comprehensive path scenario tests in `test/smoke/test_path_resolution.cpp`
- Tests cover: default engine, relative paths, absolute paths, asset base path derivation
- Tests verify file existence validation and error handling
- Tests cover special path formats (`~/`, `../`, etc.)
- All 20 tests passing

**Phase 4: Bridge normalizeScriptPath Fix (commits fbb56b7, e75c422)**
- Fixed path normalization to handle relative paths correctly
- Fixed asset base path derivation from script path
- Bridge now properly handles both relative and absolute paths
- SRP compliance improved (bridge owns all path logic)

**Phase 5: Path Consolidation to Bridge (commit e43467f)**
- CLI now passes raw paths to bridge (thin shell pattern)
- Bridge handles all path resolution logic
- Removed duplicate path resolution code from CLI
- DRY compliance improved
- SRP compliance improved (clear separation of concerns)

#### ✅ Clean Status (Updated 2026-04-01)
All critical SOLID violations identified have been resolved through Phase 1-5:
- DRY violations eliminated (Phase 1, Phase 5)
- YAGNI violations removed (Phase 1)
- Static storage issues resolved (Phase 2)
- Logger DI properly implemented (Phase 2)
- Path resolution properly delegated to bridge (Phase 4, Phase 5)
- Path resolution test coverage added (Phase 3)

#### 📊 SOLID Scorecard

| Principle | Status | Notes |
|-----------|--------|-------|
| SRP | ✅ PASS | Good separation |
| OCP | ✅ PASS | Strategy pattern |
| LSP | ✅ PASS | Interface contracts |
| ISP | ✅ PASS | Focused interfaces |
| DIP | ✅ PASS | Dependencies inverted |
| **DRY** | ✅ **PASS** | Path resolution consolidated |
| **YAGNI** | ✅ **PASS** | Dead code removed |

#### 🎯 Action Items (All Completed ✅)

**Phase 1 - YAGNI Dead Code Removal (commit 3f95af2):**
- [x] Remove `EngineConfig::resolveAssetBasePath()` - unused
- [x] Remove `EngineConfig::resolvePath()` - unused private method
- [x] Remove `EngineConfig::ConfigPaths` struct - unused
- [x] Remove unused includes from EngineConfig files

**Phase 2 - Logger DI Injection (commit b52a5f7):**
- [x] Add `ILogging* logger` to `SimulationConfig`
- [x] Create `StdErrLogging` in `CLIMain` and pass via config
- [x] Remove static `StdErrLogging` from `SimulationLoop`

**Phase 3 - Path Resolution Tests (commit aba7cf8):**
- [x] Add default engine path resolution test
- [x] Add relative script path resolution test
- [x] Add absolute script path resolution test
- [x] Add asset base path derivation test
- [x] Add file existence validation error handling test
- [x] Add special path formats test (`~/`, `../`, etc.)

**Phase 4 - Bridge Path Fix (commits fbb56b7, e75c422):**
- [x] Fix normalizeScriptPath to handle relative paths correctly
- [x] Fix asset base path derivation from script path
- [x] Bridge properly handles both relative and absolute paths

**Phase 5 - Path Consolidation (commit e43467f):**
- [x] CLI passes raw paths to bridge (thin shell pattern)
- [x] Bridge handles all path resolution logic
- [x] Remove duplicate path resolution code from CLI
- [x] DRY compliance improved
- [x] SRP compliance improved

**Earlier Work (Pre-Phase 1):**
- [x] Remove `resolveConfigPath()` from `SimulationLoop.cpp` (commit 2330d4a)
- [x] Delete `useConfigScript` conditional and dead code
- [x] Bridge handles path resolution via `LoadScript()`
- [x] Bridge owns path storage in `EngineSimContext`

## Audio Volume Control

### Current (macOS CLI)
- `AudioPlayer::setVolume(float)` uses AudioUnit's kHALOutputParam_Volume
- Controls output volume at the HAL level
- --silent sets volume=0.0, normal is volume=1.0

### Future Platforms

#### iOS
- No AudioUnit - use AVAudioEngine instead
- AVAudioMixerNode has volume property
- Need AudioRenderer abstraction for platform-specific audio

#### ESP32
- I2S driver has software volume control
- Or use DSP library for gain

### Sample Amplitude Transform (TODO)
- Multiply float samples by gain factor before output
- Useful for: cranking volume boost, fade in/out, compression
- Example: `applyGain(samples, frameCount, gain)`

### Static Linking Architecture (COMPLETED in reference escli)
- **DECISION**: Bridge should be statically linked, NOT dynamically loaded via dlopen
- Reference implementation: `/Users/danielsinclair/vscode/escli/escli` (uncommitted)
- Direct function calls instead of function pointer indirection
- C++ interfaces for SOLID compliance (IInputProvider, IPresentation)
- No runtime library loading - compile-time type safety
- TODO: Port reference implementation to escli.refac7

### Ignition State Management
- Input providers report user intent (toggle pressed), not state
- Orchestrator manages actual ignition state
- Default ON unless user explicitly toggles OFF

## Audio Mode Architecture

### Threaded Mode (default)
- Pre-fills 100ms+ buffer before playback starts
- Main loop generates audio in separate thread
- Cursor-chasing: producer (main loop) stays ahead of consumer (audio callback)
- Robust against physics spikes (~16ms)

### Sync-Pull Mode  
- No pre-buffer cushion - generates on-demand in audio callback
- Any physics delay directly causes crackles
- Use --threaded for reliability

### Audio Callback Rules (IMPORTANT)
- NO cout/printf in audio callback thread!
- Multiple threads printing = scrambled interleaved output
- Store stats in atomic/shared variables, presentation handles ALL output
- See: SyncPullAudio::renderOnDemand() - no debug there now

## Logging Policy

**Neither the bridge nor the CLI shall write directly to `cout` or `cerr`. No exceptions.**

| Output type | Interface | Rationale |
|-------------|-----------|----------|
| Engine state (RPM, throttle, audio mode) | `IPresentation` | Swappable UI layer |
| Diagnostic / operational messages | `ILogging` | Injectable, filterable, testable |
| Raw data piped downstream | `stdout` only | Keep pipe-safe |

- `ILogging` is injected into the bridge via `EngineSimSetLogging(handle, logger)`.
- The CLI creates an `ILogging` implementation (default: `StdErrLogging`) and passes it to the bridge at startup. The CLI's own diagnostic output must also go through the same logger.
- `StdErrLogging` writes to `stderr`, so diagnostic noise never corrupts a `stdout` pipe.
- **Audio callback**: additional constraint — no logging calls (or any blocking I/O) inside the audio callback thread. Stats are written to atomics; the presentation layer reads them on the main thread.

### ILogging design intent — functional-area selectors

Current `ILogging` uses severity levels (Debug/Info/Warning/Error). The intended direction is bitwise area flags so developers can trace one subsystem without noise from others:

```cpp
// Intended (requires ILogging redesign — not yet implemented):
enum LogArea : uint32_t {
    AUDIO   = 1 << 0,
    PHYSICS = 1 << 1,
    BUFFER  = 1 << 2,
    CONFIG  = 1 << 3,
    BRIDGE  = 1 << 4,
};
```

Until that redesign, use `LogLevel::Debug` for per-frame traces and `LogLevel::Info` for init/shutdown messages.

### TODO: `cout`/`cerr` migration
- [ ] Audit `engine-sim-bridge/src/` — replace all `cout`/`cerr` with `ILogging` calls
- [ ] Audit `src/` (CLI) — replace all `cout`/`cerr` with `ILogging` calls
- [ ] Bitwise area-selector redesign of `ILogging`

## Debug Output Consolidation

### All RPM/Throttle Output Locations:
| File | Function | Status |
|------|----------|---------|
| AudioSource.cpp | SineAudioSource::displayProgress | ✅ Frequency |
| AudioSource.cpp | EngineAudioSource::displayProgress | ✅ Flow |
| CLIConfig.cpp | displayHUD() | TODO remove — dead code, no call sites |
| ConsolePresentation.cpp | ShowEngineState() | ✅ Primary engine state output |

### Fix Required:
- Remove displayHUD() from CLIConfig.cpp (dead code)
- audioSource.displayProgress() already handles both modes correctly

## Path Resolution Test Coverage

### Current Status: **COMPLETE** (2026-04-01)

**Test Coverage Added (Phase 3, commit aba7cf8):**
- Comprehensive path scenario tests in `test/smoke/test_path_resolution.cpp`
- Tests verify: default engine, relative paths, absolute paths, asset base path derivation
- Tests verify file existence validation and error handling
- Tests cover special path formats (`~/`, `../`, etc.)
- All 20 tests passing

**What tests now cover:**
1. ✅ **Relative vs absolute path scenarios** - Tests explicitly verify path resolution logic
2. ✅ **Asset folder derivation** - Bridge's `resolveAssetBasePath()` is tested
3. ✅ **File existence validation** - Validation is tested
4. ✅ **Error scenarios** - Missing files, invalid paths tested
5. ✅ **Path format variations** - Tests for `~/`, `../`, absolute paths, etc.

### Architecture State (Post-Phase 5)

**CLI (Thin Shell):**
- Parses CLI arguments
- Creates SimulationConfig
- Passes raw paths to bridge
- No path resolution logic

**Bridge:**
- Handles all path normalization
- Derives asset base path from script path
- Validates file existence
- Owns all path-related logic (SRP compliance)

**Test Coverage:**
- Comprehensive path resolution tests
- All scenarios covered
- Regression protection for future changes

## Known Issues

### Sync-Pull Crackles - ROOT CAUSE ANALYSIS

**Investigation completed: Physics timing is the root cause**

| Metric | Value |
|--------|-------|
| Audio callback budget | ~10.67ms (471 frames @ 44.1kHz) |
| Physics simulation steps per callback | ~106 (10000Hz × 10.67ms) |
| Physics time per step | ~0.1ms |
| Total physics time | ~10ms (observed: 8-10ms typical) |
| Remaining margin | ~0.6ms (no safety buffer!) |

**Why it fails:**
1. Sync-pull mode: audio callback fires → calls RenderOnDemand → runs physics → returns audio
2. Physics = `simulationFrequency × dt` steps = 10000 × 0.01067 = ~106 steps
3. Each step does: ignition update + 8× fluid simulation (exhaust + intake + chamber)
4. Total: ~10ms, leaving <1ms margin
5. Any spike (GC, thread contention, thermal throttling) → missed deadline → crackle

**Threaded mode works because:**
- Pre-fills 100ms+ buffer (40+ iterations × 735 frames)
- Physics runs in main thread, decoupled from audio callback
- Audio callback just reads from circular buffer

**Build status:**
- ✅ Release build with -O3 (verified in CMakeCache.txt)
- ✅ processingTimeMs telemetry now displayed in CLI output

**Solutions (in priority order):**
1. Use --threaded mode (recommended for production)
2. Add CLI option to reduce simulation frequency (reduces quality)
3. Optimize physics engine (significant work)
4. Add pre-buffer to sync-pull mode (tradeoff: latency vs reliability)

### Config Parameters (in createDefaultConfig)
| Parameter | Current | Valid Range |
|-----------|---------|-------------|
| simulationFrequency | 10000 | 1000-100000 Hz |
| fluidSimulationSteps | 8 | 1-32 |
| targetSynthesizerLatency | 0.02s | Not used in sync-pull |

---

## CLI Thinning Analysis (COMPLETED 2026-04-01)

**IMPORTANT CORRECTION:** The initial plan to "move 20+ files to bridge" was based on a misunderstanding of what the bridge is. After careful analysis, the corrected plan follows.

### Critical Architectural Understanding

**The bridge (`engine-sim-bridge`) is a platform-agnostic C API wrapper around engine-sim.**

The bridge provides:
- `EngineSimCreate/Destroy` - Simulator lifecycle
- `EngineSimLoadScript` - Script loading with path resolution
- `EngineSimUpdate/Render` - Physics and audio generation
- `EngineSimSetThrottle/Ignition/StarterMotor` - Control inputs
- `EngineSimGetStats` - State readout

The bridge should NOT contain:
- Platform-specific audio playback (CoreAudio, AVAudioEngine, I2S)
- Platform-specific input handling (keyboard, touch, CAN bus)
- UI/presentation logic (console, TUI, GUI)

These are **client responsibilities** - different platforms implement them differently.

### Analysis Results

**Dead Code Identified (2 items):**
1. **RPMController.cpp/h** - Defined but NEVER used (verified via grep)
2. **displayHUD()** function in CLIconfig.cpp - Defined but NEVER called (verified via grep)

**NOT Dead Code (must keep):**
- **KeyboardInput.cpp/h** - IS used by KeyboardInputProvider (verified via grep)
- **AudioPlayer.cpp/h** - macOS CoreAudio - platform-specific client code
- **All audio infrastructure** - Platform-specific client code
- **All interfaces** (IInputProvider, IPresentation) - Client-side abstractions
- **EngineConfig.cpp/h** - C++ wrapper convenience over bridge C API
- **SimulationLoop.cpp/h** - Client-side orchestration
- **ILogging.cpp** - Default implementation of ILogging interface

### Corrected File Analysis

| File | Current Location | Should Be | Rationale |
|------|-----------------|-----------|-----------|
| AudioPlayer.cpp/h | src/ | **CLI** | macOS CoreAudio - platform-specific |
| AudioSource.cpp/h | src/ | **CLI** | Wrapper around bridge API |
| CircularBuffer.cpp/h | src/ | **CLI** | Used by AudioPlayer |
| audio/modes/* | src/ | **CLI** | Client strategies for using bridge |
| audio/renderers/* | src/ | **CLI** | Client-side rendering strategies |
| IAudioMode.h | src/audio/modes/ | **CLI** | Client interface |
| IAudioRenderer.h | src/audio/renderers/ | **CLI** | Client interface |
| ILogging.cpp | src/ | **CLI** | Default impl of ILogging |
| interfaces/ConsolePresentation.cpp/h | src/interfaces/ | **CLI** | CLI-specific presentation |
| interfaces/IInputProvider.h | src/interfaces/ | **CLI** | Client interface |
| interfaces/IPresentation.h | src/interfaces/ | **CLI** | Client interface |
| interfaces/KeyboardInputProvider.cpp/h | src/interfaces/ | **CLI** | CLI-specific keyboard input |
| KeyboardInput.cpp/h | src/ | **CLI** | Used by KeyboardInputProvider |
| RPMController.cpp/h | src/ | **DELETE** | DEAD CODE - never used |
| SyncPullAudio.cpp/h | src/ | **CLI** | Client-side sync-pull rendering |
| CLIMain.cpp/h | src/ | **CLI** | Main entry point |
| CLIconfig.cpp/h | src/ | **CLI** | CLI argument parsing |
| SimulationLoop.cpp/h | src/ | **CLI** | Client-side orchestration |
| EngineConfig.cpp/h | src/ | **CLI** | C++ wrapper over bridge C API |
| ConsoleColors.cpp/h | src/ | **CLI** | CLI-specific formatting |
| engine_sim_loader.h | src/ | **CLI** | Bridge API wrapper |

### Revised Execution Plan

**Phase 1 (High Priority):** Delete Dead Code
1. DELETE `RPMController.cpp/h` - NEVER used
2. DELETE `displayHUD()` function from CLIconfig.cpp - NEVER called
3. Run tests, ensure GREEN
4. Commit: "chore: remove dead code (RPMController, displayHUD)"

**Phase 2 (Low Priority, Optional):** SimulationLoop Review
1. Analyze SimulationLoop.cpp (400+ lines) for SRP compliance
2. Current implementation may be acceptable - orchestration is inherently complex
3. Only make changes if clear benefit exists

**Phase 3 (Medium Priority):** Documentation Update
1. Update BRIDGE_INTEGRATION_ARCHITECTURE.md if needed

### What NOT To Do (Critical - Avoid These Mistakes)

**DO NOT move audio infrastructure to bridge:**
- AudioPlayer is macOS-specific (CoreAudio)
- Other platforms need different implementations (iOS: AVAudioEngine, ESP32: I2S)
- Bridge should remain platform-agnostic C API

**DO NOT move input/presentation interfaces to bridge:**
- IInputProvider and IPresentation are client-side abstractions
- Different platforms need different implementations
- Bridge doesn't use these interfaces - CLI does

**DO NOT delete KeyboardInput:**
- It IS used by KeyboardInputProvider (verified via grep)
- It's a valid implementation detail

**DO NOT delete EngineConfig:**
- It provides type-safe C++ interface over bridge C API
- It provides proper error handling with std::string
- It's client-side convenience code, not bridge code

**DO NOT move SimulationLoop to bridge:**
- SimulationLoop is client-side orchestration
- Bridge provides API; client decides how to use it
- Moving it would make bridge into CLI, defeating the purpose

### Success Criteria

1. All dead code removed (RPMController, displayHUD)
2. All tests passing (no regression)
3. Clear separation of concerns maintained
4. Bridge remains platform-agnostic
5. CLI remains appropriately structured
6. SOLID compliance maintained

### Summary

**Initial plan (INCORRECT):** Move 20+ files to bridge
**Corrected plan:** Delete 2 dead files, keep everything else

The CLI is appropriately structured. The bridge is a thin platform-agnostic C API, and the CLI contains platform-specific client code as it should.
