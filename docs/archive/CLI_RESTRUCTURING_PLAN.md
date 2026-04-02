# CLI Restructuring Plan: Modular Folder Organization

**Document Version:** 1.0
**Date:** 2026-04-01
**Status:** Planning Document (NOT for implementation yet)
**Author:** Solution Architect

---

## Executive Summary

This document outlines a modular folder restructuring plan for the CLI codebase. The current structure mixes interfaces and implementations, making it difficult to identify modular components that could potentially move to the bridge.

**Status:** PLANNING ONLY - This document analyzes the current structure and proposes a reorganization. No implementation should occur until team consensus is reached.

---

## Current State Analysis

### Current Folder Structure

```
src/
├── audio/                    # ✅ Already modular
│   ├── modes/               # Audio mode strategies
│   ├── renderers/           # Audio renderer strategies
│   ├── common/              # Shared audio utilities
│   └── platform/            # Platform-specific audio
├── interfaces/              # ⚠️ Mixed - interfaces + implementations
│   ├── IInputProvider.h     # Interface
│   ├── IPresentation.h      # Interface
│   ├── KeyboardInputProvider.cpp/h  # Implementation
│   └── ConsolePresentation.cpp/h     # Implementation
├── *.cpp/h files            # ❌ Flat structure - hard to navigate
    ├── AudioPlayer.cpp/h
    ├── AudioSource.cpp/h
    ├── SyncPullAudio.cpp/h
    ├── CircularBuffer.cpp/h
    ├── SimulationLoop.cpp/h
    ├── EngineConfig.cpp/h
    ├── CLIconfig.cpp/h
    ├── CLIMain.cpp/h
    ├── ILogging.cpp
    ├── KeyboardInput.cpp/h
    ├── ConsoleColors.cpp/h
    └── engine_sim_loader.h
```

### Problems with Current Structure

1. **Mixed Concerns in `interfaces/` folder:**
   - Contains both interfaces (`IInputProvider.h`, `IPresentation.h`)
   - AND implementations (`KeyboardInputProvider.cpp/h`, `ConsolePresentation.cpp/h`)
   - Violates SRP - folder should contain interfaces OR implementations, not both

2. **Flat Root Structure:**
   - Many files in `src/` root make it hard to find related code
   - Difficult to identify modular candidates
   - No clear separation between presentation, input, logging, simulation, config

3. **Hard to Identify Modular Candidates:**
   - Which files could move to bridge later?
   - Which files are CLI-specific?
   - No visual grouping by concern

---

## Proposed Structure

```
src/
├── presentation/               # Presentation layer
│   ├── IPresentation.h         # Interface
│   └── ConsolePresentation.cpp/h  # Implementation
│
├── logging/                    # Logging layer
│   ├── ILogging.h              # Interface
│   └── StdErrLogging.cpp/h     # Implementation
│
├── input/                       # Input layer
│   ├── IInputProvider.h        # Interface
│   └── KeyboardInputProvider.cpp/h  # Implementation
│
├── audio/                       # Audio layer (✅ Already correct)
│   ├── modes/                  # Audio mode strategies
│   │   ├── IAudioMode.h
│   │   ├── ThreadedAudioMode.cpp/h
│   │   ├── SyncPullAudioMode.cpp/h
│   │   └── AudioModeFactory.cpp/h
│   ├── renderers/              # Audio renderer strategies
│   │   ├── IAudioRenderer.h
│   │   ├── SyncPullRenderer.cpp/h
│   │   ├── CircularBufferRenderer.cpp/h
│   │   └── SilentRenderer.cpp/h
│   ├── common/                 # Shared audio utilities
│   │   ├── IAudioSource.h
│   │   ├── BridgeAudioSource.cpp/h
│   │   └── CircularBuffer.cpp/h
│   └── platform/               # Platform-specific audio
│       ├── IAudioPlatform.h
│       └── macos/
│           └── CoreAudioPlatform.cpp/h
│
├── simulation/                 # Simulation orchestration
│   ├── SimulationLoop.cpp/h    # Main simulation loop
│   └── EngineConfig.cpp/h      # Bridge C++ wrapper
│
├── config/                     # Configuration
│   ├── CLIConfig.cpp/h         # CLI argument parsing
│   └── CLIMain.cpp/h           # Main entry point
│
└── bridge/                      # Bridge API wrapper (NEW)
    └── engine_sim_loader.h     # Bridge C API wrapper
```

