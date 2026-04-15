# ARCH-003: Phase 4 - IAudioPlatform Extraction

**Priority:** P1 - High Priority
**Status:** 🟡 Ready for Implementation
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @product-owner

## Overview

Extract platform-specific audio code from AudioPlayer into IAudioPlatform interface abstraction. This enables cross-platform support (macOS, iOS, ESP32) by properly abstracting platform-specific audio playback, buffering, and threading.

## Problem Statement

Current implementation has platform-specific code tightly coupled to AudioPlayer:
- CoreAudio-specific code embedded in AudioPlayer
- No abstraction for different audio platforms
- Difficult to support iOS and ESP32 platforms
- Violates Dependency Inversion Principle (depends on concretions)

## Objectives

1. **Interface Abstraction**: Create `IAudioPlatform` interface for platform-specific audio
2. **Separation of Concerns**: AudioPlayer delegates to IAudioPlatform
3. **Cross-Platform Support**: Enable macOS, iOS, and ESP32 implementations
4. **SOLID Compliance**: Follow Dependency Inversion Principle

## Acceptance Criteria

### Interface Design
- [ ] Create `IAudioPlatform` interface with audio lifecycle methods
- [ ] Define platform-specific operations (start, stop, render)
- [ ] Clean abstraction that works across platforms
- [ ] No platform-specific details leak to AudioPlayer

### CoreAudioPlatform Implementation
- [ ] Implement `IAudioPlatform` for macOS CoreAudio
- [ ] Existing AudioPlayer functionality preserved
- [ ] Thread safety maintained
- [ ] Performance characteristics unchanged

### AudioPlayer Refactoring
- [ ] AudioPlayer delegates to IAudioPlatform
- [ ] No CoreAudio-specific code in AudioPlayer
- [ ] IAudioStrategy integration maintained
- [ ] Existing tests continue to pass

### Testing
- [ ] `make test` passes completely
- [ ] All macOS audio tests pass
- [ ] Performance characteristics unchanged
- [ ] Test architect review passes

### Documentation
- [ ] IAudioPlatform interface documented
- [ ] AUDIO_MODULE_ARCHITECTURE.md updated
- [ ] Platform abstraction patterns documented
- [ ] iOS and ESP32 implementation guidance provided

## Technical Approach

### Interface Design
```cpp
class IAudioPlatform {
public:
    virtual ~IAudioPlatform() = default;

    // Lifecycle
    virtual bool initialize(const AudioConfig& config) = 0;
    virtual void start() = 0;
    virtual void stop() = 0;
    virtual void shutdown() = 0;

    // Configuration
    virtual void setRenderCallback(AudioRenderCallback callback) = 0;
    virtual AudioConfig getConfig() const = 0;

    // State
    virtual bool isRunning() const = 0;
    virtual size_t getBufferCapacity() const = 0;

    // Error handling
    virtual std::string getLastError() const = 0;
};
```

### AudioPlayer Refactoring
- AudioPlayer owns IAudioPlatform instance
- AudioPlayer delegates platform operations to IAudioPlatform
- IAudioStrategy integration continues to work
- No CoreAudio-specific code in AudioPlayer

### CoreAudioPlatform Implementation
- Migrate existing CoreAudio code from AudioPlayer
- Maintain existing behavior and performance
- Thread safety preserved
- Error handling improved

## Dependencies

- Phase 6: IAudioStrategy consolidation (should be done first)
- Current working AudioPlayer as baseline

## Risk Assessment

**Medium Risk:**
- Performance regression during refactoring
- Thread safety issues in platform abstraction
- Edge cases in platform-specific code

**Mitigation:**
- Incremental refactoring with tests at each step
- Test architect review required
- Performance comparison tests
- Preserve existing behavior where possible

## Definition of Done

- [ ] All acceptance criteria met
- [ ] `make test` passes completely
- [ ] Performance unchanged from baseline
- [ ] Documentation updated
- [ ] Code review approved by @tech-architect and @test-architect
- [ ] @product-owner final approval

## References

- `/Users/danielsinclair/vscode/escli.refac7/docs/AUDIO_MODULE_ARCHITECTURE.md`
- `/Users/danielsinclair/vscode/escli.refac7/docs/ARCHITECTURE_TODO.md`

---

**Created:** 2026-04-08
**Last Updated:** 2026-04-08
**Estimate:** 2-3 days

## Status: ✅ DONE (Production Ready)


