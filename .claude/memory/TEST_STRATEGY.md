# VirtualICE Twin — Test Strategy

## Overview
Comprehensive test strategy for the VirtualICE Twin project, covering unit, integration, and end-to-end testing with deterministic scenarios, fail-fast error handling, and CI integration.

---

## 1. Unit Test Matrix

### Classes to Test (TDD Red/Green/Refactor)

| Class / Component | Why Test | Coverage Expectation | Test Type | Key Behaviors |
|-------------------|----------|---------------------|-----------|---------------|
| **`DynoAssistedStrategy`** | Primary production strategy; maps EV telemetry → ICE RPM/gear | 95%+ line coverage on new code | RED: test `update()` with mocked signals<br>GREEN: verify target RPM computation, gear selection<br>REFACTOR: extract helper methods | - Target RPM = f(speed, gear ratios, diff ratio)<br>- Gear selection via ZF shift curves (throttle-dependent)<br>- Clutch engagement = 0.0 (dyno mode) |
| **`PhysicsDrivenStrategy`** | Research track; validates physics model viability | 90%+ line coverage | RED: test torque integration, clutch slip<br>GREEN: verify energy conservation, traction force<br>REFACTOR: extract part model interactions | - Engine torque → wheel torque conversion<br>- Clutch slip computation (`m_maxClutchTorque` limit)<br>- Wheel dynamics integration |
| **`SimpleGearbox`** (`IGearbox`) | Core shift logic; hysteresis prevents hunting | 95%+ branch coverage | RED: test shift thresholds, kickdown<br>GREEN: verify gear transitions, state machine<br>REFACTOR: extract shift timing logic | - Upshift at RPM ≥ threshold(throttle, gear)<br>- Downshift at RPM ≤ threshold(throttle, gear)<br>- Shift duration delay (200-500ms)<br>- Kickdown: throttle delta > 0.4 → downshift 1-2 gears |
| **`FrictionClutch`** (`IClutch`) | Models engagement, slip, torque transfer | 90%+ coverage | RED: test slip ratio, torque capacity<br>GREEN: verify engagement curve, lockup detection<br>REFACTOR: extract friction model evaluation | - Torque capacity = f(pressure, slip)<br>- Lockup when slip < threshold<br>- `m_maxClutchTorque` * pressure * slipFactor |
| **`SimpleWheel`** (`IWheel`) | Tire force computation, slip angle | 85%+ coverage | RED: test drive torque → acceleration<br>GREEN: verify slip, friction circle<br>REFACTOR: extract Pacejka/simple model | - Longitudinal force = f(torque, load, slip)<br>- Rolling resistance = f(speed)<br>- Lateral slip (steering angle) |
| **`MockTelemetryProvider`** | Test seam: deterministic signal generation | 90%+ coverage | RED: test `OnUpdateSimulation()` output<br>GREEN: verify time progression, signal bounds<br>REFACTOR: extract scenario math | - Generates `UpstreamSignal` from `ScenarioDefinition`<br>- No randomness; pure functions of time |
| **`IceVehicleProfile`** | Vehicle parameters loaded from .mr script | 90%+ coverage | RED: test parameter extraction<br>GREEN: verify defaults, validation<br>REFACTOR: separate loading from validation | - Mass, tire radius, diff ratio, gear ratios<br>- Queried from engine-sim or loaded from config |
| **`TwinModel`** (`ITwinModel` facade) | Orchestrates all components; critical path | 85%+ integration coverage | RED: test `update()` flow, error paths<br>GREEN: verify telemetry → strategy → controller<br>REFACTOR: extract telemetry read loop | - Combines telemetry, strategy, engine controller<br>- Handles missing data (staleness policy)<br>- Error propagation (no swallowing) |

### Coverage Expectations

- **New production code**: ≥90% line coverage (happy path + 2-3 edge cases)
- **Complex algorithms** (e.g., clutch slip, torque curves): ≥95%
- **Boundary conditions**: Explicit tests for min/max inputs
- **Error paths**: Each `throw` must have a test that triggers it

---

## 2. Integration Test Design

### Component Integration Points

```
ITelemetrySource (Mock) → ITwinStrategy → IEngineController (BridgeSimulator)
                                            ↓
                                    engine-sim (physics)
                                            ↓
                                    AudioHardware (Mock)
```