---

## Migration Plan (git mv Commands)

### Phase 1: Create New Folders (Dry Run)

```bash
# Create new folder structure
mkdir -p src/presentation
mkdir -p src/logging
mkdir -p src/input
mkdir -p src/simulation
mkdir -p src/config
mkdir -p src/bridge
```

### Phase 2: Move Presentation Files

```bash
# Move presentation interface and implementation
git mv src/interfaces/IPresentation.h src/presentation/
git mv src/interfaces/ConsolePresentation.cpp src/presentation/
git mv src/interfaces/ConsolePresentation.h src/presentation/
```

**After:** `src/presentation/` contains only presentation-related code.

### Phase 3: Move Logging Files

```bash
# Move logging interface and implementation
git mv src/ILogging.cpp src/logging/StdErrLogging.cpp
# Create new header with implementation
# ILogging.h should stay in bridge or move to bridge/include
```

**Note:** `ILogging.h` location needs clarification - it's used by both bridge and CLI.

### Phase 4: Move Input Files

```bash
# Move input interface and implementation
git mv src/interfaces/IInputProvider.h src/input/
git mv src/interfaces/KeyboardInputProvider.cpp src/input/
git mv src/interfaces/KeyboardInputProvider.h src/input/
git mv src/KeyboardInput.cpp src/input/
git mv src/KeyboardInput.h src/input/
```

**After:** `src/input/` contains all input-related code.

### Phase 5: Move Simulation Files

```bash
# Move simulation orchestration
git mv src/SimulationLoop.cpp src/simulation/
git mv src/SimulationLoop.h src/simulation/
git mv src/EngineConfig.cpp src/simulation/
git mv src/EngineConfig.h src/simulation/
```

**After:** `src/simulation/` contains simulation orchestration code.

### Phase 6: Move Config Files

```bash
# Move configuration
git mv src/CLIConfig.cpp src/config/
git mv src/CLIConfig.h src/config/
git mv src/CLIMain.cpp src/config/
git mv src/CLIMain.h src/config/
git mv src/ConsoleColors.cpp src/config/
git mv src/ConsoleColors.h src/config/
```

**After:** `src/config/` contains CLI configuration and main entry point.

### Phase 7: Move Bridge API Wrapper

```bash
# Move bridge API wrapper
git mv src/engine_sim_loader.h src/bridge/
```

**After:** `src/bridge/` contains bridge API wrapper.

### Phase 8: Clean Up Empty Folders

```bash
# Remove empty interfaces folder
rmdir src/interfaces
```

---

## Include Path Updates Required

After restructuring, all include statements need to be updated:

### Before (Current)

```cpp
#include "interfaces/IPresentation.h"
#include "interfaces/ConsolePresentation.h"
#include "interfaces/IInputProvider.h"
#include "interfaces/KeyboardInputProvider.h"
#include "SimulationLoop.h"
#include "EngineConfig.h"
#include "ILogging.h"
```

### After (Proposed)

```cpp
#include "presentation/IPresentation.h"
#include "presentation/ConsolePresentation.h"
#include "input/IInputProvider.h"
#include "input/KeyboardInputProvider.h"
#include "simulation/SimulationLoop.h"
#include "simulation/EngineConfig.h"
#include "logging/ILogging.h"  // or "bridge/ILogging.h" depending on location
```

---

## Modular Candidates for Bridge Extraction

Based on the proposed structure, these modules are candidates for potential bridge extraction:

### High Priority (Core Bridge Concerns)

| Module | Location | Bridge Candidate? | Rationale |
|--------|----------|-------------------|-----------|
| **EngineConfig** | `src/simulation/EngineConfig.cpp/h` | ✅ YES | C++ wrapper over bridge C API - already bridge-adjacent |
| **engine_sim_loader** | `src/bridge/engine_sim_loader.h` | ✅ YES | Bridge API wrapper - bridge concern |
| **ILogging** | `src/logging/` | ⚠️ MAYBE | Used by bridge and CLI - shared concern |

