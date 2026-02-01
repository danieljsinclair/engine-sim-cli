# Throttle Investigation: CLI vs GUI

## Executive Summary

**CRITICAL BUG FOUND**: The CLI uses NORMAL throttle logic (0% = idle, 100% = full), but DirectThrottleLinkage uses INVERTED logic (0% = full throttle, 100% = idle). This causes the CLI to have completely backwards throttle behavior.

## Root Cause

### The DirectThrottleLinkage Formula (INVERTED)

From `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/direct_throttle_linkage.cpp:22`:

```cpp
void DirectThrottleLinkage::setSpeedControl(double s) {
    Throttle::setSpeedControl(s);
    m_throttlePosition = 1 - std::pow(s, m_gamma);  // ← INVERTED!
}
```

With `gamma = 2.0` (Subaru EJ25):
- Input 0.0 → Throttle 100%
- Input 0.5 → Throttle 75%
- Input 1.0 → Throttle 0%

**This is OPPOSITE of normal expectations.**

### GUI Behavior (CORRECT)

From `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`:

```cpp
// Line 777: No key pressed → reset to 0.0
m_targetSpeedSetting = fineControlMode ? m_targetSpeedSetting : 0.0;

// User keys:
// Q → 0.01 (idle)
// W → 0.1 (low)
// E → 0.2 (medium)
// R → 1.0 (full)

// Line 798: Smoothing
m_speedSetting = m_targetSpeedSetting * 0.5 + 0.5 * m_speedSetting;

// Line 800: Apply to engine
m_iceEngine->setSpeedControl(m_speedSetting);
```

**GUI Key Mapping:**
- Q (idle): passes 0.01 → DirectThrottleLinkage → 99.99% throttle
- W (low): passes 0.1 → DirectThrottleLinkage → 99% throttle
- E (medium): passes 0.2 → DirectThrottleLinkage → 96% throttle
- No key: passes 0.0 → DirectThrottleLinkage → 100% throttle
- R (full): passes 1.0 → DirectThrottleLinkage → 0% throttle

Wait, that doesn't make sense. Let me recalculate...

Actually, looking at the GUI smoothing more carefully:

**Startup behavior:**
- Initial: m_speedSetting = 1.0, m_targetSpeedSetting = 1.0
- First frame (no key): m_targetSpeedSetting = 0.0
- Smoothing: m_speedSetting = 0.0 * 0.5 + 0.5 * 1.0 = 0.5
- DirectThrottleLinkage(0.5) → 75% throttle

**User presses Q (idle):**
- m_targetSpeedSetting = 0.01
- Frame 1: m_speedSetting = 0.01 * 0.5 + 0.5 * 0.5 = 0.255
  → DirectThrottleLinkage(0.255) → 93.5% throttle
- Frame 2: m_speedSetting = 0.01 * 0.5 + 0.5 * 0.255 = 0.1325
  → DirectThrottleLinkage(0.1325) → 98.2% throttle
- Converges to: DirectThrottleLinkage(0.01) → 99.99% throttle

So the GUI uses **LOW values (0.0-0.2) to get HIGH throttle (75-100%)**.

### CLI Behavior (WRONG)

From `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`:

```cpp
// Line 755: Initial value
double interactiveLoad = (args.targetLoad >= 0) ? args.targetLoad : 0.0;

// Line 860: W key increases load
interactiveLoad = std::min(1.0, interactiveLoad + 0.05);

// Line 865: Space sets to 0
interactiveLoad = 0.0;

// Line 871: R resets to 0.2
interactiveLoad = 0.2;

// Line 923: Apply to engine
throttle = interactiveLoad;
EngineSimSetThrottle(handle, throttle);
```

From `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp:433`:

```cpp
EngineSimResult EngineSimSetThrottle(EngineSimHandle handle, double position) {
    ctx->engine->setSpeedControl(position);  // ← Passes directly
}
```

**CLI Key Mapping:**
- Space (brake): passes 0.0 → DirectThrottleLinkage → 100% throttle
- R (reset): passes 0.2 → DirectThrottleLinkage → 96% throttle
- W (increase): passes 0.8 → DirectThrottleLinkage → 36% throttle
- Max: passes 1.0 → DirectThrottleLinkage → 0% throttle

## The Bug

CLI passes **NORMAL throttle values** (0% = closed, 100% = open), but DirectThrottleLinkage expects **INVERTED values** (0% = open, 100% = closed).