### 2.1 Deterministic Input Scenarios

All integration tests use **mathematically deterministic** telemetry sources — zero randomness.

#### Scenario 1: FullThrottleAcceleration
- **Duration**: 12 seconds
- **Throttle function**: `throttle(t) = 1 / (1 + exp(-(t - 4.0) / 1.5))` (sigmoid)
- **Expected RPM**: `800 + 5200 × throttle(t)`
- **Gear shifts**: 6 virtual gear transitions (1→2→3→4→5→6)
- **Validation**:
  - RPM tracking L2 error < 50 RPM
  - Shift timing error < ±0.35 s
  - Audio spectral centroid sweeps 130 → 1075 Hz
  - Harmonic correlation > 0.85

#### Scenario 2: SteadyCruise
- **Duration**: 30 seconds
- **Throttle**: constant 0.192 (≈1800 RPM @ EV gear ratio)
- **Speed**: 80 km/h constant
- **Validation**:
  - RPM mean ∈ [1746, 1854] (±3%)
  - RPM stddev < 40 RPM
  - Single gear held throughout
  - No hunting (>1 shift in 30s → FAIL)

#### Scenario 3: DecelerationBraking
- **Duration**: 15 seconds
- **Initial speed**: 100 km/h
- **Throttle**: 0.0 throughout
- **Expected RPM**: `6500 × exp(-t / 8.0)` (exponential decay)
- **Validation**:
  - RPM monotonically decreasing (all ΔRPM ≤ 0)
  - RPM(t=15s) < 300 RPM
  - L2 error < 5% of initial RPM
  - Audio energy decays exponentially

#### Scenario 4: StandstillLaunch
- **Duration**: 5 seconds
- **Starter motor**: engaged [0.0, 0.4) seconds
- **Throttle**: step 0 → 0.5 at t = 0.5 s
- **Expected RPM**: 800 (idle during cranking) → 3400 (after launch)
- **Validation**:
  - RPM ∈ [750, 850] during cranking
  - Crosses 2000 RPM within ±0.2 s of t = 0.5 s
  - Settles to 3400 ±5% by t = 1.5 s

### 2.2 Integration Flow (RED → GREEN → REFACTOR)

**RED Phase (Failing Test Compiles)**:
```cpp
// Test compiles but fails — implementation doesn't exist yet
TEST_F(DynoAssistedIntegrationTest, FullThrottleAcceleration) {
    TwinTestRunner runner;
    runner.setScenario(new FullThrottleAcceleration{});
    auto result = runner.run({});
    EXPECT_TRUE(result.passed); // Initially red
}
```

**GREEN Phase (Test Passes)**:
- Implement minimal `DynoAssistedStrategy::update()`
- Wire `ITwinModel` facade
- Add `BridgeSimulator` stub forwarding to engine-sim
- Test passes on deterministic data

**REFACTOR Phase (Cleanup)**:
- Extract helper methods
- Remove duplication
- Optimize for real-time (no heap alloc in hot path)
- Verify lock-free data structures for audio thread

### 2.3 Mock Components

#### MockTelemetryProvider (`IInputProvider`)
- Returns deterministic `EngineInput` from `ScenarioDefinition`
- No `rand()`, no `std::chrono` for signal generation
- Captures tick history for post-run analysis
- `tryRead()` is non-blocking; `read()` may block up to 10ms (USB timeout simulation)

#### MockAudioHardwareProvider (`IAudioHardwareProvider`)
- Captures audio samples to `std::vector<float>` in RAM
- Provides spectral analysis (`SpectralAnalyzer`)
- Computes: RMS, spectral centroid, harmonic profiles, fingerprints
- No hardware dependency — runs in CI

#### InMemoryTelemetry
- Thread-safe ring buffer for tick snapshots
- Allows test assertions against recorded data
- Zero-copy read access for validation

---

## 3. Objective Acceptance Criteria (per Component)

### 3.1 DynoAssistedStrategy
| Criterion | Threshold | Rationale |
|-----------|-----------|-----------|
| Target RPM computation accuracy | Error < 2% vs theoretical | Based on speed × gear ratio |
| Shift decision timing | Within ±100 RPM of threshold | Allows hysteresis tolerance |
| Clutch engagement | Always 0.0 in dyno mode | Clutch never engaged (dyno controls RPM) |
| Update latency | < 100 μs (no heap alloc) | Real-time audio thread safety |

