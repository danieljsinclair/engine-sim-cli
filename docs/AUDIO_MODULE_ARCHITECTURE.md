# Audio Module Architecture

**Document Version:** 1.0
**Date:** 2026-04-01
**Status:** Architecture Decision Record (ADR)
**Author:** Audio Systems Architect

---

## Executive Summary

This document defines a modular audio architecture where platform-specific audio output (AudioUnit, AVAudioEngine, I2S) is separated into replaceable modules. The bridge remains platform-agnostic, providing high-level simulation and audio generation APIs, while platform clients handle audio playback.

### Key Architectural Decisions

| Decision | Rationale |
|----------|-----------|
| **IAudioPlatform interface** | Abstracts platform-specific audio output (macOS CoreAudio, iOS AVAudioEngine, ESP32 I2S) |
| **CircularBuffer location** | Shared utility in `src/audio/common/` - used by all platform implementations |
| **Bridge.runSimulation()** | High-level orchestration - physics update + audio generation, NOT playback |
| **Module structure** | `src/audio/platform/{macos,ios,esp32}/` - one platform per folder |
| **Platform selection** | Compile-time selection via build flags, NOT runtime (different targets have different dependencies) |

---

## High-Level Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│                           Platform Targets                                │
│  ┌──────────┐   ┌──────────┐   ┌──────────┐   ┌──────────┐           │
│  │ macOS    │   │   iOS    │   │  ESP32   │   │  Future  │           │
│  │   CLI    │   │   App    │   │ Firmware │   │ Targets  │           │
│  └────┬─────┘   └────┬─────┘   └────┬─────┘   └────┬─────┘           │
│       │              │              │              │                   │
└───────┼──────────────┼──────────────┼──────────────┼───────────────────┘
        │              │              │              │
        │ IAudioPlatform              │              │
        │ (platform-specific impl)    │              │
        ▼              ▼              ▼              ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      Audio Platform Layer (NEW)                         │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  IAudioPlatform Interface (src/audio/platform/IAudioPlatform.h) │    │
│  │  - initialize(sampleRate, framesPerBuffer)                      │    │
│  │  - start() / stop()                                             │    │
│  │  - setVolume(float)                                             │    │
│  │  - renderCallback(buffer, frames) - called by platform         │    │
│  │  - setAudioSource(IAudioSource*)                                │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────┐             │
│  │  macOS Module  │  │   iOS Module   │  │  ESP32 Module  │             │
│  │ (CoreAudio)    │  │ (AVAudioEngine)│  │    (I2S)       │             │
│  │                │  │                │  │                │             │
│  │ CoreAudioAudio │  │  AVAudioAudio  │  │   I2SAudio     │             │
│  │ Platform       │  │  Platform      │  │   Platform     │             │
│  └────────────────┘  └────────────────┘  └────────────────┘             │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Uses
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                      Common Audio Utilities (SHARED)                    │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  CircularBuffer (src/audio/common/CircularBuffer.h)             │    │
│  │  - Thread-safe ring buffer                                     │    │
│  │  - Used by macOS and iOS modules                               │    │
│  │  - ESP32 may use simpler buffer due to memory constraints       │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  IAudioSource (src/audio/common/IAudioSource.h)                 │    │
│  │  - Interface for audio data source                              │    │
│  │  - Implemented by bridge-backed EngineAudioSource               │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    │ Calls
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│                        Engine-Sim Bridge                                  │
│  (Platform-agnostic C API - remains unchanged)                           │
│                                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  Lifecycle: Create, LoadScript, SetLogging, Destroy            │    │
│  │  Control: SetThrottle, SetIgnition, Update, GetStats           │    │
│  │  Audio: Render, RenderOnDemand, ReadAudioBuffer                │    │
│  └─────────────────────────────────────────────────────────────────┘    │
│                                                                           │
│  ┌─────────────────────────────────────────────────────────────────┐    │
│  │  runSimulation() - HIGH LEVEL ORCHESTRATION (NEW)              │    │
│  │  - NOT in bridge C API (bridge is low-level)                   │    │
│  │  - Implemented in C++ wrapper (EngineConfig or similar)        │    │
│  │  - Combines: Update(dt) + Render() / ReadAudioBuffer()         │    │
│  │  - Returns: audio buffer + engine stats                         │    │
│  └─────────────────────────────────────────────────────────────────┘    │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Interface Definitions

