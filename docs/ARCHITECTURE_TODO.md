# Architecture Refactoring TODO

## Status: IN PROGRESS

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

## In Progress
- [ ] Sample amplitude transform (multiply float samples by gain factor)
- [ ] iOS audio: AudioUnit alternative (AVAudioEngine?)

## SOLID Requirements

### SRP: CLI vs SimulationLoop
- CLIMain.cpp: Parses CLI args, creates SimulationConfig, resolves paths
- SimulationLoop.cpp: ONLY knows about SimulationConfig, NOT CommandLineArgs

### SimulationConfig struct
- configPath: Engine config path (resolved before passing)
- assetBasePath: Asset base path (resolved before passing)  
- duration, interactive, playAudio, volume, sineMode, syncPull  // temp boolean - prefer DI
- targetRPM, targetLoad, useDefaultEngine, outputWav
- audioMode: Injected (OCP compliance)

### GetSimulationConfig (in CLIMain.cpp)
- Converts CommandLineArgs → SimulationConfig
- Resolves configPath/assetBasePath using EngineConfig
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
- DRY: ⚠️ **ISSUES FOUND** (see below)
- YAGNI: ⚠️ **DEAD CODE** (see below)

### SOLID Pedant Analysis (2026-03-27)

#### ✅ SOLID Compliance (Good)
- **SRP**: `EngineConfig`, `SimulationLoop`, `AudioPlayer` each have single responsibilities
- **OCP**: Strategy pattern via `IAudioMode` and `IAudioRenderer` allows extension without modification
- **DIP**: High-level modules depend on abstractions (`IAudioMode`, `IInputProvider`, `IPresentation`)
- **DI**: Dependencies are properly injected throughout

#### ❌ Critical Issues Found

**1. WET VIOLATION - Path Resolution Duplication**

The same path resolution logic exists in TWO places:

- **Location A**: `src/SimulationLoop.cpp:171-207` (`resolveConfigPath()` function)
- **Location B**: `src/EngineConfig.cpp:83-117` (`resolveAssetBasePath()` method)

Both functions do identical work:
- Convert to absolute paths
- Handle "assets"/"es" parent directories
- Resolve asset base paths

**Fix Required**: Remove `resolveConfigPath()` from `SimulationLoop.cpp`, use `EngineConfig::resolveConfigPaths()` instead.

**2. DEAD CODE - Debug Conditional**

`src/SimulationLoop.cpp:391-414` contains:
```cpp
bool useConfigScript = true;  // Set to false to use old LoadScript method
if (useConfigScript) {
    // NEW METHOD
} else {
    // OLD METHOD (never executed)
}
```

The `else` branch is dead code that will never execute in production.

**Fix Required**: Remove the conditional and dead code. Use only the new method.

**3. PATH RESOLUTION NOT CALLED**

`EngineConfig::resolveConfigPaths()` exists but is **never called** in the happy path. The CLI passes raw user input to the bridge, which expects absolute paths.

**Fix Required**: Call `EngineConfig::resolveConfigPaths()` in `CreateSimulationConfig()` or in `runSimulation()` before calling `createDefaultWithScript()`.

**4. STATIC STORAGE LIFETIME ISSUE**

`src/EngineConfig.cpp:32-35` uses static storage for path strings:
```cpp
static std::string scriptPathStorage;
static std::string assetBasePathStorage;
```

**Issues**:
- Only one simulator instance supported
- Thread safety violations
- Subsequent calls overwrite previous paths

**Fix Required**: Use `SimulationConfig` strings instead of static storage.

#### 📊 SOLID Scorecard

| Principle | Status | Notes |
|-----------|--------|-------|
| SRP | ✅ PASS | Good separation |
| OCP | ✅ PASS | Strategy pattern |
| LSP | ✅ PASS | Interface contracts |
| ISP | ✅ PASS | Focused interfaces |
| DIP | ✅ PASS | Dependencies inverted |
| **DRY** | ❌ **FAIL** | **Path resolution duplicated** |
| **YAGNI** | ❌ **FAIL** | **Dead conditional code** |

#### 🎯 Action Items

- [ ] Remove `resolveConfigPath()` from `SimulationLoop.cpp`
- [ ] Delete `useConfigScript` conditional and dead code
- [ ] Call `EngineConfig::resolveConfigPaths()` before Create
- [ ] Replace static storage with `SimulationConfig` strings

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

## Debug Output Consolidation

### All RPM/Throttle Output Locations (MUST FIX):
| File | Function | Status |
|------|----------|--------|
| AudioSource.cpp | SineAudioSource::displayProgress | ✅ Frequency |
| AudioSource.cpp | EngineAudioSource::displayProgress | ✅ Flow |
| CLIConfig.cpp | displayHUD() | Legacy - TODO remove |
| ConsolePresentation.cpp | ShowEngineState() | Needs consolidating |

### Fix Required:
- Remove displayHUD() from CLIConfig.cpp
- Move all engine state output to ConsolePresentation
- audioSource.displayProgress() already handles both modes correctly

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