### 3.2 SimpleGearbox
| Criterion | Threshold | Rationale |
|-----------|-----------|-----------|
| Upshift RPM (25% throttle) | 2,762 ± 50 RPM | Matches ZF 8HP coefficients |
| Upshift RPM (100% throttle) | 5,350 ± 50 RPM | Performance-oriented shift |
| Downshift hysteresis | ≥ 1,200 RPM gap at 25% | Prevents hunting |
| Shift duration | 200–500 ms (configurable) | Realistic mechanical delay |
| Kickdown response | < 200 ms | Responsive to driver input |

### 3.3 FrictionClutch
| Criterion | Threshold | Rationale |
|-----------|-----------|-----------|
| Torque capacity at pressure=1.0 | Configurable (1000-5000 Nm) | Matches `m_maxClutchTorque` |
| Slip ratio at peak torque | 0.05–0.15 | Typical clutch curve |
| Lockup threshold | Slip < 0.02 | Considered fully engaged |
| Engagement smoothness | No step discontinuities | Prevents jerks |

### 3.4 Wheel Dynamics
| Criterion | Threshold | Rationale |
|-----------|-----------|-----------|
| Traction limit | μ × verticalLoad (peak) | Friction circle enforcement |
| Rolling resistance | 1.5% of weight | Typical tire resistance |
| Slip → force curve | Monotonic to peak | Physically realistic |

### 3.5 TwinModel Orchestration
| Criterion | Threshold | Rationale |
|-----------|-----------|-----------|
| Telemetry staleness detection | > 100 ms old → reject | Prevents laggy control |
| Update period | 10 Hz (100 ms) nominal | Matches telemetry rate |
| Error propagation | Throws on unrecoverable error | Fail-fast principle |
| Strategy swap | < 1 ms (atomic pointer) | No audio glitches |

---

## 4. End-to-End Smoke Test Scenarios

### 4.1 AccelerationRun
**Goal**: Validate full 0 → 100 km/h with gear progression and RPM tracking.

**Steps**:
1. Initialize twin with `DynoAssistedStrategy`
2. Inject `FullThrottleAcceleration` scenario via `MockTelemetryProvider`
3. Run simulation for 12 seconds (100 Hz physics, 10 Hz telemetry)
4. Capture audio output via `MockAudioHardwareProvider`
5. Post-run analysis:
   - **RPM tracking**: L2 error < 50 RPM across entire run
   - **Shift count**: Exactly 6 gear changes detected
   - **Shift timing**: Each within ±0.35 s of expected
   - **Audio fingerprint**: Harmonic correlation > 0.85 vs golden file
   - **Spectral centroid**: Smooth sweep from ~130 Hz → ~1075 Hz (no jumps)

**Pass Criteria**: All metrics within tolerance + no crashes.

**Failure Modes**:
- RPM diverges → clutch/dyno tuning insufficient
- Missing shifts → gearbox thresholds too conservative
- Audio artifacts → discontinuities in torque commands

---

### 4.2 DecelerationBraking
**Goal**: Verify coast-down behavior and engine braking sound.

**Steps**:
1. Start at 100 km/h (6500 RPM) in 6th gear
2. Set throttle = 0.0, clutch = 1.0 (engaged)
3. Run for 15 seconds
4. Analyze:
   - **RPM decay**: Monotonic (all ΔRPM ≤ 0)
   - **Final RPM**: < 300 RPM at t = 15 s
   - **Energy conservation**: Kinetic energy loss matches drag work (±10%)
   - **Audio decay**: Spectral energy decays exponentially (no "stuck" tone)

**Pass Criteria**: Smooth, realistic coast-down with audible engine braking.

**Failure Modes**:
- RPM plateaus → clutch slip or dyno intervention
- Oscillations → control instability
- No engine braking → clutch not transmitting torque

---

### 4.3 SteadyCruise
**Goal**: Confirm stable operation at constant speed/throttle.

