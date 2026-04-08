# Option B Integration Plan

**Document Version:** 1.0
**Date:** 2026-04-07
**Status:** READY TO START

---

## Executive Summary

This plan outlines the integration of Option B's new audio architecture into the main application components. The foundation (Phases 1-3) is complete with 100% test pass rate. This phase connects the new architecture to AudioPlayer, SimulationLoop, and CLIMain.

---

## Current Status

### Build Status
- **Overall Build:** ✅ GREEN
- **Unit Tests:** 21/21 passing
- **Integration Tests:** 7/7 passing
- **Smoke Tests:** 26/26 passing

### Completed Phases
- ✅ Phase 1: IAudioStrategy Foundation
- ✅ Phase 2: IAudioHardwareProvider
- ✅ Phase 3: State Management

---

## Integration Architecture

### Target Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    CLIMain                                │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  Factories                                           │  │
│  │  ├─ IAudioStrategyFactory                         │  │
│  │  └─ AudioHardwareProviderFactory                  │  │
│  └─────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    SimulationLoop                           │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  Uses IAudioStrategy (DI)                          │  │
│  │  Uses StrategyContext (state)                     │  │
│  └─────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                            │
                            ▼
┌─────────────────────────────────────────────────────────────┐
│                    AudioPlayer                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  IAudioStrategy* strategy                          │  │
│  │  IAudioHardwareProvider* hardwareProvider         │  │
│  │  StrategyContext* context                          │  │
│  └─────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
```

---

## Integration Tasks

### Task 1: AudioPlayer Refactoring
**Priority:** CRITICAL
**Estimated Effort:** 2-3 hours

**Objective:** Update AudioPlayer to use new architecture interfaces.

**Current State:**
```cpp
// AudioPlayer.h - Current
class AudioPlayer {
    IAudioRenderer* renderer;
    AudioUnit audioUnit;
    AudioDeviceID deviceID;
    AudioUnitContext* context;
};
```

**Target State:**
```cpp
// AudioPlayer.h - Target
class AudioPlayer {
    IAudioStrategy* strategy;
    IAudioHardwareProvider* hardwareProvider;
    StrategyContext* context;
};
```

**Work Items:**
1. Replace `IAudioRenderer* renderer` with `IAudioStrategy* strategy`
2. Replace `AudioUnit audioUnit` and `AudioDeviceID deviceID` with `IAudioHardwareProvider* hardwareProvider`
3. Replace `AudioUnitContext* context` with `StrategyContext* context`
4. Remove AudioUnit includes
5. Add new includes: `IAudioStrategy.h`, `IAudioHardwareProvider.h`, `StrategyContext.h`
6. Update constructor to accept new dependencies
7. Update `initialize()` method to work with IAudioStrategy
8. Update `start()` and `stop()` to use IAudioHardwareProvider
9. Update callback to use IAudioStrategy
10. Remove old AudioUnit-specific code

**Acceptance Criteria:**
- AudioPlayer depends on IAudioStrategy interface
- AudioPlayer depends on IAudioHardwareProvider interface
- No AudioUnit includes or dependencies
- AudioPlayer compiles and links successfully
- All AudioPlayer tests pass

---

### Task 2: SimulationLoop Refactoring
**Priority:** CRITICAL
**Estimated Effort:** 1-2 hours

**Objective:** Update SimulationLoop to use new architecture.

**Current State:**
```cpp
// SimulationLoop.h - Current
class SimulationConfig {
    std::unique_ptr<IAudioRenderer> audioMode;
};

int runUnifiedAudioLoop(
    AudioPlayer* audioPlayer,
    IAudioRenderer& audioMode,
    // ...
);
```

**Target State:**
```cpp
// SimulationLoop.h - Target
class SimulationConfig {
    std::unique_ptr<IAudioStrategy> audioStrategy;
};

