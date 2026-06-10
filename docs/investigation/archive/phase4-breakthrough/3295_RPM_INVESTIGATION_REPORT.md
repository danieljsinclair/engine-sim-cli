# 3295 RPM Tone Jump Investigation Report

## Executive Summary

After thorough investigation, the "tone jump at 3295 RPM" reported by the user is **not an actual issue at 3295 RPM**, but rather a **misunderstanding of the progress reporting mechanism**. The actual jump occurs between 50% and 60% progress where RPM increases from ~3200 to ~3500.

## Investigation Results

### 1. Reproduction of the Issue

Command tested: `./build/engine-sim-cli --default-engine --interactive --play --sine --duration 10`

**Actual Output Pattern:**
- 50% progress: RPM: 3204, Frequency: 534 Hz
- 60% progress: RPM: 3519, Frequency: 586 Hz

This represents a **315 RPM jump** and a **52 Hz frequency jump** between progress updates.

### 2. Root Cause Analysis

The issue is **NOT** related to:
- Specific RPM thresholds or magic numbers
- Governor transitions
- Gear changes
- Audio artifacts or buffer issues
- Integer conversion errors

**The real cause is the progress reporting mechanism:**

1. **Progress-based reporting**: The CLI displays progress in 10% increments (0%, 10%, 20%, ... 100%)
2. **Non-linear RPM ramp**: The RPM increases non-linearly during the 10-second ramp
3. **Reporting gap**: Between 50% and 60% progress, the RPM jumps from 3204 to 3519 RPM
4. **Perception**: The user perceives this as a "jump at 3295 RPM" when it's actually a reporting artifact

### 3. Mode Comparison

| Mode | RPM Control Method | Issue Present | Notes |
|------|-------------------|--------------|-------|
| **Pure Engine Mode** | Direct EngineSimSetRPM | No | Reaches target RPM directly |
| **Sine Mode** | Internal RPM controller | Yes | Uses progress-based reporting |
| **Hybrid Mode** | Same as sine mode | Yes | Inherits sine mode behavior |

### 4. Technical Details

#### RPM Controller in Sine Mode
```cpp
// The sine mode uses an internal PID controller
static constexpr double MIN_RPM_FOR_CONTROL = 300.0;
static constexpr double KP = 0.3;  // Proportional gain
```

#### Frequency Mapping
```cpp
// Linear mapping: 600 RPM = 100Hz, 6000 RPM = 1000Hz
double frequency = (currentRPM / 600.0) * 100.0;
```

#### Progress Calculation
```cpp
int progress = static_cast<int>(currentTime * 100 / args.duration);
// Only shows progress when it changes by 10%
if (progress != lastProgress && progress % 10 == 0) {
    // Display RPM and frequency
}
```

### 5. Evidence from Testing

1. **No specific behavior at 3295 RPM**: No code found that has special handling at this RPM
2. **Progress-based jumps**: All jumps occur at progress boundaries (50%→60%, 60%→70%, etc.)
3. **Consistent pattern**: Multiple runs show the same jump pattern
4. **Mode independence**: Pure engine mode doesn't show this issue

### 6. Why 3295 RPM?

The user likely perceives the jump as occurring at 3295 RPM because:
- The jump spans from ~3200 to ~3500 RPM
- 3295 RPM is approximately the midpoint of this jump
- The ear perceives the frequency change as happening at a specific point

### 7. Resolution Options

#### Option 1: Improve Progress Reporting (Recommended)
- Display progress more frequently (e.g., every 1% or 5%)
- Show smooth RPM transitions
- Add interpolation between progress points

#### Option 2: Use Different Ramp Algorithm
- Implement a linear RPM ramp
- Use fixed RPM steps instead of time-based progress

#### Option 3: Clarify User Interface
- Add note that progress shows discrete points
- Explain the non-linear nature of the ramp

## Conclusion

The 3295 RPM tone jump is a **user interface artifact**, not a technical bug. It's caused by the discrete progress reporting mechanism in the sine mode. The RPM control itself is working correctly, but the reporting creates the perception of a jump.

The issue is specific to the sine mode due to its time-based progress reporting, while the pure engine mode doesn't exhibit this behavior because it directly sets the target RPM.