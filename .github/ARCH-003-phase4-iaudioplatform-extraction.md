# ARCH-003: Phase 4 - IAudioPlatform Extraction

**Priority:** P1 - High Priority
**Status:** ✅ COMPLETE
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @product-owner

## Overview

Extract platform-specific audio code from AudioPlayer into `IAudioHardwareProvider` interface abstraction. This enables cross-platform support (macOS, iOS, ESP32) by properly abstracting platform-specific audio playback, buffering, and threading.

**Note:** The original proposal used the name `IAudioPlatform`. The actual implementation is named `IAudioHardwareProvider`.

## Problem Statement (Original)

Previous implementation had platform-specific code tightly coupled to AudioPlayer:
- CoreAudio-specific code embedded in AudioPlayer
- No abstraction for different audio platforms
- Difficult to support iOS and ESP32 platforms
- Violated Dependency Inversion Principle (depends on concretions)

## As-Is State (Current Codebase)

### Interface (Actual name: `IAudioHardwareProvider`)

```cpp
// src/audio/hardware/IAudioHardwareProvider.h
class IAudioHardwareProvider {
public:
    virtual ~IAudioHardwareProvider() = default;

    // Lifecycle
    virtual bool initialize(const AudioStreamFormat& format) = 0;
    virtual void cleanup() = 0;

    // Playback Control
    virtual bool startPlayback() = 0;
    virtual void stopPlayback() = 0;

    // Volume Control
    virtual void setVolume(double volume) = 0;
    virtual double getVolume() const = 0;

    // Callback Registration
    using AudioCallback = std::function<int(void* refCon, void* actionFlags,
                                           const void* timeStamp,
                                           int busNumber, int numberFrames,
                                           PlatformAudioBufferList* ioData)>;
    virtual bool registerAudioCallback(const AudioCallback& callback) = 0;

    // Diagnostics
    virtual AudioHardwareState getHardwareState() const = 0;
    virtual void resetDiagnostics() = 0;
};
```

**Supporting types:** `AudioStreamFormat`, `PlatformAudioBufferList`, `AudioHardwareState` are all defined in `IAudioHardwareProvider.h`.

### CoreAudioHardwareProvider Implementation

```cpp
// src/audio/hardware/CoreAudioHardwareProvider.h
class CoreAudioHardwareProvider : public IAudioHardwareProvider {
    // Full implementation wrapping CoreAudio AudioUnit
    // - setupAudioUnit(), configureAudioFormat(), registerCallbackWithAudioUnit()
    // - coreAudioCallbackWrapper() (static bridge from CoreAudio to AudioCallback)
};
```

The implementation wraps all CoreAudio operations:
- AudioUnit lifecycle (create, initialize, cleanup)
- Audio format configuration (sample rate, channels, float/integer)
- Playback control via `AudioOutputUnitStart`/`AudioOutputUnitStop`
- Volume control via `AudioUnitSetParameter`
- Callback registration via `AURenderCallbackStruct`
- Static `coreAudioCallbackWrapper` bridges CoreAudio's C callback to the `AudioCallback` function

### AudioPlayer Integration (Actual)

```cpp
// src/AudioPlayer.h
class AudioPlayer {
private:
    std::unique_ptr<IAudioHardwareProvider> hardwareProvider_;
    IAudioStrategy* strategy_;
    BufferContext context_;
    CircularBuffer circularBuffer_;
    // ...
};
```

AudioPlayer creates `CoreAudioHardwareProvider` directly in `initialize()`:
```cpp
hardwareProvider_ = std::make_unique<CoreAudioHardwareProvider>(logger_);
```

AudioPlayer delegates to `hardwareProvider_` for:
- `start()` -> `hardwareProvider_->startPlayback()`
- `stop()` -> `hardwareProvider_->stopPlayback()`
- `setVolume()` -> `hardwareProvider_->setVolume()`

### Factory (Exists but Unused)

`AudioHardwareProviderFactory` exists in `src/audio/hardware/AudioHardwareProviderFactory.cpp` with a `createProvider()` static method, but `AudioPlayer` creates `CoreAudioHardwareProvider` directly rather than using the factory.

## Acceptance Criteria Status

### Interface Design
- [x] `IAudioHardwareProvider` interface created with lifecycle, playback, volume, callback, and diagnostic methods
- [x] Platform-agnostic types: `AudioStreamFormat`, `PlatformAudioBufferList`, `AudioHardwareState`
- [x] Clean abstraction with no CoreAudio details leaking through the interface
- [x] `AudioCallback` uses platform-agnostic parameter types

### CoreAudioHardwareProvider Implementation
- [x] Full macOS CoreAudio implementation
- [x] AudioUnit lifecycle managed internally
- [x] Thread safety via CoreAudio's real-time callback thread
- [x] Diagnostic state tracking (underruns, overruns)

### AudioPlayer Refactoring
- [x] AudioPlayer owns `IAudioHardwareProvider` via `std::unique_ptr`
- [x] AudioPlayer delegates to `hardwareProvider_` for all platform operations
- [x] IAudioStrategy integration maintained
- [x] Existing tests continue to pass

### Gaps
- AudioPlayer creates `CoreAudioHardwareProvider` directly instead of via `AudioHardwareProviderFactory`
- The `static audioUnitCallback` method still exists in AudioPlayer (legacy from before extraction), though it is not registered with the hardware provider -- only the lambda-based callback registered via `hardwareProvider_->registerAudioCallback()` is used
- No iOS or ESP32 implementations yet (expected -- these are future platforms)

## References

- `/Users/danielsinclair/vscode/escli.refac7/src/audio/hardware/IAudioHardwareProvider.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/hardware/CoreAudioHardwareProvider.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/hardware/CoreAudioHardwareProvider.cpp`
- `/Users/danielsinclair/vscode/escli.refac7/src/audio/hardware/AudioHardwareProviderFactory.cpp`
- `/Users/danielsinclair/vscode/escli.refac7/src/AudioPlayer.h`
- `/Users/danielsinclair/vscode/escli.refac7/src/AudioPlayer.cpp`

---

**Created:** 2026-04-08
**Last Updated:** 2026-04-15
**Estimate:** 2-3 days