int runUnifiedAudioLoop(
    AudioPlayer* audioPlayer,
    IAudioStrategy& audioStrategy,
    StrategyContext& context,
    // ...
);
```

**Work Items:**
1. Replace `IAudioRenderer*` with `IAudioStrategy*`
2. Add StrategyContext parameter to methods
3. Update SimulationConfig to use IAudioStrategy
4. Update factory usage to IAudioStrategyFactory
5. Remove old IAudioMode/IAudioRenderer references

**Acceptance Criteria:**
- SimulationLoop depends on IAudioStrategy interface
- SimulationLoop uses StrategyContext
- Old IAudioMode/IAudioRenderer references removed
- SimulationLoop compiles and links successfully
- All tests pass

---

### Task 3: CLIMain Refactoring
**Priority:** CRITICAL
**Estimated Effort:** 2-3 hours

**Objective:** Update CLIMain to use new factories and architecture.

**Current State:**
```cpp
// CLIMain.cpp (inferred)
// Uses old factories for IAudioMode/IAudioRenderer
// Creates AudioUnit directly
```

**Target State:**
```cpp
// CLIMain.cpp - Target
// Uses IAudioStrategyFactory
// Uses AudioHardwareProviderFactory
// Creates StrategyContext
```

**Work Items:**
1. Replace old factory calls with IAudioStrategyFactory
2. Replace AudioUnit creation with AudioHardwareProviderFactory
3. Create StrategyContext and initialize it
4. Pass StrategyContext to AudioPlayer
5. Update command-line argument parsing for new architecture
6. Remove old factory references

**Acceptance Criteria:**
- CLIMain uses IAudioStrategyFactory
- CLIMain uses AudioHardwareProviderFactory
- CLIMain creates StrategyContext
- CLIMain compiles and links successfully
- Smoke tests pass
- CLI arguments work correctly

---

### Task 4: Build System Updates
**Priority:** HIGH
**Estimated Effort:** 1 hour

**Objective:** Update CMakeLists.txt to include new architecture components.

**Work Items:**
1. Add source files to build:
   ```
   src/audio/strategies/IAudioStrategy.h
   src/audio/strategies/ThreadedStrategy.cpp
   src/audio/strategies/SyncPullStrategy.cpp
   src/audio/strategies/IAudioStrategyFactory.cpp
   src/audio/hardware/IAudioHardwareProvider.h
   src/audio/hardware/CoreAudioHardwareProvider.cpp
   src/audio/hardware/AudioHardwareProviderFactory.cpp
   src/audio/state/AudioState.h
   src/audio/state/BufferState.h
   src/audio/state/Diagnostics.h
   src/audio/state/StrategyContext.h
   ```

2. Update include directories
3. Ensure new test targets are configured
4. Update linkage for any dependencies

**Acceptance Criteria:**
- All new source files added to build
- CMakeLists.txt compiles successfully
- All targets build correctly
- New tests can run

---

### Task 5: Integration Testing
**Priority:** CRITICAL
**Estimated Effort:** 2-3 hours

**Objective:** Verify the integrated system works correctly.

**Work Items:**
1. Create integration tests for new architecture
2. Run smoke tests
3. Verify audio output works
4. Test different strategies (ThreadedStrategy, SyncPullStrategy)
5. Verify command-line arguments work
6. Performance testing

**Acceptance Criteria:**
- Integration tests pass
- Smoke tests pass
- Audio output verified
- All strategies work correctly
- No regressions

---

## Rollout Plan

### Phase 4A: Foundation Integration
1. Task 4: Build System Updates (1 hour)
   - Update CMakeLists.txt first to enable compilation

### Phase 4B: Component Refactoring
2. Task 1: AudioPlayer Refactoring (2-3 hours)
3. Task 2: SimulationLoop Refactoring (1-2 hours)
4. Task 3: CLIMain Refactoring (2-3 hours)

### Phase 4C: Integration Testing
5. Task 5: Integration Testing (2-3 hours)

---

## Dependencies

### Critical Path
- Build System Updates → AudioPlayer Refactoring → SimulationLoop Refactoring → CLIMain Refactoring → Integration Testing

### Parallel Work
- Once Build System Updates is complete:
  - AudioPlayer, SimulationLoop, and CLIMain refactoring can proceed in parallel
  - Integration Testing must wait for all refactoring to complete

---

## Risk Mitigation

### Risk 1: Breaking Changes to AudioPlayer
- **Likelihood:** HIGH
- **Impact:** HIGH
- **Mitigation:** Comprehensive unit tests, gradual migration, keep old code until new code is verified

### Risk 2: Command-Line Argument Changes
- **Likelihood:** MEDIUM
- **Impact:** MEDIUM
- **Mitigation:** Document changes, test all argument combinations, maintain backward compatibility where possible

### Risk 3: Performance Regression
- **Likelihood:** LOW
- **Impact:** MEDIUM
- **Mitigation:** Performance testing, profiling, optimization

### Risk 4: Test Failures
- **Likelihood:** MEDIUM
- **Impact:** HIGH
- **Mitigation:** Continuous testing, fix failures immediately, maintain test coverage

---

## Success Criteria

### Must Have
- [x] All unit tests pass
- [x] All integration tests pass
- [x] All smoke tests pass
- [ ] AudioPlayer uses new architecture
- [ ] SimulationLoop uses new architecture
- [ ] CLIMain uses new architecture
- [ ] Build succeeds
- [ ] No regressions

### Nice to Have
- [ ] Performance improvements documented
- [ ] All old code removed
- [ ] Architecture documentation updated
- [ ] Migration guide created

---

## Timeline

| Task | Duration | Start | End | Owner |
|------|----------|-------|-----|-------|
| Task 4: Build System Updates | 1 hour | Day 1 | Day 1 | TBD |
| Task 1: AudioPlayer Refactoring | 3 hours | Day 1 | Day 2 | TBD |
| Task 2: SimulationLoop Refactoring | 2 hours | Day 2 | Day 2 | TBD |
| Task 3: CLIMain Refactoring | 3 hours | Day 2 | Day 3 | TBD |
| Task 5: Integration Testing | 3 hours | Day 3 | Day 3 | TBD |

**Total Duration:** 3 days

---

## Post-Integration

### Phase 5: Cleanup
- Remove deprecated IAudioMode files
- Remove deprecated IAudioRenderer files
- Remove old AudioUnitContext
- Remove baseline .dat files
- Remove temporary test files

### Phase 6: Documentation
- Update AUDIO_MODULE_ARCHITECTURE.md
- Update ARCHITECTURE_TODO.md
- Create migration guide
- Update API documentation

---

## References

### Related Documents
- `docs/OPTION_B_PROGRESS_SUMMARY.md` - Progress tracking
- `docs/AUDIO_MODULE_ARCHITECTURE.md` - Current architecture
- `docs/ARCHITECTURE_TODO.md` - Task tracking

### Key Files
- `src/audio/strategies/IAudioStrategy.h` - Unified strategy interface
- `src/audio/hardware/IAudioHardwareProvider.h` - Platform abstraction
- `src/audio/state/StrategyContext.h` - Composed state
- `src/AudioPlayer.h` - Target for refactoring
- `src/simulation/SimulationLoop.h` - Target for refactoring

---

*End of Integration Plan*