### Medium Priority (Platform-Specific)

| Module | Location | Bridge Candidate? | Rationale |
|--------|----------|-------------------|-----------|
| **AudioPlayer** | `src/audio/platform/macos/CoreAudioPlatform.cpp/h` | ❌ NO | Platform-specific - belongs in platform layer |
| **CircularBuffer** | `src/audio/common/CircularBuffer.cpp/h` | ❌ NO | Client-side utility - platform-specific buffering |

### Low Priority (CLI-Specific)

| Module | Location | Bridge Candidate? | Rationale |
|--------|----------|-------------------|-----------|
| **SimulationLoop** | `src/simulation/SimulationLoop.cpp/h` | ❌ NO | Client-side orchestration - CLI-specific |
| **CLIConfig** | `src/config/CLIConfig.cpp/h` | ❌ NO | CLI argument parsing - CLI-specific |
| **CLIMain** | `src/config/CLIMain.cpp/h` | ❌ NO | Main entry point - CLI-specific |
| **IPresentation** | `src/presentation/IPresentation.h` | ❌ NO | Client-side abstraction - CLI-specific |
| **IInputProvider** | `src/input/IInputProvider.h` | ❌ NO | Client-side abstraction - CLI-specific |

---

## SOLID Compliance Assessment

### Current Structure

| Principle | Status | Issues |
|-----------|--------|--------|
| **SRP** | ⚠️ PARTIAL | `interfaces/` folder mixes interfaces and implementations |
| **OCP** | ✅ GOOD | Strategy pattern allows extension |
| **LSP** | ✅ GOOD | Interface contracts honored |
| **ISP** | ✅ GOOD | Focused interfaces |
| **DIP** | ✅ GOOD | Dependency injection used |

### Proposed Structure

| Principle | Status | Improvement |
|-----------|--------|-------------|
| **SRP** | ✅ GOOD | Each folder has single responsibility |
| **OCP** | ✅ GOOD | Unchanged |
| **LSP** | ✅ GOOD | Unchanged |
| **ISP** | ✅ GOOD | Unchanged |
| **DIP** | ✅ GOOD | Unchanged |

---

## Migration Strategy

### Step 1: Dry Run (Recommended First)

1. Create the proposed folder structure in a test branch
2. Run `git mv` commands to verify all paths
3. Update all include statements
4. Verify build succeeds
5. Review changes with team

### Step 2: Team Consensus

Before implementing:
- [ ] Solution Architect review
- [ ] Test Architect review (include path impacts)
- [ ] Tech Architect review (CMake impacts)
- [ ] Team lead approval

### Step 3: Implementation (If Approved)

1. Create feature branch: `git checkout -b folder-restructure`
2. Execute git mv commands in order
3. Update all include statements
4. Update CMakeLists.txt if needed
5. Verify build succeeds
6. Run all tests
7. Commit with clear message: "refactor: reorganize CLI into modular folder structure"

### Step 4: Rollback Strategy

If issues arise:
```bash
git reset --hard HEAD~1  # Undo the restructure commit
```

---

## CMakeLists.txt Updates Required

The CMakeLists.txt will need updates for the new folder structure:

### Before (Current)

```cmake
add_executable(escli
    src/CLIMain.cpp
    src/SimulationLoop.cpp
    src/EngineConfig.cpp
    src/AudioPlayer.cpp
    # ... etc
)
target_include_directories(escli PRIVATE src)
```

### After (Proposed)

```cmake
add_executable(escli
    src/config/CLIMain.cpp
    src/simulation/SimulationLoop.cpp
    src/simulation/EngineConfig.cpp
    src/audio/platform/macos/CoreAudioPlatform.cpp
    # ... etc
)
target_include_directories(escli PRIVATE
    src
    src/config
    src/simulation
    src/presentation
    src/logging
    src/input
    src/bridge
    src/audio
)
```

