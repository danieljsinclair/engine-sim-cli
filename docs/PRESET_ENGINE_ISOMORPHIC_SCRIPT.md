# Preset Engine: Isomorphic .mr/.JSON via --script

## Goal

Compile `.mr` engine scripts to JSON at build time. CLI accepts both formats
through `--script` — auto-detects by file extension. No hand-coded engine configs.
JSON presets work on iOS/ESP32 without Piranha.

## Architecture

```
Build time:  .mr scripts --> preset_compiler --> .json files
Runtime:     --script foo.mr   -> Piranha compilation (desktop)
             --script foo.json -> JSON preset load (any platform)
```

Factory routes by file extension in `SimulatorFactory::create()` — no separate
flags, no enum changes.

## Design Rationale

The original plan had C++ codegen from .mr scripts with a separate `--preset` flag.
Replaced with JSON because:

- Hand-coded C++ engines drifted from .mr script values (GM LS crackled)
- The JSON pipeline (`preset_compiler` -> `PresetEngineFactory`) already existed and passed 16/16 tests
- One `--script` flag accepting both formats is simpler than two flags
- JSON allows dynamic loading without binary rebuild

### On isomorphism

`.mr` is syntactically imperative (sequential initialization calls) but functionally
declarative for engine setup (no branching, no user input during configuration).
The `preset_compiler` runs Piranha, captures the resulting engine-sim object state,
and serializes it to JSON. This is compilation (imperative -> data), not transpilation.
Round-trip `.mr -> JSON -> .mr` is not possible because the JSON captures
post-execution state, not the procedural instructions.

What we guarantee:
- **Deterministic compilation**: same .mr always produces the same JSON
- **Behavioral equivalence**: both paths produce identical audio/physics output
  (verified by Group 6 golden file tests, RMS tolerance < 1%)

## Status

### Phase 1: Fix existing preset tests — DONE
- ConvolutionFilter null-deref fix (SimulatorInitHelpers)
- Intake air mixture initialization
- Flow function unit mismatch
- 16/16 JSON pipeline tests pass

### Phase 2: Public API getters in engine-sim — DONE
- 29 read-only accessors added to engine-sim submodule

### Phase 3: JSON preset compiler — DONE
- `engine-sim-preset-compiler` tool generates .json from .mr
- `PresetEngineFactory::loadFromFile()` loads JSON at runtime
- Verified: Honda (1 cyl), Subaru (4 cyl), GM LS (8 cyl)

### Phase 4: Isomorphic --script — DONE
- Extension-based routing in `SimulatorFactory.cpp` (.json vs .mr)
- `SimulatorType` enum reverted to SineWave + PistonEngine
- Hand-coded engine classes removed (PresetSimulator, HondaTrx520, etc.)
- EnginePresetsHelper namespace retained (shared helpers)
- 28/28 preset tests pass
- End-to-end verified: `--script gm_ls.json` loads and runs without Piranha

### Phase 5: Build integration — TODO
- [ ] Makefile `presets` target to auto-generate .json from .mr scripts
- [ ] Generate JSON for all engines in `engine-sim/assets/engines/`
- [ ] iOS build: include JSON presets, exclude Piranha target

## Files Modified

### SimulatorFactory.cpp — extension routing
- `endsWith(scriptPath, ".json")` routes to `PresetEngineFactory::loadFromFile()`
- Else: Piranha compilation (existing path, unchanged)

### EnginePresets.cpp/h — gutted
- Removed: PresetSimulator, HondaTrx520Simulator, SubaruEj25Simulator, GmLsSimulator
- Removed: EnginePresets::createPreset(), getAvailablePresets()
- Retained: EnginePresetsHelper namespace (shared helpers used by PresetEngineFactory)

### CLI files — restored to pre-preset state
- `CLIconfig.h`: no presetId field
- `CLIconfig.cpp`: no --preset flag
- `CLIMain.cpp`: no PresetEngine routing

### PresetEngineTests.cpp — updated
- Removed Group 8 (hardcoded preset registry tests)
- Group 9 updated: uses JSON fixture paths with SimulatorType::PistonEngine
- 28/28 tests pass

## Build Commands

```bash
make                    # Build everything
make test               # Run smoke + unit tests

# JSON preset (no Piranha needed at runtime):
./build/engine-sim-cli --script path/to/engine.json --duration 5

# .mr script (Piranha compilation at runtime):
./build/engine-sim-cli --script es/ferrari_f136.mr --duration 5

# Generate JSON from .mr:
./build/engine-sim-bridge/engine-sim-preset-compiler \
    assets/engines/atg-video-2/07_gm_ls.mr output.json \
    /path/to/engine-sim-root
```
