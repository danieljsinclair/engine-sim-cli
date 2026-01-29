# V8 Engine Configuration Guide

## Overview

This guide documents how to load and run V8 engine configurations with the engine-sim-cli.

## Available V8 Engines

The following V8 engine configurations are available in the engine-sim assets:

1. **Ferrari F136 V8** - High-revving 9000 RPM redline
   - Location: `engine-sim-bridge/engine-sim/assets/engines/atg-video-2/08_ferrari_f136_v8.mr`
   - Characteristics: 90° V8, 4.3L displacement
   - Firing order: 1-5-3-7-4-8-2-6 (cross-plane)

2. **GM LS V8** - Classic American pushrod V8
   - Location: `engine-sim-bridge/engine-sim/assets/engines/atg-video-2/07_gm_ls.mr`
   - Characteristics: 90° V8, LS series
   - Firing order: 1-8-7-2-6-5-4-3 (cross-plane)

## How to Load V8 Engines

### Method 1: Using Pre-configured Test Files (Recommended)

Two test files have been created in the assets directory for easy V8 testing:

```bash
# Ferrari F136 V8
./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/v8_ferrari_test.mr --duration 3 --rpm 4000 --play

# GM LS V8
./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/v8_gm_ls_test.mr --duration 3 --rpm 3000 --play
```

### Method 2: Modifying main.mr

To run a different V8, modify `engine-sim-bridge/engine-sim/assets/main.mr`:

```mr
import "engine_sim.mr"
import "themes/default.mr"
import "engines/atg-video-2/08_ferrari_f136_v8.mr"  // Change this line

use_default_theme()
main()
```

Then run:
```bash
./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --play
```

### Method 3: Using Default Engine

The default engine in main.mr is currently a Subaru EJ25 (flat-4). To use it:

```bash
./build/engine-sim-cli --default-engine --duration 3 --rpm 2500 --play
```

## Common Issues and Solutions

### Issue 1: "Failed to compile script"

**Problem:** .mr files outside the assets directory fail to compile.

**Root Cause:** Import paths in .mr files are relative to the `engine-sim-bridge/engine-sim/assets/` directory. Files outside this location cannot resolve imports correctly.

**Solution:** Always create .mr configuration files within the assets directory, or use the pre-configured test files.

### Issue 2: "Script did not create an engine"

**Problem:** Loading an engine definition file directly (e.g., `08_ferrari_f136_v8.mr`) results in this error.

**Root Cause:** Engine definition files define the engine but don't call `main()` to instantiate it. They need to be imported by a wrapper file.

**Solution:** Use the test wrapper files (v8_ferrari_test.mr, v8_gm_ls_test.mr) which import the engine and call main().

### Issue 3: Local es/ Directory Files Don't Work

**Problem:** Files in `/Users/danielsinclair/vscode/engine-sim-cli/es/` fail to compile.

**Root Cause:** These local copies have incorrect or outdated import paths that don't resolve properly.

**Solution:** Use the official engine configurations in `engine-sim-bridge/engine-sim/assets/engines/` instead.

## Testing Commands

### Quick Test (1 second, no audio save)
```bash
./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/v8_ferrari_test.mr --duration 1 --rpm 4000
```

### Save Audio to File
```bash
./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/v8_ferrari_test.mr --duration 3 --rpm 4000 --output ferrari_v8.wav
```

### Interactive Mode (Real-time Control)
```bash
./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/v8_ferrari_test.mr --interactive --play
```

Controls:
- A: Toggle ignition
- S: Toggle starter
- UP/DOWN or K/J: Throttle
- SPACE: Brake
- R: Reset to idle
- Q/ESC: Quit

## Engine Comparison

| Engine | Redline | Character | Best RPM Range |
|--------|---------|-----------|----------------|
| Ferrari F136 V8 | 9000 RPM | High-revving, exotic | 4000-8500 RPM |
| GM LS V8 | ~6500 RPM | Torquey, American V8 sound | 2000-6000 RPM |
| Subaru EJ25 (default) | ~7000 RPM | Boxer rumble | 2500-6500 RPM |

## Creating Custom Engine Configurations

To create a custom V8 configuration:

1. Copy an existing engine file as a template:
   ```bash
   cp engine-sim-bridge/engine-sim/assets/engines/atg-video-2/08_ferrari_f136_v8.mr \
      engine-sim-bridge/engine-sim/assets/engines/atg-video-2/my_custom_v8.mr
   ```

2. Modify the parameters in your custom file (bore, stroke, cam profiles, etc.)

3. Create a wrapper file:
   ```mr
   import "engine_sim.mr"
   import "engines/atg-video-2/my_custom_v8.mr"

   main()
   ```

4. Run with:
   ```bash
   ./build/engine-sim-cli --script engine-sim-bridge/engine-sim/assets/my_custom_wrapper.mr --interactive --play
   ```

## File Structure Reference

```
engine-sim-bridge/engine-sim/
├── assets/
│   ├── main.mr                          # Main entry point (uses Subaru by default)
│   ├── engine_sim.mr                    # Core engine simulation imports
│   ├── engines/
│   │   ├── atg-video-2/
│   │   │   ├── 07_gm_ls.mr             # GM LS V8
│   │   │   ├── 08_ferrari_f136_v8.mr   # Ferrari F136 V8
│   │   │   ├── 10_lfa_v10.mr           # Lexus LFA V10
│   │   │   ├── 11_merlin_v12.mr        # Merlin V12
│   │   │   └── ...
│   └── themes/
│       └── default.mr
└── es/                                   # Piranha language runtime
```

## Next Steps

For more information on:
- Interactive controls: See `docs/INTERACTIVE_CONTROLS_FIX_SUMMARY.md`
- Testing procedures: See `docs/TESTING_GUIDE.md`
- Diagnostics: See `docs/DIAGNOSTICS_GUIDE.md`
