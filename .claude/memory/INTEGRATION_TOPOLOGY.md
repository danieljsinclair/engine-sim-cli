---
name: Integration Topology
description: Zero-coupling dependency architecture for vehicle-twin module
type: reference
---

Integration topology document specifying build-decoupled vehicle-twin module:
- vehicle-twin depends on nothing; bridge depends on vehicle-twin; engine-sim standalone
- iOS app wiring: TwinAdapter composes ISimulator + ITwinModel in-process
- ESP32 wiring: CANInputProvider feeds twin model via ITelemetrySource impl
- CLI mode: MockTelemetrySource injection for headless scenarios
- Threading: 10 Hz simulation → 60 Hz interpolated read, single-threaded
- Repository: engine-sim-bridge owns bridge interface; vehicle-twin/ owns twin model
  (separate CMake target, standalone library)
