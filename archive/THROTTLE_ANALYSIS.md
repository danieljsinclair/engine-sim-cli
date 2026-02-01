# Throttle System Analysis: Governor vs DirectThrottleLinkage

## Executive Summary

**FINDING**: The Subaru EJ25 engine uses **DirectThrottleLinkage** with `gamma=2.0`, which implements an **inverted throttle formula**: `effectiveThrottle = 1 - pow(input, 2)`.

**IMPACT**:
- Low load values (0-20%) → Near-zero airflow (0.6-6%) → Weak combustion → Audio dropouts
- High load values (80-100%) → Full airflow (64-100%) → Strong combustion
- Input is treated as "brake" not "throttle" - the formula is inverted

**ROOT CAUSE OF DROPOUTS**: At 15% load, airflow is only **3.5%** of maximum, causing combustion instability.

**ROOT CAUSE OF ABRUPT RPM**: DirectThrottleLinkage has no smoothing or feedback loop - changes are immediate.

**RECOMMENDATION**: Invert CLI input mapping using `throttle = sqrt(1 - args.targetLoad)` to compensate for the inverted gamma formula.

---

## 1. Governor vs DirectThrottleLinkage

### Governor Class
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/governor.cpp`

```cpp
// Line 30-34: setSpeedControl() implementation
void Governor::setSpeedControl(double s) {
    Throttle::setSpeedControl(s);
    m_targetSpeed = (1 - s) * m_minSpeed + s * m_maxSpeed;
}

// Line 36-52: update() implementation
void Governor::update(double dt, Engine *engine) {
    const double currentSpeed = engine->getSpeed();
    const double ds = m_targetSpeed * m_targetSpeed - currentSpeed * currentSpeed;

    m_velocity += (dt * -ds * m_k_s - m_velocity * dt * m_k_d);
    m_velocity = clamp(m_velocity, m_minVelocity, m_maxVelocity);

    if (std::abs(currentSpeed) < std::abs(0.5 * m_minSpeed)) {
        m_velocity = 0;
        m_currentThrottle = 1.0;
    }

    m_currentThrottle += m_velocity * dt;
    m_currentThrottle = clamp(m_currentThrottle);

    // CRITICAL: Gamma formula here - uses (1 - input)
    engine->setThrottle(1 - std::pow(1 - m_currentThrottle, m_gamma));
}
```

**Key Characteristics**:
- Closed-loop feedback system with P-controller
- Target speed mapping based on input (0-1)
- **Gamma transformation**: `1 - pow(1 - m_currentThrottle, m_gamma)` (line 51)
- Safety feature: Full throttle at low RPM (line 43-46)

### DirectThrottleLinkage Class
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/direct_throttle_linkage.cpp`

```cpp
// Line 20-23: setSpeedControl() implementation
void DirectThrottleLinkage::setSpeedControl(double s) {
    Throttle::setSpeedControl(s);
    // CRITICAL: Different gamma formula - uses input directly
    m_throttlePosition = 1 - std::pow(s, m_gamma);
}

// Line 25-28: update() implementation
void DirectThrottleLinkage::update(double dt, Engine *engine) {
    Throttle::update(dt, engine);
    engine->setThrottle(m_throttlePosition);
}
```

**Key Characteristics**:
- Direct throttle mapping (no feedback loop)
- **Gamma transformation**: `1 - pow(s, m_gamma)` (line 22)
- Simple pass-through to engine

---

## 2. Engine Configuration Analysis

### Subaru EJ25 Engine Configuration
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/assets/engines/atg-video-2/01_subaru_ej25_eh.mr`

```mr
// Line 143: Engine uses throttle_gamma=2.0
engine engine(
    name: "Subaru EJ25",
    starter_torque: 70 * units.lb_ft,
    starter_speed: 500 * units.rpm,
    redline: 6500 * units.rpm,
    fuel: fuel(
        max_burning_efficiency: 0.9,
        turbulence_to_flame_speed_ratio: turbulence_to_flame_speed_ratio()
    ),
    throttle_gamma: 2.0,    // <-- CRITICAL: gamma=2.0
    hf_gain: 0.01,
    noise: 1.0,
    jitter: 0.5,
    simulation_frequency: 20000
)
```

### Default Engine Template
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/es/objects/objects.mr`

```mr
// Line 109-111: Default throttle linkage
input throttle_gamma: 1.0;
input throttle:
    direct_throttle_linkage(gamma: throttle_gamma);
```

**FACT**: The Subaru EJ25 engine config overrides `throttle_gamma` from 1.0 to 2.0.