### 1. IAudioPlatform (NEW)

**Location:** `src/audio/platform/IAudioPlatform.h`

```cpp
namespace audio {
namespace platform {

/**
 * Platform-agnostic audio output interface.
 * Each platform (macOS, iOS, ESP32) implements this interface.
 * The platform layer handles all audio playback specifics.
 */
class IAudioPlatform {
public:
    virtual ~IAudioPlatform() = default;

    /**
     * Initialize the audio platform.
     * @param sampleRate Sample rate (e.g., 48000)
     * @param framesPerBuffer Buffer size for audio callback (e.g., 256, 512)
     * @return true if successful
     */
    virtual bool initialize(int sampleRate, int framesPerBuffer) = 0;

    /**
     * Start audio playback.
     * @return true if successful
     */
    virtual bool start() = 0;

    /**
     * Stop audio playback.
     */
    virtual void stop() = 0;

    /**
     * Set output volume.
     * @param volume Volume level (0.0 to 1.0)
     */
    virtual void setVolume(float volume) = 0;

    /**
     * Set the audio source for this platform.
     * The platform will call back to the source during audio rendering.
     * @param source Audio source implementation (caller retains ownership)
     */
    virtual void setAudioSource(IAudioSource* source) = 0;

    /**
     * Cleanup and release resources.
     */
    virtual void cleanup() = 0;

    /**
     * Get platform name for diagnostics.
     */
    virtual const char* getPlatformName() const = 0;
};

} // namespace platform
} // namespace audio
```

### 2. IAudioSource (ENHANCED)

**Location:** `src/audio/common/IAudioSource.h`

```cpp
namespace audio {

/**
 * Audio source interface.
 * Implementations provide audio data to the platform layer.
 */
class IAudioSource {
public:
    virtual ~IAudioSource() = default;

    /**
     * Generate audio samples.
     * Called by the audio platform during playback.
     *
     * @param buffer Output buffer (interleaved stereo float)
     * @param frames Number of frames to generate
     * @return Actual number of frames generated
     *
     * CRITICAL: This function MUST be allocation-free and real-time safe.
     * Called from audio thread - no blocking operations, no logging.
     */
    virtual int generateAudio(float* buffer, int frames) = 0;

    /**
     * Update audio source state (called from main thread).
     * @param deltaTime Time step in seconds
     */
    virtual void update(double deltaTime) = 0;
};

/**
 * Bridge-backed audio source implementation.
 * Uses engine-sim bridge to generate audio.
 */
class BridgeAudioSource : public IAudioSource {
public:
    BridgeAudioSource(EngineSimHandle handle, const EngineSimAPI& api);

    int generateAudio(float* buffer, int frames) override;
    void update(double deltaTime) override;

private:
    EngineSimHandle handle_;
    const EngineSimAPI& api_;
};

} // namespace audio
```

### 3. Platform Implementations

#### 3.1 macOS CoreAudio Platform

**Location:** `src/audio/platform/macos/CoreAudioPlatform.h`

```cpp
namespace audio {
namespace platform {
namespace macos {

/**
 * macOS audio platform using CoreAudio AudioUnit.
 * Wraps the existing AudioPlayer functionality.
 */
class CoreAudioPlatform : public IAudioPlatform {
public:
    CoreAudioPlatform();
    ~CoreAudioPlatform() override;

    bool initialize(int sampleRate, int framesPerBuffer) override;
    bool start() override;
    void stop() override;
    void setVolume(float volume) override;
    void setAudioSource(IAudioSource* source) override;
    void cleanup() override;
    const char* getPlatformName() const override { return "CoreAudio"; }

private:
    AudioUnit audioUnit_;
    AudioDeviceID deviceID_;
    IAudioSource* audioSource_;
    int sampleRate_;

    // Static audio callback
    static OSStatus audioCallback(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    );

    bool setupAudioUnit();
};

} // namespace macos
} // namespace platform
} // namespace audio
```

