# Plan: Extract `escli-ios` into standalone `engine-sim-app` repo

## Context

The iOS app (`escli-ios/`) lives inside the `engine-sim-cli` CLI repo but is a distinct product. The goal is to:

1. Create a new `engine-sim-app` repo containing the iOS app (renamed from `escli-ios`)
2. Make `engine-sim-cli` a submodule of the new repo (for its `engine-sim-bridge` static libraries)
3. Preserve git history from `engine-sim-cli` for the iOS-relevant files

**Note**: `vehicle-sim` stays in `engine-sim-cli` — it will be integrated with the bridge and is needed by the main project too. The new repo gets it transitively via the `engine-sim-cli` submodule.

## Current Dependencies (what the iOS app needs from outside `escli-ios/`)

| Dependency | Location | Used By |
|---|---|---|
| `src/ios/IOSSimulator.{h,mm}` | Tracked in engine-sim-cli | Xcode compiles directly (path: `../../../src/ios/`) |
| `engine-sim-bridge/include/` | Submodule of engine-sim-cli | Header search paths (`../../engine-sim-bridge/include`) |
| `engine-sim-bridge` static libs | Built by CMake from engine-sim-cli | Xcode links `libenginesim.a`, `libengine-sim.a`, etc. |
| `ios.toolchain.cmake` | Root of engine-sim-cli | `escli-ios/Makefile` references `../../ios.toolchain.cmake` |
| Root `CMakeLists.txt` | Root of engine-sim-cli | `escli-ios/Makefile` does `cmake ../..` (has iOS `return()` early-exit logic) |

**vehicle-sim**: Stays in `engine-sim-cli` as a submodule (to be integrated with bridge later). Accessible transitively via the engine-sim-cli submodule.

## Target Structure

```
engine-sim-app/                         # New repo (github.com/danieljsinclair/engine-sim-app)
├── EngineSimApp/                       # Renamed from escli-ios/EngineSimApp/
│   ├── EngineSimApp.xcodeproj/
│   ├── EngineSimWrapper.h/.mm
│   ├── EngineSimViewModel.swift
│   ├── ContentView.swift
│   ├── EngineSimAppApp.swift
│   ├── EngineSim-Bridging-Header.h
│   ├── Assets.xcassets/
│   ├── Engines/
│   ├── IOSSimulator.h                  # Moved from src/ios/ (co-located with consumer)
│   ├── IOSSimulator.mm                 # Moved from src/ios/
│   └── Info.plist
├── engine-sim-cli/                     # Submodule (current root repo)
│   ├── engine-sim-bridge/              # Nested submodule (unchanged)
│   ├── vehicle-sim/                    # Stays here (not moved)
│   ├── CMakeLists.txt
│   ├── ios.toolchain.cmake
│   └── ... (CLI source, tests, etc.)
├── Makefile                            # Top-level: builds engine-sim-cli for iOS, then Xcode
└── .gitmodules                         # Just engine-sim-cli
```

## Ownership Model

**Today**: CLI repo is root, delegates `make ios`/`make xcode` down to `escli-ios/Makefile`.

**After**: App repo is root, delegates C++ library builds down to `engine-sim-cli/Makefile`. The app repo's Makefile orchestrates:
- Building `engine-sim-cli` for iOS (cmake with iOS toolchain) → produces static libs
- Optionally opening Xcode to build the app against those libs - by default xcode should build headlessly using CI command line xcode approach. only the "xcode" makefile target will actually launch the xcode GUI.
- Cascading `clean`/`scrub`/`test` into the submodule

A fresh clone of `engine-sim-app` + `git submodule update --init --recursive` + `make` = full build.

## Steps

### Phase 1: Create the new repo with history

**Goal**: Prime `engine-sim-app` with git history from `engine-sim-cli` for the iOS-relevant files.

**Approach**: `git filter-repo` (recommended over `git filter-branch` — faster, safer, actively maintained).

```bash
# 1. Clone engine-sim-cli as the new repo
git clone https://github.com/danieljsinclair/engine-sim-cli.git engine-sim-app
cd engine-sim-app

# 2. Install git-filter-repo if needed
brew install git-filter-repo

# 3. Filter to keep only iOS-relevant paths, with history
git filter-repo \
  --path escli-ios/ \
  --path src/ios/ \
  --path ios.toolchain.cmake \
  --path-rename escli-ios/: \
  --path-rename src/ios/:ios-src/ \
  --force
```

This produces a repo containing:
- `EngineSimApp/` (was `escli-ios/EngineSimApp/`) — with history
- `Makefile` (was `escli-ios/Makefile`) — with history (will be replaced)
- `merge_libs.sh` (was `escli-ios/merge_libs.sh`) — with history
- `ios-src/IOSSimulator.{h,mm}` (was `src/ios/`) — with history
- `ios.toolchain.cmake` — with history (will be deleted after submodule provides it)
- Everything else is pruned, but commit history for these paths is preserved

