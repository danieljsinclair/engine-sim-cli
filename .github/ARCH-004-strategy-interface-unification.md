# ARCH-004: Strategy Interface Unification

**Priority:** P0 - Critical Architecture Issue
**Status:** ✅ COMPLETE
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @doc-writer

## Overview

Fix critical SOLID violation where ThreadedStrategy and SyncPullStrategy used different context types (StrategyContext* vs AudioUnitContext*), forcing unnecessary StrategyAdapter hack and breaking OCP.

## Problem Statement (Original)

Previous implementation had a critical interface mismatch:

```cpp
// ThreadedStrategy.h (CORRECT)
void setContext(StrategyContext* context);

// SyncPullStrategy.h (WRONG)
void setAudioUnitContext(struct AudioUnitContext* audioUnitContext);
```

This caused DI fraud, OCP violations, SRP violations (StrategyAdapter), and unnecessary complexity.

## Root Cause

During Phase 6 consolidation, strategies were not fully unified. ThreadedStrategy was updated to use `StrategyContext`, but SyncPullStrategy was left using legacy `AudioUnitContext`, requiring the `StrategyAdapter` hack.

## As-Is State (Current Codebase)

### Unified Interface Shape
Both strategies now use identical method signatures with `BufferContext*`:

```cpp
// Both ThreadedStrategy.h and SyncPullStrategy.h
bool render(BufferContext* context, AudioBufferList* ioData, UInt32 numberFrames) override;
bool AddFrames(BufferContext* context, float* buffer, int frameCount) override;
bool initialize(BufferContext* context, const AudioStrategyConfig& config) override;
void prepareBuffer(BufferContext* context) override;
bool startPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
void stopPlayback(BufferContext* context, EngineSimHandle handle, const EngineSimAPI* api) override;
void resetBufferAfterWarmup(BufferContext* context) override;
void updateSimulation(BufferContext* context, EngineSimHandle handle, const EngineSimAPI& api, double deltaTimeMs) override;
```

### BufferContext (Renamed from StrategyContext)
```cpp
// src/audio/state/BufferContext.h
struct BufferContext {
    AudioState audioState;
    BufferState bufferState;
    Diagnostics diagnostics;
    CircularBuffer* circularBuffer;   // Non-owning pointer
    IAudioStrategy* strategy;         // Non-owning pointer
    EngineSimHandle engineHandle;
    const EngineSimAPI* engineAPI;
};
```

Composed from focused state structs:
- `AudioState` -- playback state (isPlaying, sampleRate)
- `BufferState` -- circular buffer state (pointers, counters)
- `Diagnostics` -- performance metrics (render time, headroom, budget)
- `CircularBuffer*` -- actual audio data buffer (owned by AudioPlayer)

### Deleted Files (Confirmed on Disk)
- `src/audio/adapters/StrategyAdapter.h` -- deleted
- `src/audio/adapters/StrategyAdapter.cpp` -- deleted
- `src/audio/adapters/StrategyAdapterFactory.h` -- deleted
- `src/audio/renderers/IAudioRenderer.h` -- deleted
- `src/audio/adapters/` -- directory removed
- `src/audio/renderers/` -- directory removed

### Factory Direct Creation (No Adapter)
```cpp
// src/config/CLIMain.cpp
AudioMode mode = config.syncPull ? AudioMode::SyncPull : AudioMode::Threaded;
std::unique_ptr<IAudioStrategy> audioStrategy = IAudioStrategyFactory::createStrategy(mode, cliLogger.get());
```

No adapter layer between CLIMain and the strategies. Both strategies are truly swappable via the factory.

## Acceptance Criteria Status

### Interface Unification
- [x] Both strategies use `BufferContext*` (renamed from `StrategyContext*`)
- [x] Both strategies have identical method signatures
- [x] Both strategies are swappable via `IAudioStrategyFactory` without adapters
- [x] `AudioUnitContext` fully eliminated from all source files

### Code Cleanup
- [x] `src/audio/adapters/StrategyAdapter.h` deleted
- [x] `src/audio/adapters/StrategyAdapter.cpp` deleted
- [x] `src/audio/adapters/StrategyAdapterFactory.h` deleted
- [x] `src/audio/renderers/IAudioRenderer.h` deleted
- [x] Adapter directory removed entirely
- [x] Renderers directory removed entirely

### Testing
- [x] `make test` passes completely (26 tests passing)
- [x] Both ThreadedStrategy and SyncPullStrategy tests pass
- [x] No adapter code remains in codebase

## SOLID Compliance (Verified)

| Principle | Before | After |
|-----------|--------|-------|
| **SRP** | StrategyAdapter bridged interfaces | Each strategy has single responsibility |
| **OCP** | Adding strategies required adapter changes | New strategies add without modification |
| **LSP** | Both implemented IAudioStrategy | Both honor interface contract |
| **ISP** | Focused IAudioStrategy interface | Focused IAudioStrategy interface |
| **DIP** | Different context types (DI fraud) | Both depend on BufferContext abstraction |

## References

- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/IAudioStrategy.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/ThreadedStrategy.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/SyncPullStrategy.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/state/BufferContext.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/strategies/IAudioStrategyFactory.cpp`

---

**Created:** 2026-04-12
**Last Updated:** 2026-04-16
**Estimate:** 2-3 hours

## Recent Changes

**2026-04-16:** Phase A cleanups completed (commit b857e00)
- Removed `configure()` method from IAudioStrategy interface (was no-op)
- Strategies no longer require configure() call from AudioPlayer
- This simplifies the interface and prepares for Phase B (BufferContext eradication)

See ARCH-005 for the complete 7-phase refactor programme details.