#### 3.2 iOS AVAudioEngine Platform

**Location:** `src/audio/platform/ios/AVAudioPlatform.h`

```cpp
#if TARGET_OS_IPHONE

#import <AVFoundation/AVFoundation.h>

namespace audio {
namespace platform {
namespace ios {

/**
 * iOS audio platform using AVAudioEngine.
 * Modern iOS audio API replacing legacy AudioUnit.
 */
class AVAudioPlatform : public IAudioPlatform {
public:
    AVAudioPlatform();
    ~AVAudioPlatform() override;

    bool initialize(int sampleRate, int framesPerBuffer) override;
    bool start() override;
    void stop() override;
    void setVolume(float volume) override;
    void setAudioSource(IAudioSource* source) override;
    void cleanup() override;
    const char* getPlatformName() const override { return "AVAudioEngine"; }

private:
    AVAudioEngine* engine_;
    AVAudioPlayerNode* playerNode_;
    AVAudioFormat* format_;
    IAudioSource* audioSource_;
    int sampleRate_;
    int framesPerBuffer_;

    // AVAudioNode render callback
    AVAudioPCMBuffer* renderAudio(UInt32 frames);
};

} // namespace ios
} // namespace platform
} // namespace audio

#endif // TARGET_OS_IPHONE
```

#### 3.3 ESP32 I2S Platform

**Location:** `src/audio/platform/esp32/I2SPlatform.h`

```cpp
#if defined(ESP32)

#include "driver/i2s.h"

namespace audio {
namespace platform {
namespace esp32 {

/**
 * ESP32 audio platform using I2S driver.
 * Optimized for embedded constraints (memory, CPU).
 */
class I2SPlatform : public IAudioPlatform {
public:
    I2SPlatform();
    ~I2SPlatform() override;

    bool initialize(int sampleRate, int framesPerBuffer) override;
    bool start() override;
    void stop() override;
    void setVolume(float volume) override;
    void setAudioSource(IAudioSource* source) override;
    void cleanup() override;
    const char* getPlatformName() const override { return "I2S"; }

private:
    i2s_port_t i2sPort_;
    IAudioSource* audioSource_;
    int sampleRate_;
    float volume_;
    TaskHandle_t audioTask_;

    // FreeRTOS task for audio generation
    static void audioTask(void* pvParameters);

    bool setupI2S();
    void generateAudio();
};

} // namespace esp32
} // namespace platform
} // namespace audio

#endif // ESP32
```

---

## Module Structure

```
src/audio/
├── common/                    # Shared audio utilities
│   ├── CircularBuffer.h/cpp   # Thread-safe ring buffer
│   ├── IAudioSource.h         # Audio source interface
│   └── BridgeAudioSource.h/cpp # Bridge-backed source
│
├── platform/                  # Platform-specific implementations
│   ├── IAudioPlatform.h       # Platform interface (ABSTRACTION)
│   │
│   ├── macos/                 # macOS CoreAudio implementation
│   │   ├── CoreAudioPlatform.h/cpp
│   │   └── CMakeLists.txt
│   │
│   ├── ios/                   # iOS AVAudioEngine implementation
│   │   ├── AVAudioPlatform.h/cpp
│   │   └── CMakeLists.txt
│   │
│   └── esp32/                 # ESP32 I2S implementation
│       ├── I2SPlatform.h/cpp
│       └── CMakeLists.txt
│
├── modes/                     # Audio mode strategies (EXISTING - no change)
│   ├── IAudioMode.h
│   ├── ThreadedAudioMode.h/cpp
│   ├── SyncPullAudioMode.h/cpp
│   └── AudioModeFactory.h/cpp
│
└── renderers/                 # Audio renderer strategies (EXISTING - no change)
    ├── IAudioRenderer.h
    ├── SyncPullRenderer.h/cpp
    ├── CircularBufferRenderer.h/cpp
    └── SilentRenderer.h/cpp
```

---

## Platform Selection Strategy

