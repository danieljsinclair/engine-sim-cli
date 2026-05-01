# Plan: Eradicate engine_sim_bridge.* from All Repos

## Findings

### Three files exist

| # | File | Location | Role |
|---|------|----------|------|
| 1 | `engine-sim-bridge/engine-sim/include/engine_sim_bridge.h` | engine-sim upstream submodule (fork at github.com/danieljsinclair/engine-sim) | OLD C API header -- `EngineSimHandle`, `EngineSimResult` enum, C function declarations. Hardcodes 48kHz. Last touched in commit `6ff7ebd` (dr_wav integration). |
| 2 | `engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp` | engine-sim upstream submodule | OLD C API implementation -- `EngineSimContext`, `EngineSimCreate/Destroy/Render/Update`, raw `PistonEngineSimulator*` usage. 821 lines of dead code. |
| 3 | `engine-sim-bridge/include/simulator/engine_sim_bridge.h` | bridge layer (our code) | ACTIVE header -- defines `EngineSimConfig`, `EngineSimStats`, `EngineSimDefaults`, `EngineSimAudio` namespace. This is the **single source of truth** for all simulation constants. Used by ~15 files across the bridge. |

### Critical distinction

Files 1 & 2 (upstream submodule) are the OLD C API for .NET P/Invoke -- completely superseded by ISimulator. They are dead code.

File 3 (bridge layer) is NOT dead. It defines types and constants still in active use:
- `EngineSimConfig` -- used by ISimulator, BridgeSimulator, SimulatorFactory, SimulationLoop, engine_sim_capi.h
- `EngineSimStats` -- used by ISimulator, BridgeSimulator, SimulationLoop, engine_sim_capi.h
- `EngineSimDefaults` -- used by SimulationLoop, ThreadedStrategy, SimulatorFactory
- `EngineSimAudio` -- used by BridgeSimulator, SyncPullStrategy, SimulationLoop

### What references file 3 (`include/simulator/engine_sim_bridge.h`)

**Bridge headers (7 files):**
- `include/simulator/ISimulator.h` -- needs `EngineSimConfig`, `EngineSimStats`
- `include/simulator/BridgeSimulator.h` -- needs `EngineSimConfig`, `EngineSimStats`
- `include/simulator/SimulatorFactory.h` -- needs `EngineSimDefaults` comment
- `include/simulator/engine_sim_capi.h` -- needs `EngineSimConfig`, `EngineSimStats`
- `include/hardware/IAudioHardwareProvider.h` -- needs `EngineSimAudio`?
- `include/simulation/SimulationLoop.h` -- needs `EngineSimDefaults::SAMPLE_RATE`
- `include/strategy/AudioLoopConfig.h` -- comment reference
- `include/strategy/ThreadedStrategy.h` -- needs `EngineSimDefaults`
- `include/strategy/SyncPullStrategy.h` -- needs `EngineSimAudio`

**Bridge source (5 files):**
- `src/simulation/SimulationLoop.cpp` -- `EngineSimConfig`, `EngineSimStats`, `EngineSimAudio`
- `src/simulator/ISimulator.cpp` -- `EngineSimConfig`
- `src/simulator/SineSimulator.cpp` -- `EngineSimConfig`
- `src/simulator/SimulatorFactory.cpp` -- `EngineSimConfig`, `EngineSimDefaults`
- `src/strategy/SyncPullStrategy.cpp` -- `EngineSimAudio`

**Tests (2 files):**
- `test/BridgeUnitTests.cpp`
- `test/iOSAdapterTests.cpp`

### What references files 1 & 2 (upstream)

**CMakeLists.txt references:**
- `engine-sim-bridge/engine-sim/CMakeLists.txt` lines 217, 220, 243 -- lists `engine_sim_bridge.cpp`/`.h` in upstream lib sources
- `engine-sim-bridge/CMakeLists.txt` line 140 -- `include/simulator/engine_sim_bridge.h` as public header

**engine_sim_bridge.cpp** itself includes the upstream header at line 1: `#include "../include/engine_sim_bridge.h"`. This is the upstream header, NOT the bridge one.

### engine_sim_capi.h status

`engine_sim_capi.h` exists at `include/simulator/engine_sim_capi.h`. It's a C API wrapper that references `EngineSimConfig`/`EngineSimStats` from the bridge's `engine_sim_bridge.h`. Zero files include it. It is dead code -- safe to delete alongside or separately.

### iOS app

The iOS app has zero references to `engine_sim_bridge` or `engine_sim_capi`. It was already migrated to ISimulator (task #10).

## Removal Plan

### Phase 1: Delete the upstream copies (files 1 & 2) -- SAFE NOW

These are dead code. No bridge code includes or links against them.

**Delete:**
- `engine-sim-bridge/engine-sim/include/engine_sim_bridge.h`
- `engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp`

**Update CMakeLists.txt:**
- `engine-sim-bridge/engine-sim/CMakeLists.txt`:
  - Remove `src/engine_sim_bridge.cpp` from source list (line ~217)
  - Remove `include/engine_sim_bridge.h` from header list (line ~220)
  - Remove `PUBLIC_HEADER include/engine_sim_bridge.h` (line ~243)

### Phase 2: Delete engine_sim_capi.h -- SAFE NOW

Zero consumers.

**Delete:**
- `engine-sim-bridge/include/simulator/engine_sim_capi.h`

### Phase 3: Rename the bridge's engine_sim_bridge.h -- REQUIRES REFACTORING

The bridge's `include/simulator/engine_sim_bridge.h` must be preserved (it has the types), but the filename `engine_sim_bridge` is confusing since the C API is gone. The file should be renamed to something that reflects its actual contents: config structs, constants, and audio utilities.

**Option A: Rename to `EngineSimTypes.h`** (recommended)
- Rename `include/simulator/engine_sim_bridge.h` -> `include/simulator/EngineSimTypes.h`
- Update all 14 `#include "simulator/engine_sim_bridge.h"` -> `#include "simulator/EngineSimTypes.h"`
- Update `engine-sim-bridge/CMakeLists.txt` line 140: `PUBLIC_HEADER include/simulator/EngineSimTypes.h`
- Optionally split: move `EngineSimAudio` to its own `AudioUtils.h`, move `EngineSimDefaults` to its own `EngineSimDefaults.h`

**Option B: Split into multiple headers**
- `EngineSimConfig.h` -- struct `EngineSimConfig`
- `EngineSimStats.h` -- struct `EngineSimStats`
- `EngineSimDefaults.h` -- namespace `EngineSimDefaults`
- `EngineSimAudio.h` -- namespace `EngineSimAudio`
- Update all includes to target the specific header they need
- This is cleaner but more churn. Recommend Option A first, split later if desired.

### Phase 4: Verify build

After each phase:
```bash
make clean && make
make test
```

## Execution Order

1. Phase 1 (delete upstream copies + CMakeLists) -- low risk, no code changes
2. Phase 2 (delete engine_sim_capi.h) -- zero consumers, zero risk
3. Phase 3 (rename bridge header) -- moderate churn, 14 file updates
4. `make clean && make && make test` after each phase
