# Architecture Refactoring TODO

## Status: IN PROGRESS

## Completed ✅
- [x] Git mv AudioMode.cpp → audio/modes/ThreadedAudioMode.cpp (history preserved)
- [x] Extract renderers to audio/renderers/ folder
- [x] Extract mode classes to audio/modes/ folder
- [x] One class per file for renderers
- [x] DRY helpers extracted (clampFramesToCapacity, handleUnderrun)

## Current Issues

### 1. Class Declarations in Headers
Question: Do abstract classes need separate .h files?
- Currently: IAudioRenderer.h + 3 renderer .h files
- Could consolidate: All in IAudioRenderer.h

### 2. SyncPullAudioMode in IAudioMode.h
- Class definition in header causes coupling
- Consider moving to SyncPullAudioMode.cpp

## Architecture Recommendations (from Solution Architect)

### Priority 1: One Class Per File - COMPLETED ✅
| File | Status |
|------|--------|
| IAudioRenderer.h | ✅ |
| SyncPullRenderer.h/cpp | ✅ |
| CircularBufferRenderer.h/cpp | ✅ |
| SilentRenderer.h/cpp | ✅ |
| IAudioMode.h | ✅ |
| ThreadedAudioMode.cpp | ✅ |
| SyncPullAudioMode.cpp | ✅ |

### Priority 2: Folder Structure
```
src/
├── audio/
│   ├── renderers/   ✅ Done
│   └── modes/       ✅ Done
└── interfaces/     ⏳ For TMUI
```

### Priority 3: Bridge vs CLI Separation
| Component | Current | Target |
|-----------|---------|--------|
| Audio buffer ops | CLI | Bridge |
| Warmup phase | CLI | Bridge |
| Engine creation | CLI | Bridge |

### Priority 4: Interfaces for TMUI
- IEngineDataProvider
- IAudioOutput  
- IDisplayOutput

## SOLID Status
- SRP: ✅ Good
- OCP: ✅ Good (Strategy pattern)
- LSP: ✅ Good
- ISP: ✅ Good
- DIP: ✅ Good (Factory returns interfaces)
- DRY: ✅ Good (helpers extracted)

## Questions from User
1. Do abstract classes need separate headers? → In C++, yes for compilation
2. SyncPullAudioMode in IAudioMode.h? → Coupling issue, consider moving
