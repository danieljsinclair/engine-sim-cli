# ARCH-001: Phase 6 - IAudioStrategy Consolidation

**Priority:** P0 - Critical Architecture
**Status:** 🟡 Ready for Implementation
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @product-owner

## Overview

Consolidate IAudioMode and IAudioRenderer into a single, unified IAudioStrategy interface following SOLID Open-Closed Principle. This eliminates duplicate interfaces and provides clean, swappable strategy pattern for audio rendering modes.

## Problem Statement

Current implementation has duplicate interfaces:
- `IAudioMode` - manages mode selection and lifecycle
- `IAudioRenderer` - handles rendering callback

This violates Single Responsibility Principle and creates confusion about responsibilities. Test harness directly calls implementation details like `startAudioRenderingThread()`, violating OCP by exposing internal threading details.

## Objectives

1. **Merge Interfaces**: Create single `IAudioStrategy` that unifies mode and renderer responsibilities
2. **Strategy Implementation**:
   - `ThreadedStrategy`: Handles lifecycle + rendering + thread management internally
   - `SyncPullStrategy`: Handles lifecycle + rendering + on-demand (no threads)
3. **Eliminate Implementation Leaks**: Test harness becomes strategy-agnostic
4. **SOLID Compliance**: Follow Open-Closed Principle - open for extension, closed for modification

## Acceptance Criteria

### Interface Design
- [ ] Create `IAudioStrategy` interface with unified lifecycle and rendering methods
- [ ] Remove `IAudioMode` and `IAudioRenderer` interfaces
- [ ] All strategy-specific logic encapsulated in strategy implementations

### ThreadedStrategy
- [ ] Internally manages audio thread lifecycle (start/stop)
- [ ] Implements `IAudioStrategy` interface completely
- [ ] No external thread management exposed
- [ ] Maintains cursor-chasing behavior

### SyncPullStrategy
- [ ] Implements on-demand rendering only (no threads)
- [ ] Implements `IAudioStrategy` interface completely
- [ ] No threading-related code
- [ ] Maintains deterministic behavior

### Test Harness
- [ ] Remove direct calls to `startAudioRenderingThread()`
- [ ] Remove sleep delays related to thread startup
- [ ] Strategy-agnostic - no knowledge of threading details
- [ ] Works with any IAudioStrategy implementation

### Testing
- [ ] `make test` passes for all unit and integration tests
- [ ] Integration tests for both strategies work correctly
- [ ] Deterministic behavior verified (SineWave_SyncPull test)
- [ ] Test architect review passes

### Documentation
- [ ] AUDIO_MODULE_ARCHITECTURE.md updated
- [ ] ARCHITECTURE_TODO.md marked as completed for Phase 6
- [ ] Interface documentation complete

## Technical Approach

### New Interface Design
```cpp
class IAudioStrategy {
public:
    virtual ~IAudioStrategy() = default;

    // Lifecycle management
    virtual void initialize(StrategyContext& context) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;

    // Rendering callback
    virtual void render(const AudioCallbackInfo& info) = 0;

    // State query
    virtual bool isActive() const = 0;
};
```

### ThreadedStrategy Changes
- Remove external `startAudioRenderingThread()` method
- Internal thread lifecycle managed in `start()`/`stop()`
- Encapsulates all threading logic
- Maintains existing cursor-chasing behavior

### SyncPullStrategy Changes
- Implements `start()`/`stop()` as no-ops (no thread)
- Rendering happens on-demand via `render()`
- Maintains deterministic behavior
- No threading code at all

## Dependencies

- Blocks: Phase 4 IAudioPlatform extraction
- Depends on: Current working ThreadedStrategy and SyncPullStrategy

## Risk Assessment

**High Risk:**
- ThreadedStrategy internal thread management could introduce race conditions
- Test harness changes could break existing tests

**Mitigation:**
- Test architect review required
- Incremental implementation with tests at each step
- Preserve existing behavior where possible
- Run `make test` after each major change

## Definition of Done

- [ ] All acceptance criteria met
- [ ] `make test` passes completely (no failures)
- [ ] Documentation updated
- [ ] Code review approved by @tech-architect and @test-architect
- [ ] @product-owner final approval
- [ ] Build succeeds with `make build`

## References

- `/Users/danielsinclair/vscode/escli.refac7/docs/ARCHITECTURE_AUDIT.md`
- `/Users/danielsinclair/vscode/escli.refac7/docs/AUDIO_MODULE_ARCHITECTURE.md`
- `/Users/danielsinclair/vscode/escli.refac7/docs/ARCHITECTURE_TODO.md`
- `/Users/danielsinclair/vscode/escli.refac7/docs/DETERMINISM_FAILURE_FIX_PLAN.md`

---

**Created:** 2026-04-08
**Last Updated:** 2026-04-08
**Estimate:** 2-3 days