---

## Success Criteria

1. ✅ All files moved with git mv (history preserved)
2. ✅ Build succeeds with new folder structure
3. ✅ All tests pass
4. ✅ No broken include paths
5. ✅ Folder structure clearly separates concerns
6. ✅ Modular candidates for bridge extraction are obvious

---

## Open Questions

1. **ILogging.h location:** Should it be in `src/logging/` or `engine-sim-bridge/include/`?
   - Currently used by both bridge and CLI
   - Needs clarification on bridge vs CLI ownership

2. **CircularBuffer location:** Currently in `src/audio/common/` - should it move?
   - Used by AudioPlayer (platform-specific)
   - Could move to `src/audio/platform/macos/` if only used there

3. **AudioSource location:** Currently in `src/` root - should it move?
   - Related to bridge API
   - Could move to `src/audio/common/` or `src/bridge/`

4. **SyncPullAudio location:** Currently in `src/` root - should it move?
   - Related to sync-pull rendering
   - Could move to `src/audio/renderers/` or `src/audio/modes/`

---

## Appendix: Complete File Inventory

### Current File Locations (47 files)

| File | Current Location | Proposed Location |
|------|------------------|-------------------|
| **Presentation** |||
| IPresentation.h | `src/interfaces/` | `src/presentation/` |
| ConsolePresentation.cpp | `src/interfaces/` | `src/presentation/` |
| ConsolePresentation.h | `src/interfaces/` | `src/presentation/` |
| **Logging** |||
| ILogging.cpp | `src/` | `src/logging/StdErrLogging.cpp` |
| ILogging.h | `engine-sim-bridge/include/` | Keep or move to `src/logging/` |
| **Input** |||
| IInputProvider.h | `src/interfaces/` | `src/input/` |
| KeyboardInputProvider.cpp | `src/interfaces/` | `src/input/` |
| KeyboardInputProvider.h | `src/interfaces/` | `src/input/` |
| KeyboardInput.cpp | `src/` | `src/input/` |
| KeyboardInput.h | `src/` | `src/input/` |
| **Simulation** |||
| SimulationLoop.cpp | `src/` | `src/simulation/` |
| SimulationLoop.h | `src/` | `src/simulation/` |
| EngineConfig.cpp | `src/` | `src/simulation/` |
| EngineConfig.h | `src/` | `src/simulation/` |
| **Config** |||
| CLIConfig.cpp | `src/` | `src/config/` |
| CLIConfig.h | `src/` | `src/config/` |
| CLIMain.cpp | `src/` | `src/config/` |
| CLIMain.h | `src/` | `src/config/` |
| ConsoleColors.cpp | `src/` | `src/config/` |
| ConsoleColors.h | `src/` | `src/config/` |
| **Bridge** |||
| engine_sim_loader.h | `src/` | `src/bridge/` |
| **Audio (Already Modular)** |||
| audio/modes/* | `src/audio/modes/` | No change |
| audio/renderers/* | `src/audio/renderers/` | No change |
| audio/common/* | `src/audio/common/` | No change |
| audio/platform/* | `src/audio/platform/` | No change |
| **Audio (To Sort)** |||
| AudioPlayer.cpp | `src/` | `src/audio/platform/macos/CoreAudioPlatform.cpp` |
| AudioPlayer.h | `src/` | `src/audio/platform/macos/CoreAudioPlatform.h` |
| AudioSource.cpp | `src/` | `src/audio/common/BridgeAudioSource.cpp` |
| AudioSource.h | `src/` | `src/audio/common/BridgeAudioSource.h` |
| SyncPullAudio.cpp | `src/` | `src/audio/modes/SyncPullAudioMode.cpp` (merge?) |
| SyncPullAudio.h | `src/` | `src/audio/modes/SyncPullAudioMode.h` (merge?) |
| CircularBuffer.cpp | `src/` | Already in `src/audio/common/` |
| CircularBuffer.h | `src/` | Already in `src/audio/common/` |

---

*End of CLI Restructuring Plan*

**Next Steps:** Review with team, address open questions, get consensus before implementation.
