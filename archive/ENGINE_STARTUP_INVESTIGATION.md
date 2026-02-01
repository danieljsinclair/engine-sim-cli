# Engine Startup Investigation Report

## Problem Statement
The engine simulator CLI is hanging because the engine won't start - RPM stays at ~0 despite the starter motor being enabled and throttle being applied (0.5 → 0.7 → 0.8).

## Investigation Findings

### 1. Starter Motor Implementation

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/starter_motor.cpp`

```cpp
void StarterMotor::calculate(Output *output, atg_scs::SystemState *state) {
    // ... setup Jacobian matrices ...

    if (m_rotationSpeed < 0) {
        output->limits[0][0] = m_enabled ? -m_maxTorque : 0.0;
        output->limits[0][1] = 0.0;
    }
    else {
        output->limits[0][0] = 0.0;
        output->limits[0][1] = m_enabled ? m_maxTorque : 0.0;
    }
}
```

**Key Finding**: The starter motor is a physics constraint that applies torque to the crankshaft when `m_enabled = true`.

### 2. Starter Motor Initialization

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/piston_engine_simulator.cpp:178-181`

```cpp
m_starterMotor.connectCrankshaft(m_engine->getOutputCrankshaft());
m_starterMotor.m_maxTorque = m_engine->getStarterTorque();
m_starterMotor.m_rotationSpeed = -m_engine->getStarterSpeed();
m_system->addConstraint(&m_starterMotor);
```

**Key Finding**: The starter motor is properly connected and added to the physics system during initialization.

### 3. CLI Startup Sequence - THE BUG

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp:779-811`

```cpp
// Initial warmup - fixed throttle ramp to start combustion
const double warmupDuration = 2.0;  // Longer warmup for combustion
std::cout << "Starting warmup sequence...\n";
while (currentTime < warmupDuration) {
    EngineSimStats stats = {};
    EngineSimGetStats(handle, &stats);

    // Use sufficient throttle during warmup to ensure strong combustion
    // Higher airflow allows more fuel/air mixture for combustion torque
    double warmupThrottle;
    if (currentTime < 1.0) {
        warmupThrottle = 0.5;  // Medium throttle for initial start
    } else if (currentTime < 1.5) {
        warmupThrottle = 0.7;  // Higher throttle for combustion development
    } else {
        warmupThrottle = 0.8;  // High throttle ready for starter disable
    }

    EngineSimSetThrottle(handle, warmupThrottle);
    EngineSimUpdate(handle, updateInterval);
    currentTime += updateInterval;

    if (static_cast<int>(currentTime * 2) % 2 == 0) {  // Print every 0.5 seconds
        std::cout << "  Warmup: " << stats.currentRPM << " RPM, Throttle: " << warmupThrottle << "\n";
    }
}
std::cout << "Warmup complete. Starting main simulation...\n";

currentTime = 0.0;

// CRITICAL: Enable starter motor to start the engine!
// Without this, the engine never cranks and never generates audio
EngineSimSetStarterMotor(handle, 1);
```

**CRITICAL BUG IDENTIFIED**:

1. **Lines 782-804**: Warmup phase runs for 2.0 seconds
2. During warmup: Throttle is applied (0.5 → 0.7 → 0.8)
3. During warmup: `EngineSimUpdate()` is called (line 798)
4. **During warmup: Starter motor is NOT enabled!**
5. **Line 811**: Starter motor is enabled AFTER warmup completes

**Why this causes the problem**:
- Without the starter motor enabled, the crankshaft cannot rotate
- Without crankshaft rotation, there's no air intake, no compression, no combustion
- The engine just sits at 0 RPM despite throttle being applied
- After 2 seconds of "warmup" (which does nothing), the starter is finally enabled
- But by then, the expectation is that combustion should have already started

### 4. GUI Startup Sequence

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp:859-865`

```cpp
const bool prevStarterEnabled = m_simulator->m_starterMotor.m_enabled;
if (m_engine.IsKeyDown(ysKey::Code::S)) {
    m_simulator->m_starterMotor.m_enabled = true;
}
else {
    m_simulator->m_starterMotor.m_enabled = false;
}
```

**Key Finding**: The GUI uses the 'S' key to manually control the starter motor. The user holds 'S' to crank the engine.

**Critical Difference**: In the GUI, the user can manually enable the starter motor at any time, while the CLI has a flawed automatic sequence.