**Note**: `git filter-repo` rewrites commit hashes. The history is preserved in terms of diffs and messages, but SHA references change. This is inherent to any history-filtering approach.

### Phase 2: Restructure files

1. **Move `IOSSimulator.{h,mm}` into `EngineSimApp/`** — co-locate with the Xcode project that compiles them
   ```
   git mv ios-src/IOSSimulator.h EngineSimApp/
   git mv ios-src/IOSSimulator.mm EngineSimApp/
   rm -rf ios-src/
   ```

2. **Remove stale files** — the old `Makefile` (from `escli-ios/Makefile`), `merge_libs.sh`, build dirs, and root-level `ios.toolchain.cmake` (the submodule provides its own copy)

3. **Commit** the restructuring

### Phase 3: Add submodule

```bash
# Add engine-sim-cli as a submodule (includes engine-sim-bridge and vehicle-sim transitively)
git submodule add https://github.com/danieljsinclair/engine-sim-cli.git engine-sim-cli
git submodule update --init --recursive
```

### Phase 4: Update build paths

**New top-level `Makefile`** (the app repo's build orchestrator):
- Builds engine-sim-bridge static libs for iOS by running cmake against `engine-sim-cli/` as source dir
- Uses `engine-sim-cli/ios.toolchain.cmake` as the toolchain
- Cascades `clean`/`scrub`/`test` into the submodule via `$(MAKE) -C engine-sim-cli <target>`
- Opens Xcode after building libs

```makefile
# Key targets
all: build-iphoneos build-iphonesimulator          # Build static libs for both platforms
xcode: all                                         # Build libs + open Xcode
clean:                                              # Cascade clean
    $(MAKE) -C engine-sim-cli clean
    rm -rf build-iphoneos build-iphonesimulator
scrub:                                              # Cascade scrub
    $(MAKE) -C engine-sim-cli scrub
    rm -rf build-iphoneos build-iphonesimulator
test:                                               # Run CLI/bridge tests
    $(MAKE) -C engine-sim-cli test

build-iphoneos:
    cmake -S engine-sim-cli -B build-iphoneos \
        -DCMAKE_TOOLCHAIN_FILE=engine-sim-cli/ios.toolchain.cmake \
        -DPLATFORM=OS64 ...
    $(MAKE) -C build-iphoneos engine-sim-bridge
```

**Xcode project (`project.pbxproj`)**:
- `HEADER_SEARCH_PATHS`: Change `../../engine-sim-bridge/include` → `../engine-sim-cli/engine-sim-bridge/include`
- `HEADER_SEARCH_PATHS`: Remove `../../../src` (IOSSimulator now local)
- Source file refs for IOSSimulator.h/.mm: change from `../../../src/ios/` to local paths
- `LIBRARY_SEARCH_PATHS`: Update to point at new build output locations

**IOSSimulator.mm** includes:
- `#include "simulation/SimulationRunner.h"` — resolves via header search path to `engine-sim-cli/engine-sim-bridge/include/`
- `#include "simulator/SimulatorFactory.h"` — same

**EngineSimWrapper.mm** includes:
- `#include "ios/IOSSimulator.h"` → `#include "IOSSimulator.h"` (now same directory)

### Phase 5: Clean up engine-sim-cli

After extraction, remove the iOS-specific files from `engine-sim-cli`:
- Delete `escli-ios/` directory
- Delete `src/ios/` directory
- Update root `Makefile` (remove `ios`/`xcode` targets)
- Update root `CMakeLists.txt` (remove `if(IOS)` early return if no longer needed, or keep for standalone iOS lib builds)
- `vehicle-sim` submodule stays in engine-sim-cli (no changes needed)

### Phase 6: Create GitHub repo and push

```bash
# Create the new repo on GitHub
gh repo create danieljsinclair/engine-sim-app --private --source=. --push
```

## What `git filter-repo` gives you

- Every commit that ever touched `escli-ios/`, `src/ios/`, or `ios.toolchain.cmake` is preserved
- The diffs, commit messages, authors, and dates are intact
- Only the relevant file paths appear in each commit (other paths are stripped)
- Commit SHAs are rewritten (unavoidable with any filtering approach)

**Alternative approaches considered**:
- `git subtree split` — only works for a single path prefix, can't combine `escli-ios/` + `src/ios/`
- Manual cherry-pick — extremely tedious for many commits
- Clone-and-delete — preserves full history but the new repo contains all CLI history too (confusing, not a clean separation)

## Verification

1. **History check**: `git log --oneline EngineSimApp/` — should show iOS-relevant commits
2. **Build check**: `make all` — builds engine-sim-bridge static libs for both iOS platforms
3. **Xcode check**: `make xcode` — opens Xcode, project builds without missing headers
4. **Submodule check**: `git submodule status` — shows engine-sim-cli (with nested engine-sim-bridge + vehicle-sim)
