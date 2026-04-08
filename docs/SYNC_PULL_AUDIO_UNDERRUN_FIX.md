# SyncPull Audio Underrun Fix

## Problem
The CLI was experiencing audio clicks and dropouts in SyncPull mode with sine mode, with the warning:
```
[WARN] SyncPullStrategy::render: Engine provided 270/471 frames, silenced remainder
```

## Root Cause
Race condition between two threads updating the simulation simultaneously:

1. **Main thread**: Calling `api.Update()` from `StrategyAdapter::updateSimulation()` in the simulation loop
2. **Audio callback thread**: Calling `RenderOnDemand()` from `SyncPullStrategy::render()` which also updates the simulation

Both were updating the simulation state at the same time, causing:
- Simulation state corruption
- Insufficient audio generation (~270 frames instead of 471 requested)
- Audio clicks and dropouts

## Solution
Modified `StrategyAdapter::updateSimulation()` to skip calling `api.Update()` for SyncPullStrategy:

```cpp
void StrategyAdapter::updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                                       AudioPlayer* audioPlayer) {
    // Threaded mode: generates audio separately, but simulation still needs updates
    // Sync-pull mode: generates audio on-demand via RenderOnDemand() which updates simulation
    // Only call api.Update() for threaded mode to avoid race condition
    if (strategy_ && strcmp(strategy_->getName(), "Threaded") == 0) {
        api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);
    }
    (void)audioPlayer;
}
```

## Rationale
- **SyncPullStrategy**: `RenderOnDemand()` already updates the simulation as part of generating audio on-demand
- **ThreadedStrategy**: Still needs `api.Update()` from the main thread because audio generation happens in a separate thread

## Results
- All 65 tests pass (26 smoke + 7 integration + 32 unit)
- No CLI segmentation faults
- No audio underruns or clicks in SyncPull mode
- Proper audio generation: `req=471 got=471` instead of `req=471 got=270`
- User command `./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr` works correctly

## Files Modified
- `src/audio/adapters/StrategyAdapter.cpp` - Updated `updateSimulation()` method

## Testing
Verified with:
1. `./build/engine-sim-cli --sine --play` - No underruns, clean audio
2. `./build/engine-sim-cli --interactive --play --script es/ferrari_f136.mr` - Works correctly
3. Full test suite - All tests passing