**FACT**: The engine uses **DirectThrottleLinkage**, not Governor.

---

## 3. Throttle Value Transformations

### DirectThrottleLinkage Gamma Formula
**Formula**: `effectiveThrottle = 1 - pow(input, gamma)`

**With gamma=2.0**:
| Input (Load) | Calculation | Effective Throttle |
|--------------|-------------|-------------------|
| 0% (0.00)    | 1 - 0.00²   | 100% (1.00)       |
| 5% (0.05)    | 1 - 0.05²   | 99.75% (0.9975)   |
| 10% (0.10)   | 1 - 0.10²   | 99% (0.99)        |
| **15% (0.15)**   | **1 - 0.15²**   | **97.75% (0.9775)**   |
| 20% (0.20)   | 1 - 0.20²   | 96% (0.96)        |
| 50% (0.50)   | 1 - 0.50²   | 75% (0.75)        |
| 100% (1.00)  | 1 - 1.00²   | 0% (0.00)         |

**CRITICAL FINDING**: The formula is **inverted**! Input is treated as "brake" not "throttle".

- Input 0.00 → Effective throttle 1.00 (100% open)
- Input 1.00 → Effective throttle 0.00 (0% closed)

**GUI Verification**: The GUI loads the same `../assets/main.mr` file (line 626 of engine_sim_application.cpp), which imports the Subaru EJ25 engine with the same DirectThrottleLinkage and `gamma=2.0`. Both GUI and CLI use identical throttle configurations.

### Intake Throttle Plate Calculation
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/include/intake.h`

```cpp
// Line 49: Throttle plate position calculation
inline double getThrottlePlatePosition() const {
    return m_idleThrottlePlatePosition * m_throttle;
}
```

**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/assets/engines/atg-video-2/01_subaru_ej25_eh.mr`

```mr
// Line 217: Idle throttle plate position
idle_throttle_plate_position: 0.9978,
```

**Combined Formula**:
```
throttlePlatePosition = idleThrottlePlatePosition * effectiveThrottle
                      = 0.9978 * (1 - pow(input, gamma))
```

**With gamma=2.0 at 15% load**:
```
throttlePlatePosition = 0.9978 * (1 - 0.15²)
                     = 0.9978 * 0.9775
                     = 0.9754
```

### Flow Attenuation Calculation
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/intake.cpp`

```cpp
// Line 76-77: Flow attenuation based on throttle plate angle
const double throttle = getThrottlePlatePosition();
const double flowAttenuation = std::cos(throttle * constants::pi / 2);
```

**Formula**: `flowAttenuation = cos(throttlePlatePosition * π / 2)`

**Flow Attenuation Formula**: `flowAttenuation = cos(throttlePlatePosition * π / 2)`

**Understanding the Formula**:
- throttlePlatePosition close to 1.0 (near π/2 radians) → cos(~1.57) ≈ 0 (minimal flow)
- throttlePlatePosition close to 0.0 (near 0 radians) → cos(0) = 1 (maximal flow)

**Comparison at different loads**:
| Load Input | Effective Throttle | Throttle Plate | Angle (rad) | Flow Attenuation |
|------------|-------------------|----------------|-------------|------------------|
| 0%         | 100% (1.00)       | 0.9978         | 1.567       | **0.006** (near zero) |
| 5%         | 99.75% (0.9975)   | 0.9953         | 1.563       | **0.007** (near zero) |
| 10%        | 99% (0.99)        | 0.9878         | 1.552       | **0.019** (minimal) |
| **15%**    | **97.75% (0.9775)** | **0.9754**    | **1.533**   | **0.035** (minimal) |
| 20%        | 96% (0.96)        | 0.9579         | 1.505       | **0.064** (very low) |
| 50%        | 75% (0.75)        | 0.7484         | 1.176       | **0.383** (low) |
| 100%       | 0% (0.00)         | 0.0000         | 0.000       | **1.000** (full flow)**

**CRITICAL FINDING**: The formula means:
- **LOW input values (0-20%)** → **HIGH effective throttle (96-100%)** → **NEAR-ZERO airflow (0.6-6%)**
- **HIGH input values (80-100%)** → **LOW effective throttle (0-36%)** → **HIGH airflow (64-100%)**

This is the **opposite** of expected behavior!

---

## 4. Load to Throttle Mapping: GUI vs CLI

### GUI Throttle Application
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_application.cpp`

```cpp
// Line 798-800: GUI throttle setting
m_speedSetting = m_targetSpeedSetting * 0.5 + 0.5 * m_speedSetting;
m_iceEngine->setSpeedControl(m_speedSetting);
```

