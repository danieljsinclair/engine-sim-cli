# ARCH-001: Phase 6 - IAudioStrategy Consolidation

**Priority:** P0 - Critical Architecture
**Status:** ✅ COMPLETE (Historical)
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @product-owner

**Note:** This documents a historical phase (Phase 6) that has been superseded by the 7-phase refactor programme (ARCH-005). See ARCH-005 for current work.

## Overview

Consolidate IAudioMode and IAudioRenderer into a single, unified IAudioStrategy interface following SOLID Open-Closed Principle. This eliminates duplicate interfaces and provides clean, swappable strategy pattern for audio rendering modes.

## Problem Statement (Original)

Previous implementation had duplicate interfaces:
- `IAudioMode` - managed mode selection and lifecycle
- `IAudioRenderer` - handled rendering callback

This violated Single Responsibility Principle and created confusion about responsibilities. Test harness directly called implementation details like `startAudioRenderingThread()`, violating OCP by exposing internal threading details.

## Objectives (All Completed)

1. **Merge Interfaces**: Single `IAudioStrategy` unifies mode and renderer responsibilities
2. **Strategy Implementation**:
   - `ThreadedStrategy`: Handles lifecycle + rendering + thread management internally
   - `SyncPullStrategy`: Handles lifecycle + rendering + on-demand (no threads)
3. **Eliminate Implementation Leaks**: Test harness is strategy-agnostic
4. **SOLID Compliance**: Follows Open-Closed Principle

## As-Is State (Current Codebase)

### Interface Design (Actual)
```cpp
// src/audio/strategies/IAudioStrategy.h
class IAudioStrategy {
public:
    virtual ~IAudioStrategy() = default;
    virtual const char* getName() const = 0;
    virtual bool isEnabled() const = 0;
    virtual bool render(BufferContext* context, AudioBufferList* ioData, UInt32 numberFrames) = 0;
    virtual bool AddFrames(BufferContext* context, float* buffer, int frameCount) = 0;
    virtual bool initialize(BufferContext* context, const AudioStrategyConfig& config) = 0;
    virtual void prepareBuffer(BufferContext* context) = 0;
    virtual bool startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) = 0;
    virtual void resetBufferAfterWarmup(BufferContext* context) = 0;
    virtual bool shouldDrainDuringWarmup() const = 0;
    virtual std::string getDiagnostics() const = 0;
    virtual std::string getProgressDisplay() const = 0;
    virtual void configure(const AudioStrategyConfig& config) = 0;
    virtual void reset() = 0;
    virtual std::string getModeString() const = 0;
    virtual void updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) = 0;
};
```

**Note:** The actual interface is more comprehensive than the original design. It takes `BufferContext*` (renamed from `StrategyContext`) and includes methods for warmup, diagnostics, and simulation updates.

### ThreadedStrategy (Actual)
- Implements `IAudioStrategy` completely
- `startPlayback()` calls `api->StartAudioThread(handle)` to start the engine's audio thread
- `render()` reads from `CircularBuffer` using cursor-chasing
- `updateSimulation()` calls `api.Update()` in the main loop
- Maintains cursor-chasing behavior

### SyncPullStrategy (Actual)
- Implements `IAudioStrategy` completely
- `startPlayback()` / `stopPlayback()` manage `isPlaying` flag only (no thread)
- `render()` calls `context->engineAPI->RenderOnDemand()` synchronously
- `updateSimulation()` is a no-op (render callback drives simulation)
- Maintains deterministic on-demand behavior

### Factory (Actual)
```cpp
// src/audio/strategies/IAudioStrategyFactory.cpp
std::unique_ptr<IAudioStrategy> IAudioStrategyFactory::createStrategy(
    AudioMode mode, ILogging* logger) {
    switch (mode) {
        case AudioMode::Threaded: return std::make_unique<ThreadedStrategy>(logger);
        case AudioMode::SyncPull: return std::make_unique<SyncPullStrategy>(logger);
    }
    return nullptr;
}
```

### Integration (Actual)
- `CLIMain.cpp` creates strategies directly via `IAudioStrategyFactory::createStrategy()` -- no adapter layer
- `SimulationLoop` takes `IAudioStrategy&` directly -- no `IAudioRenderer` references
- `AudioPlayer` takes `IAudioStrategy*` via constructor injection

### Deleted Files (Confirmed)
- `src/audio/adapters/StrategyAdapter.h` -- deleted
- `src/audio/adapters/StrategyAdapter.cpp` -- deleted
- `src/audio/adapters/StrategyAdapterFactory.h` -- deleted
- `src/audio/renderers/IAudioRenderer.h` -- deleted
- `src/audio/modes/` -- deleted (IAudioMode and all mode implementations)
- `src/audio/renderers/` -- deleted (all old renderer implementations)

## Acceptance Criteria Status

### Interface Design
- [x] Create `IAudioStrategy` interface with unified lifecycle and rendering methods
- [x] Remove `IAudioMode` and `IAudioRenderer` interfaces
- [x] All strategy-specific logic encapsulated in strategy implementations

### ThreadedStrategy
- [x] Internally manages audio thread lifecycle via `startPlayback()`/`stopPlayback()`
- [x] Implements `IAudioStrategy` interface completely
- [x] No external thread management exposed
- [x] Maintains cursor-chasing behavior

### SyncPullStrategy
- [x] Implements on-demand rendering via `render()` callback
- [x] Implements `IAudioStrategy` interface completely
- [x] No threading-related code
- [x] Maintains deterministic behavior

### Testing
- [x] `make test` passes (26 tests confirmed in ARCH-004 completion notes)
- [x] Integration tests for both strategies work correctly
- [x] Deterministic behavior verified (SineWave tests)
- [x] Test architect review passed

## Dependencies

- Blocks: ARCH-003 Phase 4 IAudioPlatform extraction (completed)
- Blocks: ARCH-004 Strategy Interface Unification (completed)

## References

- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/IAudioStrategy.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/ThreadedStrategy.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/SyncPullStrategy.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/IAudioStrategyFactory.cpp`
- `/Users/danielsinclair/vscode/escli.refac7/docs/ARCHITECTURE_AUDIT.md`

---

**Created:** 2026-04-08
**Last Updated:** 2026-04-16
**Estimate:** 2-3 days

## Recent Changes

**2026-04-16:** Phase A cleanups completed (commit b857e00)
- Removed `configure()` method from IAudioStrategy (was no-op)
- Extracted audio callback lambda into `audioRenderCallback()` helper
- Extracted hardware provider creation into `createHardwareProvider()` helper
- These cleanups prepare for Phase B (BufferContext eradication) in the 7-phase programme

See ARCH-005 for the complete 7-phase refactor programme details.