Result:
- User presses W to increase throttle → CLI passes 0.5 → Engine gets 75% throttle
- User presses W more → CLI passes 0.8 → Engine gets 36% throttle ← **THROTTLE DECREASES!**
- User maxes out → CLI passes 1.0 → Engine gets 0% throttle ← **ENGINE SHUTS OFF!**

## Evidence from Symptoms

1. **Engine revving to 6000 RPM**: CLI passes 0.0-0.2, which gives 96-100% throttle
2. **0.00 exhaust flow (no sound)**: When user increases throttle beyond 0.5, actual throttle decreases
3. **Weird throttle response**: Higher input = lower actual throttle (inverted)

## The Fix

CLI needs to **INVERT** throttle values before passing to DirectThrottleLinkage:

```cpp
// In engine_sim_cli.cpp, line 923:
// OLD (WRONG):
throttle = interactiveLoad;

// NEW (CORRECT):
throttle = 1.0 - interactiveLoad;  // Invert for DirectThrottleLinkage
```

Or better, check which throttle system is being used:

```cpp
// For DirectThrottleLinkage (gamma != 1.0):
throttle = 1.0 - interactiveLoad;

// For Governor (normal behavior):
throttle = interactiveLoad;
```

## Default Throttle System

From `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/es/objects/objects.mr:109-111`:

```mr
input throttle_gamma: 1.0;
input throttle:
    direct_throttle_linkage(gamma: throttle_gamma);
```

**ALL engines default to DirectThrottleLinkage with gamma=1.0**.

Subaru EJ25 sets `throttle_gamma: 2.0`, which makes the inversion more severe.

## Test Cases

### Current CLI Behavior (BROKEN)

| User Action | CLI Passes | DirectThrottleLinkage | Actual Throttle | Result |
|-------------|------------|----------------------|-----------------|--------|
| Space (brake) | 0.0 | 1 - 0.0^2 = 1.0 | 100% | Full throttle! |
| R (reset) | 0.2 | 1 - 0.2^2 = 0.96 | 96% | Near full |
| W (press 5x) | 0.5 | 1 - 0.5^2 = 0.75 | 75% | Medium |
| W (press 10x) | 0.8 | 1 - 0.8^2 = 0.36 | 36% | Low |
| W (max) | 1.0 | 1 - 1.0^2 = 0.0 | 0% | Engine dies! |

### Fixed CLI Behavior (CORRECT)

| User Action | CLI Passes | DirectThrottleLinkage | Actual Throttle | Result |
|-------------|------------|----------------------|-----------------|--------|
| Space (brake) | 1.0 | 1 - 1.0^2 = 0.0 | 0% | Correct |
| R (reset) | 0.8 | 1 - 0.8^2 = 0.36 | 36% | Idle-ish |
| W (press 5x) | 0.5 | 1 - 0.5^2 = 0.75 | 75% | Medium |
| W (press 10x) | 0.2 | 1 - 0.2^2 = 0.96 | 96% | High |
| W (max) | 0.0 | 1 - 0.0^2 = 1.0 | 100% | Full power |

## Comparison with Governor

Governor uses NORMAL logic (from `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/governor.cpp:51`):

```cpp
engine->setThrottle(1 - std::pow(1 - m_currentThrottle, m_gamma));
```

With gamma=0.1:
- Input 0.0 → Output 0.0
- Input 0.5 → Output ~0.5
- Input 1.0 → Output 1.0

Governor is NOT inverted, so if an engine used Governor, CLI would work correctly.

## Recommendation

**Fix CLI to invert throttle values for DirectThrottleLinkage engines**:

1. Detect which throttle system the engine uses
2. Invert values if DirectThrottleLinkage
3. Pass normal values if Governor

This will require:
- Adding a query function to get throttle type
- Modifying CLI to check throttle type
- Inverting values when needed

**Quick fix for now**: Always invert (since most engines use DirectThrottleLinkage):

```cpp
throttle = 1.0 - interactiveLoad;
```

## Files Analyzed

- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/direct_throttle_linkage.cpp` - Throttle implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/governor.cpp` - Governor implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp` - GUI throttle handling
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` - CLI throttle handling
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp` - Bridge implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/es/objects/objects.mr` - Default throttle system
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/assets/engines/atg-video-2/01_subaru_ej25_eh.mr` - Subaru EJ25 config