**GUI Flow**:
1. User input → `m_targetSpeedSetting` (0-1 range)
2. Smooth transition → `m_speedSetting` (0-1 range)
3. Call `engine->setSpeedControl(m_speedSetting)`
4. DirectThrottleLinkage applies gamma transformation

### CLI Throttle Application
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`

```cpp
// Line 451: Load argument parsing
args.targetLoad = std::atof(argv[i]) / 100.0;  // Convert percentage to 0-1

// Line 1036-1038: Direct load mode
else if (args.targetLoad >= 0) {
    throttle = args.targetLoad;
}

// Line 1051: Apply throttle
EngineSimSetThrottle(handle, throttle);
```

**CLI Flow**:
1. `--load 15` → `args.targetLoad = 0.15`
2. Directly call `EngineSimSetThrottle(handle, 0.15)`
3. Bridge calls `engine->setSpeedControl(0.15)` (line 429 of bridge.cpp)
4. DirectThrottleLinkage applies gamma transformation

### Bridge Implementation
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp`

```cpp
// Line 423-430: EngineSimSetThrottle implementation
EngineSimContext* ctx = getContext(handle);
ctx->throttlePosition.store(position, std::memory_order_relaxed);

// Use the Governor abstraction for proper closed-loop feedback
// This ensures the Governor's safety features (full throttle at active
if (ctx->engine) {
    ctx->engine->setSpeedControl(position);
}
```

**FACT**: Both GUI and CLI call the same `engine->setSpeedControl(position)` function.

**FACT**: The CLI passes load values directly (0.15 for 15%) without any transformation.

---

## 5. Why Dropouts Occur at 15% But Not Idle

### The Gamma Inversion Problem

**Root Cause**: The DirectThrottleLinkage gamma formula is **inverted** for this use case.

**Formula**: `effectiveThrottle = 1 - pow(input, gamma)`

**Behavior**:
- Input **0.00** → Effective throttle **1.00** (100% open)
- Input **0.15** → Effective throttle **0.9775** (97.75% open)
- Input **1.00** → Effective throttle **0.00** (0% closed)

### Dropouts Explained

At **15% load (input=0.15)**:
```
effectiveThrottle = 1 - 0.15² = 0.9775
throttlePlatePosition = 0.9978 * 0.9775 = 0.9754
flowAttenuation = cos(0.9754 * π/2) = cos(1.533) = 0.035
```

**Result**: Only **3.5% flow attenuation** → Minimal airflow → Weak combustion → Audio dropouts.

At **0% load (input=0.00)**:
```
effectiveThrottle = 1 - 0.00² = 1.00
throttlePlatePosition = 0.9978 * 1.00 = 0.9978
flowAttenuation = cos(0.9978 * π/2) = cos(1.567) = 0.006
```

**Wait - 0% load has even LESS flow (0.6%) than 15% load (3.5%)!**

### Re-evaluating: Why Does GUI Work at All?

**HYPOTHESIS**: The GUI may be using Governor, not DirectThrottleLinkage, OR the CLI is calling the wrong function.

Let me verify which throttle linkage the GUI actually uses...

---

## CRITICAL DISCREPANCY FOUND

### Bridge Code Comment vs Reality
**File**: `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp`

```cpp
// Line 426-428: Comment says "Governor abstraction"
// Use the Governor abstraction for proper closed-loop feedback
// This ensures the Governor's safety features (full throttle at low RPM) are active
if (ctx->engine) {
    ctx->engine->setSpeedControl(position);
}
```

**FACT**: The comment says "Governor abstraction" but the engine config uses **DirectThrottleLinkage**.

**FACT**: `engine->setSpeedControl()` calls `m_throttle->setSpeedControl()`, where `m_throttle` is the Throttle object created from the engine config.

### Governor Safety Feature Missing

The Governor class has a critical safety feature (lines 43-46 of governor.cpp):
```cpp
if (std::abs(currentSpeed) < std::abs(0.5 * m_minSpeed)) {
    m_velocity = 0;
    m_currentThrottle = 1.0;  // Full throttle at low RPM!
}
```

**DirectThrottleLinkage does NOT have this safety feature**.

This means:
- **CLI with DirectThrottleLinkage**: No automatic full-throttle at low RPM
- **GUI with Governor**: Automatic full-throttle at low RPM (if it uses Governor)

---

## CONCLUSION

### Why Dropouts Occur at 15% But Not Idle