### 5. Physics Update Sequence

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/simulator.cpp:98-156`

```cpp
bool Simulator::simulateStep() {
    if (getCurrentIteration() >= simulationSteps()) {
        return false;
    }

    const double timestep = getTimestep();
    m_system->process(timestep, 1);  // Process physics constraints (including starter motor)

    m_engine->update(timestep);      // Update engine (throttle, ignition)
    m_vehicle->update(timestep);
    m_transmission->update(timestep);

    updateFilteredEngineSpeed(timestep);

    // ... crankshaft angle tracking ...

    simulateStep_();                 // Process combustion, fluid dynamics
    writeToSynthesizer();            // Write exhaust flow to audio synthesizer

    ++m_currentIteration;
    return true;
}
```

**Key Finding**: The physics system processes all constraints (including starter motor) during `m_system->process(timestep, 1)`.

**If `m_enabled = false`**: The starter motor constraint applies 0 torque (see `starter_motor.cpp:38`)

**If `m_enabled = true`**: The starter motor constraint applies up to `m_maxTorque` to rotate the crankshaft

## Root Cause Analysis

**The engine won't start because**:

1. The CLI warmup phase (lines 782-804) applies throttle WITHOUT enabling the starter motor
2. Without the starter motor, the crankshaft cannot rotate (0 RPM)
3. Without rotation, there's no air intake, no compression, no combustion
4. The "warmup" is completely ineffective - it's just applying throttle to a stationary engine
5. After 2 seconds of useless warmup, the starter is finally enabled (line 811)
6. But the expected behavior is that the engine should have been cranking DURING warmup

## Evidence

**From the code**:
- Line 798: `EngineSimUpdate(handle, updateInterval)` - called during warmup
- Lines 789-795: Throttle values 0.5, 0.7, 0.8 applied during warmup
- **Line 811**: `EngineSimSetStarterMotor(handle, 1)` - starter enabled AFTER warmup
- Lines 782-804: No call to `EngineSimSetStarterMotor()` during warmup loop

**From physics**:
- `starter_motor.cpp:38`: When `m_enabled = false`, torque limits are set to 0.0
- `simulator.cpp:110`: `m_system->process(timestep, 1)` processes all constraints
- If starter motor is disabled during warmup, it applies 0 torque to the crankshaft
- With 0 torque and initial velocity of 0, the crankshaft never moves

## The Fix

**Move the starter motor enable to BEFORE the warmup loop**:

```cpp
// CRITICAL: Enable starter motor FIRST, before warmup!
// Without the starter motor, the engine cannot crank and cannot start
EngineSimSetStarterMotor(handle, 1);

// Initial warmup - fixed throttle ramp to start combustion
const double warmupDuration = 2.0;
std::cout << "Starting warmup sequence...\n";
while (currentTime < warmupDuration) {
    // ... warmup code ...
    EngineSimSetThrottle(handle, warmupThrottle);
    EngineSimUpdate(handle, updateInterval);  // Now this will actually crank the engine!
    // ...
}

// Continue with main simulation loop
```

**Why this fixes it**:
1. Starter motor is enabled before warmup begins
2. During warmup, the starter motor cranks the engine (applies torque to crankshaft)
3. Crankshaft rotates → air intake → compression → combustion
4. Throttle controls how much air/fuel mixture enters during cranking
5. Engine RPM increases as combustion adds torque to the starter motor torque
6. Once RPM is high enough, starter motor can be disabled

## Additional Findings

### Governor vs DirectThrottleLinkage

**From TA2 findings**: The Subaru EJ25 uses DirectThrottleLinkage with gamma=2.0

**Location**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/direct_throttle_linkage.cpp`

```cpp
void DirectThrottleLinkage::update(double dt, Engine *engine) {
    Throttle::update(dt, engine);

    // Apply gamma curve to throttle
    // actual = 1 - (1 - input)^gamma
    engine->m_throttleValue = 1.0 - std::pow(1.0 - m_value, m_gamma);
}
```

**Calculation for gamma=2.0**:
- Input 0.5: actual = 1 - 0.5^2 = 1 - 0.25 = 0.75 (75%)
- Input 0.8: actual = 1 - 0.8^2 = 1 - 0.64 = 0.36 (36%)

**This is correct behavior** - it's a nonlinear throttle curve that provides more control at low throttle openings.

### Ignition System

**Location**: CLI line 711

```cpp
EngineSimSetIgnition(handle, 1);
```

**Finding**: Ignition is enabled automatically before the simulation loop. This is correct.

### Audio Thread

**Location**: CLI line 702

```cpp
result = EngineSimStartAudioThread(handle);
```

**Finding**: Audio thread is started correctly. This matches the GUI architecture.

## Conclusion

**The engine won't start because the starter motor is not enabled during the warmup phase.**

The CLI attempts to "warmup" the engine with throttle applied but NO starter motor. This is physically impossible - an engine cannot start without cranking (rotating the crankshaft). The starter motor must be enabled BEFORE and DURING the warmup phase for the engine to crank and start combustion.

**The fix is simple**: Move `EngineSimSetStarterMotor(handle, 1)` from line 811 to before the warmup loop (before line 782).

**Additional note**: The CLI also hangs while loading impulse responses, but that's a separate issue from the engine startup problem.