### Compile-Time Selection (Recommended)

Different platforms have fundamentally different build environments and dependencies. Runtime selection adds unnecessary complexity.

**CMake Approach:**

```cmake
# Platform detection
if(APPLE)
    if(IOS)
        set(AUDIO_PLATFORM_SOURCES
            src/audio/platform/ios/AVAudioPlatform.cpp
        )
        find_library(AVFOUNDATION_LIB AVFoundation REQUIRED)
    else()
        set(AUDIO_PLATFORM_SOURCES
            src/audio/platform/macos/CoreAudioPlatform.cpp
        )
        find_library(COREAUDIO_LIB CoreAudio REQUIRED)
        find_library(AUDIOTOOLBOX_LIB AudioToolbox REQUIRED)
    endif()
elseif(ESP32)
    set(AUDIO_PLATFORM_SOURCES
        src/audio/platform/esp32/I2SPlatform.cpp
    )
    # ESP-IDF build system handles I2S driver linking
endif()

add_library(audio_platform ${AUDIO_PLATFORM_SOURCES})
target_link_libraries(audio_platform ${PLATFORM_LIBS})
```

### Factory Function

**Location:** `src/audio/platform/AudioPlatformFactory.h`

```cpp
namespace audio {
namespace platform {

// Factory function - implemented separately for each platform
// Each platform's build links to its own implementation

#if defined(__APPLE__)
    #if TARGET_OS_IPHONE
        #include "ios/AVAudioPlatform.h"
        inline std::unique_ptr<IAudioPlatform> createAudioPlatform() {
            return std::make_unique<ios::AVAudioPlatform>();
        }
    #else
        #include "macos/CoreAudioPlatform.h"
        inline std::unique_ptr<IAudioPlatform> createAudioPlatform() {
            return std::make_unique<macos::CoreAudioPlatform>();
        }
    #endif
#elif defined(ESP32)
    #include "esp32/I2SPlatform.h"
    inline std::unique_ptr<IAudioPlatform> createAudioPlatform() {
        return std::make_unique<esp32::I2SPlatform>();
    }
#endif

} // namespace platform
} // namespace audio
```

---

## Bridge API Clarifications

### What Bridge Does (Platform-Agnostic)

- **Physics Simulation:** `Update(dt)` - advances engine simulation
- **Audio Generation:** `Render()` / `RenderOnDemand()` - generates audio samples
- **Control:** `SetThrottle()`, `SetIgnition()`, etc.
- **Diagnostics:** `GetStats()`, `GetLastError()`

### What Bridge Does NOT Do (Platform-Specific)

- **NOT** audio playback - that's the platform's responsibility
- **NOT** managing audio devices - platform-specific
- **NOT** volume control - platform handles this

### runSimulation() - High-Level Orchestration

**IMPORTANT:** `runSimulation()` is NOT a bridge C API function. It's a C++ convenience wrapper (in `EngineConfig` or similar) that combines bridge calls:

```cpp
// In EngineConfig or SimulationLoop (C++ wrapper, NOT bridge C API)
void runSimulation(double dt) {
    // 1. Update physics
    api_->Update(handle_, dt);

    // 2. Generate audio (depending on mode)
    if (syncPullMode_) {
        // Audio generated on-demand in callback
        // Nothing to do here
    } else {
        // Threaded mode: generate audio and fill buffer
        api_->Render(handle_, audioBuffer_, bufferFrames_);
    }

    // 3. Audio playback is handled by platform module
    // Platform's audio callback will call BridgeAudioSource::generateAudio()
}
```

**The bridge C API remains unchanged.** `runSimulation()` is a convenience in the C++ wrapper layer.

---

## CircularBuffer Location Decision

**Decision:** `CircularBuffer` lives in `src/audio/common/` as shared utility.

**Rationale:**

1. **Used by multiple platforms:** macOS and iOS both benefit from the ring buffer implementation
2. **Platform-agnostic:** Pure C++ implementation with no platform dependencies
3. **Testable:** Can be unit tested independently
4. **ESP32 may opt out:** Due to memory constraints, ESP32 might use a simpler buffer