**Steps**:
1. Set constant throttle = 0.192 (≈1800 RPM target)
2. Maintain for 30 seconds
3. Analyze:
   - **RPM stability**: Stddev < 40 RPM
   - **Mean RPM**: 1800 ± 3%
   - **Gear changes**: 0 after initial selection (no hunting)
   - **Audio**: Clean harmonic comb, no beating/fluctuation

**Pass Criteria**: Rock-steady operation, no shift oscillations.

**Failure Modes**:
- Gear hunting → hysteresis too small
- RPM drift → load compensation too aggressive

---

### 4.4 DisconnectRecovery
**Goal**: Test fail-fast behavior when telemetry source fails.

**Steps**:
1. Run normally for 5 seconds
2. Simulate `ITelemetrySource` disconnection (returns `false` from `read()`)
3. Verify:
   - **Immediate failure**: `TwinModel::update()` throws or returns error within 1 update cycle
   - **No disguise**: Error is explicit (not hidden/silent)
   - **State preservation**: Last known good state retained
   - **Recovery**: On reconnection, resumes without restart

**Pass Criteria**: Fails fast and explicitly; recovers cleanly.

**Failure Modes**:
- Silent failure continues → dangerous
- Crashes instead of controlled error → poor UX
- Requires full restart → not resilient

---

## 5. Fail-Fast Error Handling Principles

### 5.1 Throw, Don't Disguise

| Situation | Correct Approach | Anti-Pattern |
|-----------|------------------|--------------|
| Telemetry timeout | Throw `TelemetryTimeoutError` with timestamp | Return stale data with warning flag |
| Out-of-range RPM (e.g., < 0) | Throw `RangeError` with value | Clamp silently and continue |
| Gear selection failed | Throw `GearboxError` with attempted gear | Stay in neutral without informing caller |
| Audio buffer underrun | Throw `AudioError` | Drop samples and continue with glitches |
| Strategy swap during shift | Return `false` with reason | Abort shift silently |

**Rationale**: Silent masking hides problems that compound into unpredictable behavior. Better to stop immediately and make the issue obvious.

### 5.2 Explicit Error Types (from `INTERFACE_DESIGN.md`)

```cpp
// Telemetry errors are explicit and non-throwing (error code pattern)
struct TelemetrySourceError {
    enum class Code { None, DeviceDisconnected, ParseError, Timeout, ... };
    Code code = Code::None;
    std::string message;
};
// ITelemetrySource returns bool; caller checks getLastError()

// Strategy/Model errors throw (fail-fast)
class TwinModelError : public std::runtime_error {
    using runtime_error::runtime_error;
};

// Strategy invariants throw
class StrategyError : public std::runtime_error { ... };
class GearboxError : public std::runtime_error { ... };
```

**Pattern**: 
- **I/O-bound components** (telemetry, audio) → error codes (recoverable)
- **Logic/core components** (strategy, model, gearbox) → exceptions (invariant violations, programming errors)

### 5.3 No Silent Swallowing

**Forbidden**:
```cpp
// ❌ NEVER DO THIS
void TwinModel::update(double dt) {
    try {
        telemetry.read(signal);
    } catch (...) {
        // Silent swallow — disaster!
    }
}
```

**Required**:
```cpp
// ✅ Explicit handling
void TwinModel::update(double dt) {
    if (!telemetry.read(signal)) {
        throw TwinModelError("Telemetry read failed: " + 
                              telemetry.getLastError().message);
    }
    // ... or return error code if part of contract
}
```

### 5.4 Assert Correct Behaviour (Not Error Handling)

As per CLAUDE.md: prioritize testing that the system works correctly over testing how it fails. Write tests that assert:
- "RPM tracks within 50 RPM" (correct behaviour)
- Not: "throws on bad telemetry" (error handling)

Exception: Fail-fast validation *of the framework itself* (e.g., "throws if telemetry disconnected during shift") is testable as error condition.

---

## 6. Test Isolation Techniques

### 6.1 Mock Telemetry (Deterministic Scenarios)

**Principle**: Replace real OBD2/CAN hardware with `MockTelemetryProvider` that replays mathematical scenarios.

