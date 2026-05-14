# Preset Engine: Build-Time .mr → C++ Compilation

## Goal

Compile `.mr` engine definition scripts into static C++ preset classes at build time.
Desktop: Piranha runtime + presets. iOS/ESP32: presets only, zero Piranha.

## Status Tracker

### Phase 1: Fix existing preset tests — DONE
- [x] ConvolutionFilter null-deref fix (SimulatorInitHelpers)
- [x] Intake air mixture initialization (21% O2, 79% N2)
- [x] Flow function unit mismatch (filter radius 0 → 50 thou)
- [x] Throttle inversion (DirectThrottleLinkage semantics)
- [x] Simulation frequency mismatch (10000 vs 40000)
- [x] Piranha crash in SimulatorFactory (scope guard for compiler)
- [x] **Result: 16/16 tests pass, 2 golden skipped**

### Phase 2: Public API getters in engine-sim — DONE
- [x] Function: getSampleCount, getX, getY, getFilterRadius, getInputScale, getOutputScale
- [x] Transmission: getGearCount, getGearRatio, getMaxClutchTorque
- [x] Intake: getVolume, getInputFlowK, getIdleFlowK, getIdleThrottlePlatePosition
- [x] CylinderHead: getIntakePortFlow, getExhaustPortFlow
- [x] ExhaustSystem: getOutletFlowRate
- [x] Camshaft: getLobeCount
- [x] IgnitionModule: getTimingCurve, getRevLimit, getLimiterDuration, getCylinderCount, getFiringOrder
- **29 lines total, all additive, no logic changes to 3rd party code**

### Phase 3: .mr → C++ codegen tool — DONE
- [x] `tools/preset_codegen.cpp` — builds and runs
- [x] Reads actual values from Piranha-compiled engine (not hardcoded)
- [x] Emits: engine params, crankshafts, exhaust, intake, cylinder banks, camshafts,
      port flows, fuel, ignition timing/firing order, combustion chambers, vehicle, transmission
- [x] Air mixture initialization in generated code
- [x] Per-cylinder intake/exhaust wiring (reads from compiled engine, not counts)
- [x] Bank-indexed variable names (no collisions on multi-bank engines)
- [x] CMakeLists.txt: `engine-sim-preset-codegen` target
- **Verified: Honda (1 cyl), Subaru (4 cyl V-flat), Ferrari F136 (8 cyl V8) all compile and run**

### Phase 4: Wire presets into CLI — NOT STARTED
- [ ] Add `PresetEngine` to `SimulatorType` enum
- [ ] Register codegen output classes in bridge (preset registry)
- [ ] `SimulatorFactory::create()` handles PresetEngine type (no Piranha)
- [ ] CLI `--preset ferrari_f136` flag
- [ ] Header display: `Script:` (cyan) vs `Preset:` (purple)
- [ ] Conditional compilation: iOS builds get presets only, no Piranha

### Phase 5: CMake bulk generation — NOT STARTED
- [ ] `generate_preset()` CMake function for custom commands
- [ ] Auto-generate from all `.mr` scripts in engine-sim/assets/engines/
- [ ] Generated `.cpp` files added to bridge library sources
- [ ] iOS build: include generated presets, exclude Piranha target

## Files Changed (uncommitted)

### engine-sim (3rd party submodule) — minimal getters only
```
include/camshaft.h           +1
include/cylinder_head.h      +3
include/exhaust_system.h     +1
include/function.h           +7
include/ignition_module.h    +6
include/intake.h             +4
include/transmission.h       +3
src/ignition_module.cpp      +4
```

### engine-sim-bridge (our code)
```
.gitignore                                build artifact patterns
CMakeLists.txt                            preset-codegen target
include/simulator/SimulatorInitHelpers.h  convolution filter helper
include/simulator/EnginePresetsHelper.h   NEW — shared DRY helpers
src/simulator/SimulatorInitHelpers.cpp    convolution filter impl
src/simulator/EnginePresets.cpp           air mix, filter radius fixes
src/simulator/PresetEngineFactory.cpp     DRY refactor, correctness fixes
src/simulator/SimulatorFactory.cpp        Piranha crash fix
src/simulator/EnginePresetsHelper.h       NEW — shared helper header
tools/preset_codegen.cpp                  NEW — .mr → C++ codegen
test/PresetEngineTests.cpp                test fixes (16/16 pass)
```

## What's Left for Phase 4 (CLI Integration)

The codegen tool produces standalone C++ classes with `static Simulator* create()`.
The CLI needs a way to select and instantiate these without Piranha.

1. **Preset registry** — map of name → factory function
2. **SimulatorFactory** — new `PresetEngine` branch that looks up from registry
3. **CLI flag** — `--preset ferrari_f136` selects from registry
4. **Display** — label as "Preset:" (purple) vs "Script:" (cyan)
5. **Conditional build** — `#ifdef ATG_ENGINE_SIM_PIRANHA_ENABLED` gates script path

## Build Commands

```bash
# Full build from root (only way to build)
make

# Tests
make test

# Run CLI with Piranha script
./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr
```
