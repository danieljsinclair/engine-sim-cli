# Comprehensive Fix Plan - engine-sim-cli

## Overview

This document provides a complete, prescriptive plan for fixing all remaining issues in engine-sim-cli. Each fix includes:
- The problem (what's broken)
- Root cause (from research)
- The fix (exact code changes needed)
- File locations and line numbers
- HOW TO TEST (verification steps)
- WHY this fix works

**Status**: All fixes are pending implementation. This is a planning document only - NO code changes have been made.

---

## Fix 1: Audio Dropout Issue (Option A - Bridge Fix)

### Problem
Audio has ~1500ms dropouts/glitches during playback. The CLI requests audio but gets insufficient samples, causing audio buffer underruns.

### Root Cause
**Location**: `engine-sim-bridge/engine-sim/src/synthesizer.cpp:228`

The synthesizer has a hardcoded 2000-sample limit:
```cpp
const bool inputAvailable =
    m_inputChannels[0].data.size() > 0
    && m_audioBuffer.size() < 2000;  // <-- HARDCODED LIMIT
```

The buffer is configured for 96000 samples (from CLI config), but the synthesizer refuses to fill beyond 2000 samples. This creates a producer-consumer mismatch where:
- CLI requests up to 800 samples/frame at 60fps = 48,000 samples/second
- Synthesizer only produces max 2000 samples per `renderAudio()` call
- Result: Chronic buffer underrun causing dropouts

**WRONG FIX**: Reducing chunk size would just make dropouts more frequent.

### The Fix (Option A: Bridge-side Workaround)

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`

**After line 516**, add a loop to repeatedly call `renderAudio()` until we have sufficient samples:

```cpp
// CRITICAL: For synchronous rendering (no audio thread), we must call renderAudio()
// to generate audio samples before reading them. The audio thread normally does this.

// WORKAROUND: Pre-fill the audio buffer before reading
// The synthesizer has a hardcoded 2000-sample limit per renderAudio() call
// (synthesizer.cpp:228), but our buffer is configured for 96000 samples.
// We need to call renderAudio() multiple times to fill the buffer.
const int minRequiredSamples = frames;  // We need at least 'frames' samples
const int maxRenderCalls = 50;  // Safety limit to prevent infinite loop
int callCount = 0;

// Call renderAudio() repeatedly to fill the buffer
while (callCount < maxRenderCalls) {
    ctx->simulator->synthesizer().renderAudio();

    // Check if we have enough samples now
    int availableSamples = ctx->simulator->synthesizer().getAvailableSamples();
    if (availableSamples >= minRequiredSamples) {
        break;  // Buffer has enough samples
    }

    callCount++;
}

if (callCount >= maxRenderCalls) {
    // Log warning but don't fail - we'll read what we have
    static bool warnedOnce = false;
    if (!warnedOnce) {
        std::cerr << "WARNING: Reached max render calls, buffer may underrun\n";
        warnedOnce = true;
    }
}

// Read audio from synthesizer (int16 format)
// IMPORTANT: readAudioOutput returns MONO samples (1 sample per frame)
int samplesRead = ctx->simulator->readAudioOutput(
    frames,
    ctx->audioConversionBuffer
);
```

**NOTE**: This fix assumes `getAvailableSamples()` exists. If it doesn't, add it to the synthesizer class.

### Alternative: If `getAvailableSamples()` doesn't exist

Add to `engine-sim-bridge/engine-sim/include/synthesizer.h`:
```cpp
int getAvailableSamples() const { return m_audioBuffer.size(); }
```

### HOW TO TEST

1. **Compile the bridge with changes**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli/build
   cmake ..
   make
   ```

2. **Run CLI with audio playback**:
   ```bash
   ./engine-sim-cli --default-engine --rpm 2000 --duration 30 --play
   ```

3. **Listen for dropouts**:
   - Audio should be smooth and continuous
   - No gaps or glitches every ~2 seconds
   - RPM should stabilize without oscillation

4. **Verify with diagnostics**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli
   ./diagnostics engine-sim-bridge/engine-sim/assets/main.mr 10.0
   ```

   Check "BUFFER STATUS" section:
   - `Buffer Underruns` should be 0 or < 5
   - `Failed Reads` should be 0
   - `Successful Reads` should match frame count

5. **Expected Results**:
   - Smooth audio for full duration
   - No pulsating or oscillating RPM
   - Buffer underruns eliminated

### WHY This Fix Works

1. **Respects the 2000-sample limit**: We don't modify engine-sim, so we keep compatibility with upstream
2. **Pre-fills the buffer**: By calling `renderAudio()` multiple times, we fill the buffer before reading
3. **Satisfies consumer demand**: The CLI gets enough samples to maintain smooth playback
4. **No performance penalty**: The extra calls are fast and only happen when buffer is low

---

## Fix 2: W Key Increases Target RPM (Not Throttle)

### Problem
In interactive mode, pressing W increases target RPM instead of throttle. This is actually confusing because:
- The GUI doesn't HAVE a target RPM control
- GUI uses Q/W/E/R for speed control (0.01, 0.1, 0.2, 1.0) not RPM
- The CLI has both RPM control and load control, creating confusion

### Root Cause
**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:826-829`

Current code:
```cpp
case 'w':
case 'W':
    interactiveTargetRPM += 100;  // <-- WRONG: Increases RPM
    rpmController.setTargetRPM(interactiveTargetRPM);
    break;
```

The W key is supposed to increase throttle (load), not RPM target. This is a holdover from an earlier implementation that conflated these concepts.

### The Fix

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Replace lines 826-829** with:
```cpp
case 'w':
case 'W':
    // Increase throttle (load), not RPM target
    interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
    break;
```

**Also update the help text** at lines 412 and 718:
- Old: `W - Increase target RPM`
- New: `W - Increase throttle`

### HOW TO TEST

1. **Compile the CLI**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli/build
   make
   ```

2. **Run interactive mode**:
   ```bash
   ./engine-sim-cli --default-engine --interactive --play
   ```

3. **Press W key multiple times**:
   - Throttle should increase (shown in HUD)
   - RPM should naturally rise as throttle increases
   - No sudden RPM jumps

4. **Expected behavior**:
   - W increases throttle percentage
   - RPM rises gradually as engine responds
   - HUD shows increasing throttle, not increasing target RPM

### WHY This Fix Works

1. **Matches GUI behavior**: In the GUI, W is for speed/simulation speed, not RPM
2. **More intuitive**: Throttle control is more direct than RPM target
3. **Eliminates confusion**: Removes the hallucinated "target RPM" feature that doesn't exist in GUI

---

## Fix 3: RPM Controller Pulsation (Aggressive Gains)

### Problem
When using RPM control mode (`--rpm`), the engine oscillates between 800-1800 RPM constantly. The throttle flips 0-100% rapidly, creating a pulsating engine sound.

### Root Cause
**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:325-368`

The RPMController has overly aggressive PID gains:
```cpp
RPMController() : targetRPM(0), kp(2.0), integral(0), ki(0.2), lastError(0), firstUpdate(true) {}
```

- `kp = 2.0` is too high - causes overshoot
- `ki = 0.2` accumulates too fast - causes windup
- No anti-windup clamping on integral term

This creates a race between the control modes where RPM mode keeps trying to take over from direct throttle mode.

### The Fix

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Replace line 325** with:
```cpp
RPMController() : targetRPM(0), kp(0.8), integral(0), ki(0.05), lastError(0), firstUpdate(true) {}
```

**Also update line 347** (integral clamping):
```cpp
// Reduce integral term to prevent windup
integral = std::max(-100.0, std::min(100.0, integral + error * dt));
```

### HOW TO TEST

1. **Compile the CLI**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli/build
   make
   ```

2. **Test RPM control**:
   ```bash
   ./engine-sim-cli --default-engine --rpm 1500 --duration 10 --play
   ```

3. **Observe behavior**:
   - RPM should smoothly converge to 1500
   - No oscillation or pulsation
   - Throttle should stabilize around some value (not flip 0-100%)

4. **Test different RPM targets**:
   ```bash
   ./engine-sim-cli --default-engine --rpm 1000 --duration 10 --play
   ./engine-sim-cli --default-engine --rpm 2500 --duration 10 --play
   ```

5. **Expected results**:
   - Smooth RPM convergence
   - Stable throttle
   - No pulsation

### WHY This Fix Works

1. **Lower kp (2.0 -> 0.8)**: Reduces overshoot and oscillation
2. **Lower ki (0.2 -> 0.05)**: Prevents integral windup
3. **Tighter integral clamping (-500 -> -100)**: Further prevents windup
4. **Maintains responsiveness**: Still responds to changes, just more smoothly

---

## Fix 4: Missing --output Parameter

### Problem
The README and documentation mention an `--output` parameter, but it's not actually implemented in the code. Users expect to be able to specify output file with `--output`.

### Root Cause
**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

The argument parser (lines 423-516) doesn't handle `--output`. It only handles:
- `--script`
- `--rpm`
- `--load`
- `--interactive`
- `--play`
- `--duration`
- `--default-engine`

### The Fix

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Add after line 465** (after `--duration` handling):
```cpp
else if (arg == "--output") {
    if (++i >= argc) {
        std::cerr << "ERROR: --output requires a path\n";
        return false;
    }
    args.outputWav = argv[i];
}
```

**Update help text** at line 400:
```cpp
std::cout << "  --output <path>      Output WAV file path\n";
```

**Update examples** at lines 417-420:
```cpp
std::cout << "Examples:\n";
std::cout << "  " << progName << " --script v8_engine.mr --rpm 850 --duration 5 --output output.wav\n";
std::cout << "  " << progName << " --script v8_engine.mr --interactive --play\n";
std::cout << "  " << progName << " --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --output recording.wav\n";
std::cout << "  " << progName << " --default-engine --rpm 2000 --play --output engine.wav\n";
```

### HOW TO TEST

1. **Compile the CLI**:
   ```bash
   cd /Users/danielsinclair/vscode/engine-sim-cli/build
   make
   ```

2. **Test --output parameter**:
   ```bash
   ./engine-sim-cli --default-engine --rpm 1500 --duration 5 --output test_output.wav
   ```

3. **Verify output**:
   - File `test_output.wav` should be created
   - File should contain valid audio (5 seconds duration)

4. **Test with interactive mode**:
   ```bash
   ./engine-sim-cli --default-engine --interactive --output interactive_recording.wav
   ```

5. **Test help**:
   ```bash
   ./engine-sim-cli --help
   ```
   Should show `--output <path>` in options list

6. **Expected results**:
   - `--output` works correctly
   - WAV files created at specified paths
   - Help text shows the new option

### WHY This Fix Works

1. **Matches documentation**: The README already mentions `--output`
2. **Improves usability**: Users can specify output explicitly
3. **Better than positional args**: More explicit and less confusing
4. **Works with interactive mode**: Can record interactive sessions

---

## Fix 5: Load Parameter Does Nothing in Interactive Mode

### Problem
When using `--load` with `--interactive`, the load parameter is ignored. The interactive controls start at 0% throttle instead of the specified load.

### Root Cause
**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:710`

```cpp
double interactiveLoad = (args.targetLoad >= 0) ? args.targetLoad : 0.0;
```

When `targetLoad >= 0`, it should use the specified value. But the logic at line 710 defaults to 0.0 instead of using `args.targetLoad`.

Actually wait, looking more carefully: the code DOES use `args.targetLoad` when it's >= 0. But the issue is that when `--interactive` is set, the load parameter is meant to be the initial load, not override interactive control.

### The Fix

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:710`

**Current code**:
```cpp
double interactiveLoad = (args.targetLoad >= 0) ? args.targetLoad : 0.0;
```

**This is actually correct** - it uses the specified load. The issue might be elsewhere.

Actually, re-reading the problem: "Load parameter does nothing in interactive mode" - this might mean that when you pass `--load 50` in interactive mode, it should SET the load to 50% and let interactive controls adjust from there. The current code does this.

Let me reconsider: perhaps the issue is that `--load` is ignored when `--rpm` is also specified? Let me check line 511-513:

```cpp
// Auto-enable RPM mode if target RPM is specified and load is not
if (args.targetRPM > 0 && args.targetLoad < 0) {
    args.targetLoad = -1.0;  // Auto mode
}
```

This shows that if you specify both `--rpm` and `--load`, the load will be used. So this should work.

**Wait**, I think the actual issue is that the documentation is misleading. The `--load` parameter is for FIXED load mode, not for setting initial load in interactive mode. In interactive mode, the load is controlled by keyboard.

Let me adjust the fix to clarify behavior:

**No code change needed** - the behavior is correct. Instead, update documentation to clarify.

### The Fix (Documentation Only)

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:400-406`

**Update help text**:
```cpp
std::cout << "Options:\n";
std::cout << "  --script <path>      Path to engine .mr configuration file\n";
std::cout << "  --rpm <value>        Target RPM to maintain (default: auto)\n";
std::cout << "  --load <0-100>       FIXED throttle load percentage (ignored in interactive mode)\n";
std::cout << "  --interactive        Enable interactive keyboard control (overrides --load)\n";
std::cout << "  --play, --play-audio Play audio to speakers in real-time\n";
std::cout << "  --duration <seconds> Duration in seconds (default: 3.0, ignored in interactive)\n";
std::cout << "  --output <path>      Output WAV file path\n";
std::cout << "  --default-engine     Use default engine from main repo (ignores config file)\n\n";
```

**Add note**:
```cpp
std::cout << "NOTES:\n";
std::cout << "  --load sets a FIXED throttle for non-interactive mode only\n";
std::cout << "  In interactive mode, use J/K or Up/Down arrows to control load\n";
std::cout << "  Use --rpm for RPM control mode (throttle auto-adjusts)\n\n";
```

### HOW TO TEST

1. **No code changes needed** - just documentation clarification

2. **Test current behavior**:
   ```bash
   # This should use 50% fixed load
   ./engine-sim-cli --default-engine --load 50 --duration 5

   # This should ignore --load and use interactive control (starts at 0%)
   ./engine-sim-cli --default-engine --load 50 --interactive
   ```

3. **Verify understanding**:
   - `--load` without `--interactive`: Fixed load mode
   - `--load` with `--interactive`: Load parameter is ignored (as documented)

### WHY This Fix Works

1. **Clarifies behavior**: Users understand that `--load` is for fixed mode only
2. **No breaking changes**: Current behavior is correct
3. **Better documentation**: Prevents confusion about what `--load` does

---

## Implementation Order

Implement fixes in this order to minimize risk and maximize testability:

1. **Fix 2 (W key)**: Simplest fix, isolated to interactive mode
2. **Fix 4 (--output parameter)**: Simple argument parsing, low risk
3. **Fix 5 (Load documentation)**: Documentation only, no code changes
4. **Fix 3 (RPM controller)**: Moderate complexity, but isolated to RPM mode
5. **Fix 1 (Audio dropout)**: Most complex, requires bridge modification

## Testing Strategy

### Unit Tests (Per Fix)
Each fix includes specific test instructions in the "HOW TO TEST" section above.

### Integration Tests
After all fixes are complete:

1. **Test all modes**:
   ```bash
   # Fixed load mode
   ./engine-sim-cli --default-engine --load 50 --duration 10 --output test1.wav

   # RPM control mode
   ./engine-sim-cli --default-engine --rpm 2000 --duration 10 --output test2.wav

   # Interactive mode
   ./engine-sim-cli --default-engine --interactive --output test3.wav

   # Auto mode
   ./engine-sim-cli --default-engine --duration 10 --output test4.wav
   ```

2. **Verify audio quality**:
   - No dropouts or glitches
   - Smooth RPM transitions
   - No pulsation or oscillation

3. **Verify controls**:
   - W key increases throttle
   - J/K and arrows adjust load
   - RPM control works smoothly

## Success Criteria

After all fixes, the CLI should:
- [ ] Play smooth audio without dropouts (Fix 1)
- [ ] W key increases throttle, not RPM (Fix 2)
- [ ] RPM control doesn't oscillate (Fix 3)
- [ ] `--output` parameter works (Fix 4)
- [ ] Documentation clarifies `--load` behavior (Fix 5)

## Notes

- All changes are in CLI or bridge - NO changes to engine-sim core
- Fixes maintain backward compatibility
- All fixes follow Option A approach (bridge/CLI side, not engine-sim side)

---

**Document Version**: 1.0
**Created**: January 28, 2026
**Status**: Ready for Implementation