**Implementation**:
```cpp
class MockTelemetryProvider : public ITelemetrySource {
    EngineInput OnUpdateSimulation(double dt) override {
        currentTime_ += dt;
        return EngineInput{
            .throttle = scenario_->throttleAt(currentTime_),
            .ignition = true,
            .starterMotorEngaged = false,
            .shouldContinue = (currentTime_ < scenario_->duration())
        };
    }
};
```

**Isolation Benefits**:
- No hardware dependencies → runs on CI
- Bitwise reproducible (same code → same samples on x86/ARM)
- No timing jitter from real sensor noise
- Can simulate edge cases (instant throttle jump) safely

### 6.2 Mock Audio Hardware

**Principle**: Capture audio to memory instead of outputting to speakers.

**Implementation**:
```cpp
int MockAudioHardwareProvider::callback(AudioBufferView& buffer) {
    std::lock_guard lock(mutex_);
    capture_.samples.insert(end(capture_.samples),
                           buffer.asFloat(),
                           buffer.asFloat() + buffer.totalInterleavedSamples());
    return userCallback_(buffer); // Forward to engine-sim's callback
}
```

**Isolation Benefits**:
- No sound card needed
- Enables spectral analysis of output
- Can compare against golden fingerprints
- Validates audio pipeline without side effects

### 6.3 Deterministic Scenarios

All scenarios use closed-form mathematical functions:

| Function | Use Case | Formula |
|----------|----------|---------|
| Sigmoid | Smooth throttle ramp | `1 / (1 + exp(-(t-4)/1.5))` |
| Exponential | Coast-down decay | `RPM0 × exp(-t/τ)` |
| Step | Instant transitions | `t < t0 ? a : b` |
| Linear | Constant acceleration | `a × t + b` |

**No `rand()`, no `std::normal_distribution`** — ensures tests are hermetic.

### 6.4 Test Doubles for Engine-Sim

For unit tests (not integration), substitute `ISimulator` with a mock that provides canned `EngineStats`:

```cpp
class MockSimulator : public ISimulator {
    MOCK_METHOD(void, setThrottle, (double), (override));
    MOCK_METHOD(double, getRpm, (), (const, override));
    // ... other methods
};
```

This allows testing `DynoAssistedStrategy` in isolation without spinning up full physics.

---

## 7. CI Integration Plan

### 7.1 GitHub Actions Workflow

```yaml
# .github/workflows/twin-validation.yml
name: Twin Validation

on:
  push:
    branches: [ master, develop ]
  pull_request:
    branches: [ master ]

jobs:
  build-and-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies
        run: |
          sudo apt-get update
          sudo apt-get install -y cmake ninja-build \
            libgtest-dev libgmock-dev

      - name: Configure CMake
        run: |
          cmake -B build -GNinja \
            -DCMAKE_BUILD_TYPE=Release \
            -DBUILD_TWIN_TESTS=ON \
            -DBUILD_TWIN_TEST_RUNNER=ON

      - name: Build twin library and tests
        run: cmake --build build --target twin_test_runner

      - name: Run deterministic twin tests
        run: |
          ./build/tests/twin_test_runner \
            --gtest_filter=*DynoAssisted*:*Gearbox*:*Integration* \
            --gtest_output=xml:test-results.xml

      - name: Run physics-driven tests (research track)
        run: |
          ./build/tests/twin_test_runner \
            --gtest_filter=*PhysicsDriven* \
            --gtest_output=xml:physics-results.xml
        continue-on-error: true  # Research track may be flaky

      - name: Upload test results
        uses: actions/upload-artifact@v4
        if: always()
        with:
          name: twin-test-artifacts
          path: |
            test-results.xml
            physics-results.xml
            test_artifacts/validation_reports/*.json
            test_artifacts/audio_fingerprints/*.wav

      - name: Quality gate — check coverage
        run: |
          # Use lcov/gcovr or custom coverage script
          ./scripts/check-coverage.sh --min 90 --component twin
```

### 7.2 Build Targets

New CMake targets added:

