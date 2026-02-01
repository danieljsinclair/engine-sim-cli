# Engine Startup Fix - Summary

## Problem
The engine won't start in the CLI. RPM stays at ~0 despite throttle being applied (0.5 → 0.7 → 0.8).

## Root Cause
**The starter motor is NOT enabled during the warmup phase.**

### Evidence

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

**Lines 782-804 (Warmup loop)**:
```cpp
while (currentTime < warmupDuration) {
    // Throttle: 0.5 → 0.7 → 0.8
    EngineSimSetThrottle(handle, warmupThrottle);
    EngineSimUpdate(handle, updateInterval);
    // NO starter motor enabled here!
}
```

**Line 811 (Starter motor enabled AFTER warmup)**:
```cpp
EngineSimSetStarterMotor(handle, 1);
```

### Why This Causes the Problem

1. **During warmup (lines 782-804)**:
   - Throttle is applied (0.5, 0.7, 0.8)
   - `EngineSimUpdate()` is called
   - **Starter motor is disabled (default state)**
   - Result: Engine sits at 0 RPM because there's nothing to turn the crankshaft

2. **Physics explanation**:
   - Starter motor is a physics constraint that applies torque to the crankshaft
   - When `m_enabled = false`, it applies 0 torque
   - With 0 torque and initial velocity of 0, the crankshaft never rotates
   - Without rotation: no air intake, no compression, no combustion

3. **After warmup (line 811)**:
   - Starter motor is finally enabled
   - But by now, 2 seconds have been wasted with the engine doing nothing

## The Fix

**Move the starter motor enable to BEFORE the warmup loop:**

```cpp
// CRITICAL: Enable starter motor BEFORE warmup!
// Without the starter motor, the engine cannot crank
EngineSimSetStarterMotor(handle, 1);

// Initial warmup - fixed throttle ramp to start combustion
const double warmupDuration = 2.0;
std::cout << "Starting warmup sequence...\n";
while (currentTime < warmupDuration) {
    // ... rest of warmup code ...
}
```

**Location**: Move line 811 to before line 782

## Why This Works

1. Starter motor is enabled before warmup begins
2. During warmup, the starter motor cranks the engine (applies torque to crankshaft)
3. Crankshaft rotates → air intake → compression → combustion
4. Throttle controls how much air/fuel mixture enters during cranking
5. Engine RPM increases as combustion adds torque
6. Once RPM is high enough (~550 RPM), starter motor can be disabled

## Code Locations

### Starter Motor Implementation
- **File**: `engine-sim-bridge/engine-sim/src/starter_motor.cpp`
- **Lines 37-44**: Shows that when `m_enabled = false`, torque limits are 0.0

### Physics Update
- **File**: `engine-sim-bridge/engine-sim/src/simulator.cpp`
- **Line 110**: `m_system->process(timestep, 1)` processes all constraints including starter motor

### Starter Motor Initialization
- **File**: `engine-sim-bridge/engine-sim/src/piston_engine_simulator.cpp`
- **Lines 178-181**: Starter motor is connected to crankshaft and added to physics system

## Additional Notes

### GUI Comparison
The GUI uses manual starter motor control via the 'S' key. The user holds 'S' to crank the engine. The CLI attempts to automate this but has a bug in the sequence.

### Governor vs DirectThrottleLinkage
The Subaru EJ25 uses DirectThrottleLinkage with gamma=2.0, which applies a nonlinear throttle curve. This is working correctly and is not related to the startup issue.

### Other Systems
- Ignition: Enabled correctly at line 711
- Audio thread: Started correctly at line 702
- Throttle: Applied correctly during warmup

## Verification

To verify the fix:
1. Move `EngineSimSetStarterMotor(handle, 1)` from line 811 to before line 782
2. Recompile the CLI
3. Run the CLI and observe RPM during warmup
4. Expected: RPM should increase from 0 to ~550 RPM during warmup as the starter motor cranks the engine

## Impact

This is a **critical bug** that prevents the engine from starting at all. The fix is simple (one line move) and should completely resolve the issue.
