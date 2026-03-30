# Bridge Architecture Review & Static Linking Plan

**Date:** 2026-03-27
**Status:** Architecture Decision Record

## Executive Summary

After reviewing the bridge implementation and comparing with the reference escli codebase, I confirm that:

1. **The bridge IS properly wrapping engine-sim** (not modifying its API)
2. **A reference implementation exists** showing the correct static linking approach
3. **The current architecture can be improved** by migrating from dynamic loading to static linking

## Current Architecture (escli.refac7)

### Bridge Implementation: CORRECT

The bridge (`engine-sim-bridge/src/engine_sim_bridge.cpp`) properly wraps engine-sim:

```
┌─────────────────────────────────────────────────────────┐
│                  escli (CLI)                           │
│  - Uses engine_sim_loader.h with dlopen()              │
│  - Loads libenginesim.dylib dynamically                │
│  - Function pointers to EngineSim* API                 │
└────────────────────┬────────────────────────────────────┘
                     │ dlopen()
                     ▼
┌─────────────────────────────────────────────────────────┐
│         libenginesim.dylib (bridge)                    │
│  ┌─────────────────────────────────────────────────┐   │
│  │  engine_sim_bridge.cpp                          │   │
│  │  - Exports C API (EngineSimCreate, Render, etc)│   │
│  │  - Wraps engine-sim C++ classes internally     │   │
│  └────────────────────┬────────────────────────────┘   │
│                       │                                 │
│  ┌────────────────────▼────────────────────────────┐   │
│  │  engine-sim (statically linked)                │   │
│  │  - PistonEngineSimulator                       │   │
│  │  - Engine, Vehicle, Transmission               │   │
│  │  - Synthesizer                                 │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### Issues with Current Approach

1. **Fragile dynamic loading** - Requires dlopen/dlsym
2. **No compile-time type checking** - Errors only at runtime
3. **Function pointer indirection** - Adds complexity
4. **Path resolution issues** - DYLD_LIBRARY_PATH dependencies

## Reference Implementation (escli)

**Location:** `/Users/danielsinclair/vscode/escli/escli` (uncommitted changes)

### Key Changes

#### 1. Direct Function Calls (engine_sim_loader.h)

```cpp
// OLD: Function pointer indirection
struct EngineSimAPI {
    PFN_EngineSimCreate Create;
    // ... requires dlsym to populate
};

// NEW: Direct calls
struct EngineSimAPI {
    EngineSimResult Create(const EngineSimConfig* config, EngineSimHandle* outHandle) const {
        return EngineSimCreate(config, outHandle);
    }
    // ... all functions called directly
};
```

#### 2. No Dynamic Loading (CLIMain.cpp)

```cpp
// OLD: Load library at runtime
EngineSimAPI engineAPI = {};
if (!LoadEngineSimLibrary(engineAPI, useMock)) {
    return 1;
}

// NEW: Static linking
EngineSimAPI engineAPI = {};  // Just use it, no loading
```

#### 3. Clean C++ Interfaces

Reference escli has proper interfaces:
- `IInputProvider` - Abstract input sources (keyboard, upstream)
- `IPresentation` - Abstract output (console, TUI, headless)

## Migration Plan

### Phase 1: Port Reference Implementation

**Files to port from reference escli:**

1. **src/engine_sim_loader.h**
   - Replace dynamic loading with direct function calls
   - Remove dlopen/dlsym complexity
   - Keep C API wrapper for now

2. **src/interfaces/** (already exists in refac7)
   - IInputProvider.h
   - IPresentation.h
   - Implementations already present

3. **src/CLIMain.cpp**
   - Remove LoadEngineSimLibrary() call
   - Remove UnloadEngineSimLibrary() call
   - Simplify initialization

### Phase 2: Bridge as C++ Interface (Future)

Long-term: Expose C++ interface instead of C API:

```cpp
// Future: IEngineProvider interface
class IEngineProvider {
public:
    virtual void Initialize(const EngineConfig& config) = 0;
    virtual void SetThrottle(double position) = 0;
    virtual void SetIgnition(bool enabled) = 0;
    virtual void Update(double deltaTime) = 0;
    virtual void Render(float* buffer, int frames) = 0;
    virtual EngineStats GetStats() const = 0;
};

class RealEngineProvider : public IEngineProvider {
    // Wraps engine-sim C++ classes directly
};

class MockEngineProvider : public IEngineProvider {
    // Sine wave implementation
};
```

## Benefits of Static Linking

1. **Compile-time type safety** - All errors caught at build
2. **Simpler deployment** - Single binary, no library path issues
3. **Better testing** - Easy to swap implementations
4. **SOLID compliance** - Proper DI with C++ interfaces
5. **Performance** - No indirection, linker can optimize

## Action Items

- [ ] Review reference implementation in `/Users/danielsinclair/vscode/escli/escli`
- [ ] Port `engine_sim_loader.h` static linking version
- [ ] Update `CLIMain.cpp` to remove dynamic loading
- [ ] Update CMakeLists.txt for static linking
- [ ] Test with both real and mock engines
- [ ] Update documentation to reflect new architecture

## Documentation Updates

- [x] BRIDGE_INTEGRATION_ARCHITECTURE.md - Updated with static linking decision
- [x] ARCHITECTURE_TODO.md - Added static linking task

---

*End of Bridge Architecture Review*
