# ARCH-005: iOS App Architecture

**Date:** 2026-04-16
**Status:** Architecture Decision Record (In Progress)

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
- **Status:** COMPLETE

**Considerations:**
- AVAudioEngine: Higher-level API, easier integration with iOS lifecycle. Provides sufficient control for engine simulation.
- RemoteIO: Lower-level, more control over audio pipeline. Unnecessary complexity for this use case.

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
