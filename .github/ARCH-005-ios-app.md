# ARCH-005: iOS App Architecture

**Date:** 2026-04-16
**Status:** Architecture Decision Record (Active)

## Context

Building an iOS application using the engine-sim-bridge library for real-time engine audio generation and playback.

### Team Composition

- **tech-architect** - Implementation
- **build-architect** - Build/Xcode configuration
- **test-architect** - TDD testing
- **doc-writer** - Documentation (tracking this file)

## Architecture Decisions

### 1. Hardware Adapter Choice

**Options:**
- **AVAudioEngine** - iOS built-in audio engine
- **RemoteIO** - iOS audio I/O API

**Decision:** Use AVAudioEngine
- **Rationale:** Higher-level API, easier integration with iOS lifecycle. RemoteIO is lower-level but unnecessary complexity for this use case. KISS principle applies.
- **Status:** IMPLEMENTED (AVAudioEngineHardwareProvider.mm)
- **Implementation Details:**
  - AVAudioEngine with AVAudioSourceNode for real-time audio generation
  - AVAudioSession configured with .playback category
  - AudioCallback integration via AVAudioSourceNode render block
  - Follows IAudioHardwareProvider interface pattern
  - Factory integration in AudioHardwareProviderFactory for iOS platform detection

**Considerations:**
- AVAudioEngine: Higher-level API, easier integration with iOS lifecycle. Provides sufficient control for engine simulation.
- RemoteIO: Lower-level, more control over audio pipeline. Unnecessary complexity for this use case.

---

### 2. Audio Buffer Design

**Decision:** Use AudioBufferDescriptor (platform-agnostic)
- **Rationale:** Decouples bridge library from CoreAudio-specific types (AudioBufferList*), enabling iOS/macOS cross-platform support
- **Status:** IMPLEMENTED (AudioTypes.h)
- **Implementation Details:**
  - Simple struct: `struct AudioBufferDescriptor { float* buffer; int frameCount; int channelCount; }`
  - Platform adapters (CoreAudioHardwareProvider, AVAudioEngineHardwareProvider) convert from platform-specific types
  - Strategy layer uses platform-agnostic AudioBufferDescriptor
  - Legacy PlatformAudioBufferList kept for backward compatibility during migration

**Decision Criteria:**
- PlatformAudioBufferList: Redesign to use a simple struct with float* data, int frameCount, int channels. No void* tricks. Each platform adapter fills it directly.
- AudioCallback: Simplify to `std::function<void(float* buffer, int frames, int channels)>`. Platform adapter handles the translation from platform callback.
- App Architecture: Single-view SwiftUI app, MVVM pattern, ObjC++ wrapper (like VehicleSimWrapper in vehicle-sim).

---

### 3. iOS SwiftUI App Architecture

**Decision:** Build SwiftUI app with MVVM pattern and ObjC++ wrapper
- **Rationale:** SwiftUI provides modern iOS UI framework, MVVM enables clean separation of concerns, ObjC++ wrapper bridges C++ API to Swift
- **Status:** IMPLEMENTED (escli-ios/EngineSimApp/)
- **Implementation Details:**
  - **EngineSimWrapper.h/.mm**: Thin ObjC++ wrapper around engine_sim_bridge C API
  - **EngineSimViewModel.swift**: ObservableObject with 10Hz timer, published properties for telemetry
  - **ContentView.swift**: SwiftUI UI with throttle slider, ignition toggle, starter button, RPM display
  - **EngineSimApp.swift**: @main entry point
  - **EngineSim-Bridging-Header.h**: Exposes Objective-C++ classes to Swift
  - **Info.plist**: iOS app configuration with audio background mode
  - **MVVM Pattern**: ViewModel owns wrapper, View binds to published properties
  - **Controls**: Throttle slider (0-100%), ignition toggle, starter motor button
  - **Telemetry Display**: Real-time RPM, load, exhaust flow, manifold pressure

**UI Controls:**
- Throttle slider: 0-100% range, updates EngineSimSetThrottle
- Ignition toggle: Enables/disables ignition system
- Starter button: Momentary button for starter motor
- Real-time telemetry: RPM, throttle position, engine load, exhaust flow, manifold pressure

**Architecture Pattern:**
- Single-view SwiftUI app following MVVM pattern
- ObjC++ wrapper delegates to engine_sim_bridge C API (follows vehicle-sim pattern)
- ViewModel owns wrapper instance, manages lifecycle and 10Hz update timer
- View binds to ViewModel's @Published properties via @StateObject
- Thin wrapper layer (SRP) - no simulation logic, delegates entirely to C API

---

### 4. TDD Test Infrastructure

**Decision:** Implement comprehensive GTest-based test suite for iOS components
- **Rationale:** TDD approach ensures quality, GTest provides consistent infrastructure with existing bridge tests, tests must add real business value
- **Status:** IMPLEMENTED (engine-sim-bridge/test/iOSAdapterTests.cpp)
- **Implementation Details:**
  - **Test Framework**: Google Test (GTest) - consistent with existing bridge tests
  - **Test Count**: 15 tests (value over volume - cut from original 29)
  - **Test Groups**: 5 test groups covering different layers and scenarios
    - Group 1: AudioBufferDescriptor construction and data access (3 tests)
    - Group 2: AVAudioEngineHardwareProvider lifecycle (4 tests)
    - Group 3: Callback wiring verification (2 tests)
    - Group 4: Full pipeline integration (3 tests)
    - Group 5: Callback stress tests (2 tests)
    - Group 6: ObjC++ wrapper lifecycle (1 test)
  - **Business Value Focus**: Tests validate real business scenarios, not coverage vanity
    - Happy path testing prioritized
    - Reasonable edge cases included
    - No fragile tests that lock in strict error messages
    - Tests for actual production code, not mocks or external code

**Test Categories:**
- **Unit Tests**: AudioBufferDescriptor construction, data access, frame/channel calculations
- **Lifecycle Tests**: Initialize, startPlayback, stopPlayback, cleanup, volume control
- **Integration Tests**: Full audio pipeline with real engine (sine mode), SyncPull and Threaded strategies
- **Stress Tests**: Callback race conditions, rapid start/stop cycles, shutdown during callback
- **Validation Tests**: Callback invocation, buffer writability, audio generation verification

**Testing Principles Applied:**
- **Happy Path First**: Core functionality tested before edge cases
- **Business Value**: Tests validate critical user-facing functionality (audio output, controls, lifecycle)
- **No Fragile Tests**: Error assertions focus on intent, not exact messages
- **TDD Compliance**: All tests compile and follow red/green/refactor pattern
- **iOS Simulator Support**: Tests designed to run on iOS simulator environment

---

**App Requirements:**
- Single-view iOS app with engine simulation controls
- Controls: throttle slider (0-100%), ignition toggle, starter motor button
- Audio output through device speakers
- Real-time RPM/load/exhaust display
- Must use engine-sim-bridge library via ObjC++ wrapper

**Decision Criteria:**
- Hardware Adapter: Use AVAudioEngine (higher-level, easier lifecycle, sufficient for engine sim). RemoteIO is lower-level but unnecessary complexity for this use case. KISS principle applies.
- PlatformAudioBufferList: Redesign to use a simple struct with float* data, int frameCount, int channels. No void* tricks. Each platform adapter fills it directly.
- AudioCallback: Simplify to `std::function<void(float* buffer, int frames, int channels)>`. Platform adapter handles the translation from platform callback.
- App Architecture: Single-view SwiftUI app, MVVM pattern, ObjC++ wrapper (like VehicleSimWrapper in vehicle-sim).

---

*This document will be updated as architectural decisions are made during iOS app development.*