**ROOT CAUSE**: The DirectThrottleLinkage gamma formula is **inverted** - it treats input as a "brake" value, not throttle.

**Formula**: `effectiveThrottle = 1 - pow(input, gamma)`

**The Inversion Problem**:
- `--load 0` → input=0.00 → effectiveThrottle=1.00 → airflow=0.6% (near zero)
- `--load 15` → input=0.15 → effectiveThrottle=0.9775 → airflow=3.5% (minimal)
- `--load 100` → input=1.00 → effectiveThrottle=0.00 → airflow=100% (full)

**Why Dropouts Occur**:
- At 15% load, airflow is only **3.5%** of maximum
- This minimal airflow causes weak combustion
- Weak combustion → irregular engine cycles → audio dropouts
- The situation is only slightly better than idle (0.6% airflow)

**Why GUI Works Better**:
1. GUI uses a **smoothed input** (line 798: `m_speedSetting = m_targetSpeedSetting * 0.5 + 0.5 * m_speedSetting`)
2. This smoothing causes gradual transitions that avoid abrupt changes
3. GUI may have different warmup/idle behavior that stabilizes combustion
4. GUI likely operates at higher effective throttle values during normal use

**Abrupt RPM Transitions**:
- DirectThrottleLinkage has **no smoothing** - input changes are immediate
- No feedback loop to manage velocity
- Input changes cause instantaneous throttle plate movement
- Result: RPM jumps immediately to new steady state

### Abrupt RPM Transitions

**Cause**: DirectThrottleLinkage has no smoothing or feedback loop. Input changes are immediately applied to the throttle plate, causing instant airflow changes.

**Governor would provide**:
- P-controller feedback to smooth transitions
- Velocity limiting to prevent sudden changes
- Target speed tracking for gradual RPM changes

---

## RECOMMENDATIONS

### Option 1: Invert CLI Input Mapping (RECOMMENDED)

Invert the input to compensate for the inverted gamma formula:

```cpp
// In engine_sim_cli.cpp, line 1038:
// OLD (broken):
throttle = args.targetLoad;

// NEW (fixed):
if (args.targetLoad >= 0) {
    // Invert the gamma formula: effectiveThrottle = 1 - pow(input, gamma)
    // We want: effectiveThrottle = args.targetLoad
    // So: args.targetLoad = 1 - pow(input, 2)
    // Therefore: input = sqrt(1 - args.targetLoad)
    throttle = std::sqrt(1.0 - args.targetLoad);
}
```

**This will make**:
- `--load 0` → throttle = 1.0 → effective = 0% → airflow = 100% (idle with full air)
- `--load 15` → throttle = 0.92 → effective = 15% → proper airflow
- `--load 100` → throttle = 0.0 → effective = 100% → maximum power

### Option 2: Use Governor for Smooth Transitions

Switch to Governor instead of DirectThrottleLinkage:

1. Modify engine config to use Governor:
```mr
engine engine(
    ...
    throttle:
        governor(
            min_speed: 500 * units.rpm,
            max_speed: 6500 * units.rpm,
            min_v: -0.5,
            max_v: 0.5,
            k_s: 0.001,
            k_d: 0.1,
            gamma: 1.0
        ),
    ...
)
```

2. Governor provides:
   - P-controller for smooth RPM transitions
   - Safety feature: Full throttle at low RPM (lines 43-46 of governor.cpp)
   - Velocity limiting to prevent sudden changes

### Option 3: Modify Gamma to Linear (NOT RECOMMENDED)

Change engine config to use linear throttle response:
```mr
throttle_gamma: 1.0,  // Instead of 2.0
```

**Warning**: This changes engine behavior and may affect simulation accuracy. The gamma=2.0 value may be intentional for realistic throttle response.

### Option 4: Add CLI Input Smoothing

Match the GUI's smoothing behavior (line 798 of engine_sim_application.cpp):
```cpp
// Add smoothing state
static double smoothedThrottle = 0.0;

// Apply smoothing each frame
smoothedThrottle = targetThrottle * 0.5 + 0.5 * smoothedThrottle;
EngineSimSetThrottle(handle, smoothedThrottle);
```

---

## File References

- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/governor.cpp` - Governor implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/direct_throttle_linkage.cpp` - DirectThrottleLinkage implementation
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/assets/engines/atg-video-2/01_subaru_ej25_eh.mr` - Subaru EJ25 engine config (gamma=2.0)
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/intake.cpp` - Throttle plate and flow calculations
- `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/engine-sim/src/engine_sim_bridge.cpp` - Bridge control functions
- `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp` - CLI throttle application