**NOT in bridge:**
- Bridge is a C API wrapper around engine-sim
- CircularBuffer is a client-side utility for audio streaming
- Putting it in bridge would violate SRP

---

## Migration Path

### Phase 1: Create Platform Abstraction (Week 1)

1. Create `src/audio/platform/` directory structure
2. Define `IAudioPlatform` interface
3. Define `IAudioSource` interface (enhanced from existing)
4. Move `CircularBuffer` to `src/audio/common/`

### Phase 2: Refactor macOS Implementation (Week 2)

1. Extract `AudioPlayer` CoreAudio code into `CoreAudioPlatform`
2. Create `BridgeAudioSource` adapter
3. Update CLI to use `IAudioPlatform` interface
4. Test macOS CLI functionality

### Phase 3: iOS Implementation (Week 3-4)

1. Implement `AVAudioPlatform` for iOS
2. Create iOS app project structure
3. Integrate with bridge
4. Test iOS audio output

### Phase 4: ESP32 Implementation (Week 5-6)

1. Implement `I2SPlatform` for ESP32
2. Adapt for memory constraints
3. Test on ESP32 hardware

### Phase 5: Cleanup (Week 7)

1. Remove old `AudioPlayer.cpp/h` (replaced by `CoreAudioPlatform`)
2. Update documentation
3. Final testing across all platforms

---

## SOLID Compliance

| Principle | Application | Status |
|-----------|-------------|--------|
| **SRP** | Each platform module handles only its audio output | ✅ Design |
| **OCP** | New platforms added via interface, no modification needed | ✅ Design |
| **LSP** | All platforms implement `IAudioPlatform` contract | ✅ Design |
| **ISP** | Focused interface - only audio output methods | ✅ Design |
| **DIP** | High-level code depends on `IAudioPlatform` abstraction | ✅ Design |

---

## Testing Strategy

### Unit Tests

- `CircularBuffer` - thread safety, boundary conditions
- `BridgeAudioSource` - bridge API integration
- Platform mocks - test `IAudioPlatform` interface compliance

### Integration Tests

- macOS CLI with `CoreAudioPlatform`
- Bridge + `BridgeAudioSource` + platform
- Audio generation across different modes

### Platform Tests

- macOS: CoreAudio callback timing
- iOS: AVAudioEngine session management
- ESP32: I2S driver integration, memory usage

---

## Success Criteria

1. ✅ macOS CLI functionality unchanged after refactoring
2. ✅ iOS app can play engine audio
3. ✅ ESP32 firmware can output engine audio
4. ✅ Bridge remains platform-agnostic (no platform-specific code)
5. ✅ All platforms share `CircularBuffer` and `IAudioSource`
6. ✅ SOLID principles maintained
7. ✅ No increase in binary size for existing platforms

---

## Open Questions

1. **ESP32 memory constraints:** Should ESP32 use a simpler buffer instead of `CircularBuffer`?
   - **Recommendation:** Start with `CircularBuffer`, optimize if needed

2. **iOS audio session management:** Who handles interruptions (calls, alarms)?
   - **Recommendation:** `AVAudioPlatform` should handle session management

3. **Volume control:** Should volume be in bridge or platform?
   - **Decision:** Platform - bridge generates audio at full scale

---

## Appendix: File Migration Plan

| Current Location | New Location | Action |
|------------------|--------------|--------|
| `src/AudioPlayer.cpp/h` | `src/audio/platform/macos/CoreAudioPlatform.cpp/h` | Refactor |
| `src/AudioSource.cpp/h` | `src/audio/common/BridgeAudioSource.cpp/h` | Refactor |
| `src/CircularBuffer.cpp/h` | `src/audio/common/CircularBuffer.cpp/h` | Move |
| (NEW) | `src/audio/platform/IAudioPlatform.h` | Create |
| (NEW) | `src/audio/platform/ios/AVAudioPlatform.cpp/h` | Create |
| (NEW) | `src/audio/platform/esp32/I2SPlatform.cpp/h` | Create |

---

*End of Audio Module Architecture Document*
