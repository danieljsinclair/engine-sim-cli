# Throttle Fix Proposal

## The Problem

CLI throttle is backwards because DirectThrottleLinkage uses inverted logic:
- Input 0.0 → 100% throttle
- Input 1.0 → 0% throttle

## The Fix

### Option 1: Simple Inversion (Quick Fix)

In `/Users/danielsinclair/vscode/engine-sim-cli/src/engine_sim_cli.cpp`:

```cpp
// Around line 923, change:
throttle = interactiveLoad;

// To:
throttle = 1.0 - interactiveLoad;  // Invert for DirectThrottleLinkage
```

This will make CLI work correctly for DirectThrottleLinkage engines (most engines).

### Option 2: Smart Detection (Better Fix)

Add a function to query the throttle type, then conditionally invert:

```cpp
// In engine_sim_bridge.h:
EngineSimResult EngineSimGetThrottleType(
    EngineSimHandle handle,
    EngineSimThrottleType* type);

typedef enum {
    ESIM_THROTTLE_DIRECT,
    ESIM_THROTTLE_GOVERNOR
} EngineSimThrottleType;
```

Then in CLI:

```cpp
EngineSimThrottleType throttleType;
EngineSimGetThrottleType(handle, &throttleType);

if (throttleType == ESIM_THROTTLE_DIRECT) {
    throttle = 1.0 - interactiveLoad;  // Invert for DirectThrottleLinkage
} else {
    throttle = interactiveLoad;  // Normal for Governor
}
```

### Option 3: Fix at Bridge Level (Cleanest)

In `/Users/danielsinclair/vscode/engine-sim-cli/engine-sim-bridge/src/engine_sim_bridge.cpp`:

```cpp
EngineSimResult EngineSimSetThrottle(EngineSimHandle handle, double position) {
    if (!validateHandle(handle)) {
        return ESIM_ERROR_INVALID_HANDLE;
    }

    if (position < 0.0 || position > 1.0) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    EngineSimContext* ctx = getContext(handle);
    ctx->throttlePosition.store(position, std::memory_order_relaxed);

    // Convert from "intuitive" throttle (0=closed, 1=open) to
    // DirectThrottleLinkage's inverted logic (0=open, 1=closed)
    double speedControlValue;
    if (ctx->engine->getThrottle()->isDirectThrottleLinkage()) {
        speedControlValue = 1.0 - position;  // Invert
    } else {
        speedControlValue = position;  // Normal for Governor
    }

    if (ctx->engine) {
        ctx->engine->setSpeedControl(speedControlValue);
    }

    return ESIM_SUCCESS;
}
```

This way, ALL clients (CLI, .NET, etc.) get correct behavior automatically.

## Testing

### Before Fix

```
User Action    CLI Passes    Engine Gets    Result
-----------     -----------   -----------    ------
Space           0.0           100%           Full throttle!
R               0.2           96%            High
W x5            0.5           75%            Medium
W x10           0.8           36%            Low (DECREASED!)
W x20           1.0           0%             DEAD
```

### After Fix

```
User Action    CLI Inverts   Engine Gets    Result
-----------     -----------   -----------    ------
Space           1.0           0%             Correct (brake)
R               0.8           36%            Low idle
W x5            0.5           75%            Medium
W x10           0.2           96%            High
W x20           0.0           100%           Full power
```

## Recommendation

**Implement Option 3** (fix at bridge level) because:
1. Fixes all clients automatically
2. Hides the complexity from users
3. Matches GUI behavior
4. Single point of maintenance

## Implementation Steps

1. Add `isDirectThrottleLinkage()` method to Throttle class
2. Modify `EngineSimSetThrottle()` to invert when needed
3. Test with Subaru EJ25 (DirectThrottleLinkage)
4. Test with an engine using Governor (if any)
5. Verify GUI still works correctly
