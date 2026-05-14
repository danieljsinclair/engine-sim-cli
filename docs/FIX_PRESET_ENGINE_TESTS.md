# Plan: Fix Preset Engine Tests (Silent Audio / Near-Zero RPM)

## Context

4 of 14 preset engine tests fail: the 2 `PresetAudioIdleTest` and 2 `PresetThrottleTest` variants.
Both Honda and Subaru presets load correctly but produce near-zero RPM (~0.0002) and completely
silent audio output. The engine doesn't start.

**This is NOT an engine-sim bug.** The CLI works perfectly with Piranha scripts. The issue is
entirely in `PresetEngineFactory.cpp` (our bridge code) which reconstructs engines from JSON.

## Timeline: When It Was Working vs Broken

### escli.refac7 (main repo)
- `96a5183` - Latest: iOS app wired to load presets
- `05d5783` - Updated bridge submodule with preset factories
- Everything before `05d5783` was working (CLI only, no preset tests existed)

### engine-sim-bridge (submodule)
- `16b900a` - **Latest**: Fixed ignition timing, added default exhaust outlet flow rate
- `9e9dd20` - First commit: added PresetEngineFactory + PresetEngineTests

**The preset tests were NEVER green.** They were introduced in `9e9dd20` and have been
iterated on (`f8eefc1`, `bddf4f3`, `16b900a`) but the 4 audio/throttle tests have always
failed. The 10 infrastructure/creation/shutdown tests pass.

### engine-sim (nested submodule)
- `859aa4f` - Latest: added `renderAudioOnDemand()` and combustion chamber init in `loadSimulation()`
- This is the third-party engine-sim library - working fine with Piranha/CLI

## Root Cause Analysis

The CLI/Piranha path works because the Piranha script interpreter sets up the engine
in the correct order using `engine_node.h::generate()`:
1. Creates crankshafts, cylinder banks, pistons, rods, heads
2. Connects rod assemblies
3. Generates ignition module
4. Initializes combustion chambers
5. Then `loadSimulation()` calls `placeAndInitialize()` which places pistons physically
   and re-initializes `m_system` with correct volume

The PresetEngineFactory does the SAME sequence (steps 1-4 in `loadFromString()`),
and `loadSimulation()` does step 5. So the initialization order is NOT the issue.

**The actual problem is likely in the JSON data or missing defaults.** Key areas to investigate:

### 1. Crankshaft `rodJournals` array
In `PresetEngineFactory.cpp:199`: `csParams.rodJournals = journalCount` counts from JSON
but `Crankshaft::initialize()` uses `rodJournals` to allocate journal arrays. The JSON
has `"rodJournals": [{"angle": 0}]` but the factory reads the array only for angles, not
for count. The count comes from `journalCount = journals.isArray() ? journals.size() : 0`.
This looks correct for Honda (1 journal).

### 2. Starter motor direction
`piston_engine_simulator.cpp:180`: `m_rotationSpeed = -m_engine->getStarterSpeed()`.
Honda's starterSpeed is 52.36 rad/s. Negative means CW. The starter constraint
sets `limits[0][0] = -m_maxTorque` when `m_rotationSpeed < 0`, which means it can only
apply torque in the negative theta direction. This should work.

### 3. Throttle state
Test calls `setSpeedControl(1.0)` which maps to `engine->setSpeedControl(1.0)`.
This goes through `DirectThrottleLinkage` which sets `m_throttlePosition = 1 - 1 = 0`
which means throttle plate OPEN. The intake's `getThrottlePlatePosition()` returns
`m_idleThrottlePlatePosition * m_throttle` = `0.993 * 0 = 0`, so `flowAttenuation = cos(0) = 1`.
Air should flow freely. This looks correct.

### 4. The `while(simulateStep())` loop
The test correctly uses `while (simulator_->simulateStep()) {}` to exhaust all substeps.

### 5. Combustion check - `ignite()` prerequisites
Looking at `combustion_chamber.cpp:176`:
- Needs `m_system.mix().p_fuel != 0` - requires fuel in the mix
- Needs equivalence ratio between 0.5 and 1.9
- The intake must have provided fuel-air mixture