```cmake
# vehicle-twin library (standalone)
add_library(vehicletwin STATIC
    src/DynoAssistedStrategy.cpp
    src/SimpleGearbox.cpp
    src/FrictionClutch.cpp
    src/IceVehicleProfile.cpp
    # ... etc
)
target_include_directories(vehicletwin PUBLIC include)

# Twin test runner (deterministic scenarios)
add_executable(twin_test_runner
    test/runner/TwinTestRunner.cpp
    test/mocks/MockTelemetryProvider.cpp
    test/mocks/MockAudioHardwareProvider.cpp
    test/scenarios/*.cpp
    test/twin/EngineTwinTest.cpp
)
target_link_libraries(twin_test_runner
    vehicletwin
    enginesim
    gtest gmock
)
```

### 7.3 Golden File Regression

Baseline golden files stored in `test/golden/`:

```bash
test/golden/
├── full_throttle_acceleration.json
├── steady_cruise.json
├── deceleration_braking.json
└── standstill_launch.json
```

CI compares current run against golden:
```cpp
// If cosine similarity drops below 0.90 → FAIL
EXPECT_GT(fingerprint.cosineSimilarity, 0.90)
    << "Audio fingerprint drifted from golden baseline";
```

Golden files are updated manually after intentional changes (new gear ratios, different vehicle profile).

### 7.4 Flaky Test Detection

GitHub Actions `continue-on-error` for research track; main track must pass 100%. If a previously-passing test starts flaking:
1. Auto-retry once (in case of CI noise)
2. If still failing, block merge and alert
3. Quarantine flaky test with `DISABLED_` prefix until fixed

---

## 8. Test Environment

### 8.1 Headless Requirements

Zero real hardware dependencies:
- **No USB CAN adapter** → `MockTelemetryProvider` generates signals
- **No audio device** → `MockAudioHardwareProvider` captures to RAM
- **No display** → Pure CLI test runner
- **No Tesla vehicle** → CSV scenario replay

All tests runnable in GitHub Actions Ubuntu container.

### 8.2 Resource Limits

- **Memory**: < 100 MB per test run (audio capture buffer ~350 KB per 10 s)
- **CPU time**: < 30 s per scenario (10x real-time simulation)
- **Disk**: < 50 MB (golden files + artifacts)

### 8.3 Parallelization

Test scenarios are independent → run in parallel:
```yaml
strategy:
  matrix:
    scenario: [accel, cruise, brake, launch]
```

---

## 9. Documentation & Traceability

### 9.1 Test-to-Requirement Mapping

Each test references a requirement from design docs:

```cpp
// TEST_F(DynoAssistedTest, RpmTracksSpeed)
// Requirement: DYN-REQ-01
// Design doc: INTERFACE_DESIGN.md §3.2
// Evidence: CLUTCH_PHYSICS_ANALYSIS.md
TEST_F(DynoAssistedTest, RpmTracksSpeed) {
    // ...
}
```

### 9.2 Living Documentation

Test reports include:
- Design decision traceability
- Metrics over time (RPM error trend)
- Audio fingerprint similarity history
- Code coverage by component

Artifacts published to GitHub Pages or stored as build attachments.

---

## 10. Continuous Improvement

### 10.1 Test Quality Metrics

Track in CI:
- **Line coverage** on new code (target: ≥90%)
- **Mutation score** (using mutmut or similar) — kill mutants
- **Flakiness rate** (< 1%)
- **Determinism** (bitwise reproducibility across runs)

### 10.2 Red/Green/Refactor Discipline

**Mandatory workflow**:
1. Write RED test (compiles, fails)
2. Implement minimal GREEN (test passes)
3. REFACTOR (cleanup, optimize, remove duplication)
4. Verify all tests still pass

**No committing** without completing the cycle.

### 10.3 Architecture Critic Review

Before merging to `master`:
- SOLID compliance check (automated `clang-tidy` + human review)
- DRY scan (detect duplicated logic)
- Real-time safety review (no heap alloc in audio thread)
- Error handling review (no swallowed exceptions)

Reviewer checklist included in PR template.

---

## Summary

This test strategy enables deterministic, hermetic validation of the VirtualICE Twin without physical hardware. By combining mock telemetry, audio capture, spectral analysis, and golden-file regression, we can objectively verify that the engine twin produces correct RPM tracking, realistic gear shifts, and authentic audio — all within a CI pipeline. The fail-fast principle ensures problems surface immediately rather than silently compounding.

**Next Step**: Execute Phase 0 spikes to validate assumptions (audio quality, clutch tuning, timing jitter) before committing to full Phase 1 implementation.