**Hypothesis: The intake gas system isn't providing the right fuel-air mix.** The intake
calls `m_system.flow()` which flows gas from atmosphere to plenum. The fuel-air mix is
constructed in `Intake::process()` based on `m_molecularAfr`. Looking at the JSON:
`"molecularAfr": 12.5`. The factory sets `fParams.molecularAfr = fuelJson["molecularAfr"].numberOr(14.7)`.
This is 12.5 from JSON. But `Fuel::initialize()` stores this. The combustion check uses
`m_fuel->getMolecularAfr()`.

The real question: **is the intake even flowing?** The intake's `InputFlowK` is derived
from `runnerFlowRate` (0.00637) when no `inputFlowK` is in JSON. That should be non-zero.

### 6. Most likely root cause: `CombustionChamber::flow()` access to uninitialized data

Looking at `combustion_chamber.cpp:257-305`: The `flow()` method accesses:
- `m_head->getIntake(cylinderIndex)` → returns Intake*
- `m_head->getExhaustSystem(cylinderIndex)` → returns ExhaustSystem*

These are wired in `PresetEngineFactory.cpp:434-445`. For Honda (single bank, single cylinder),
`head->setAllIntakes(engine->getIntake(0))` and `head->setExhaustSystem(0, engine->getExhaustSystem(0))`.

**But the flow rate calculations depend on:**
- `m_manifoldToRunnerFlowRate` = `intake->getRunnerFlowRate()` (0.00637 from JSON)
- `m_primaryToCollectorFlowRate` = `exhaust->getPrimaryFlowRate()` (0.00637 from JSON)
- `m_intakeFlowRate` = `m_head->intakeFlowRate(cylinderIndex)` - depends on camshaft/valve
- `m_exhaustFlowRate` = `m_head->exhaustFlowRate(cylinderIndex)` - depends on camshaft/valve

The camshaft lobe profile IS in the JSON. The valve lift depends on camshaft rotation
which depends on crankshaft rotation. At near-zero RPM, valve lift is essentially
determined by the cam's base position.

### 7. SIMULATION FREQUENCY MISMATCH
**THIS IS THE SMOKING GUN.** Look at the JSON:
```json
"simulationFrequency": 40000
```

The factory sets `params.initialSimulationFrequency = engineJson["simulationFrequency"].numberOr(10000)`.
But in the test harness (`PresetEngineTests.cpp:105`):
```cpp
pistonSim->setSimulationFrequency(10000);
```

And in `PresetEngineFactory.cpp:163`:
```cpp
params.initialSimulationFrequency = engineJson["simulationFrequency"].numberOr(10000);
```

The test overrides to 10000 but the JSON says 40000. This alone shouldn't cause zero RPM,
but it means the simulation timestep is different.

**MORE CRITICAL:** The test calls `startFrame(1.0 / 60.0)` which is a 16.67ms frame.
At 10000 Hz sim frequency, that's ~167 substeps per frame. At 40000 Hz, it would be ~667.
The starter motor at 52 rad/s should spin the engine regardless of sim frequency.

## Recommended Fix Approach

**Step 1: Diagnostic comparison test** (delegated to agent)

Add a temporary diagnostic test to `PresetEngineTests.cpp` that loads Honda via BOTH
Piranha (if available) and PresetFactory, runs one frame, and prints:
- Crankshaft `v_theta` (angular velocity)
- Chamber pressure, mix (p_fuel, p_o2, p_inert)
- Intake flow, exhaust flow
- `getVolume()` on combustion chamber (before and after placeAndInitialize)

**Step 2: Fix the root cause** (delegated to agent, after diagnosis)

Based on diagnostic results, fix `PresetEngineFactory.cpp` or `PresetEngineTests.cpp`.

Likely candidates:
- Missing/wrong parameter causing no combustion
- Simulation frequency mismatch (JSON says 40000, test overrides to 10000)
- Crankshaft not spinning because constraint solver isn't getting enough torque

**Step 3: Verify all 14 tests pass**

## Key Files
- `engine-sim-bridge/src/simulator/PresetEngineFactory.cpp` - JSON engine deserializer
- `engine-sim-bridge/test/PresetEngineTests.cpp` - Failing tests
- `engine-sim-bridge/engine-sim/src/piston_engine_simulator.cpp` - loadSimulation/placeAndInitialize
- `engine-sim-bridge/engine-sim/src/combustion_chamber.cpp` - ignite/flow/update
- `engine-sim-bridge/engine-sim/scripting/include/engine_node.h` - Piranha reference path

## Verification
- `cmake --build build-test --target preset_engine_tests`
- `./build-test/preset_engine_tests`
- All 14 tests pass
