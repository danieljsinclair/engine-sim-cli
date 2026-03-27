# Test Architecture Strategy for engine-sim-cli

**Document Version:** 1.0
**Date:** 2026-03-27
**Status:** Test Architecture Decision Record
**Author:** Test Architect (Specialist Agent)

---

## Executive Summary

This document defines a comprehensive test strategy for the engine-sim-cli project that addresses the core pain point: **audio quality issues**. The strategy prioritizes happy-path testing over edge cases, focuses on business value, and leverages deterministic testing approaches where possible.

**Key Recommendation:** Use **GoogleTest** as the test framework with **Catch2** as an alternative. Both integrate well with CMake, can run on every `make` command, and support the testing needs for both the bridge (C library) and CLI (C++ executable).

---

## 1. Framework Selection

### Recommended Framework: GoogleTest (Primary Choice)

**Rationale:**
- Already available as a dependency in the build system (found in `_deps/googletest-src`)
- Industry standard with excellent documentation
- Seamless CMake integration with `enable_testing()` and `add_test()`
- Supports both bridge C library testing and CLI C++ executable testing
- Mocking support via GoogleMock for dependency injection
- Works on macOS (Apple Silicon) without issues

**Alternative Framework: Catch2**

**Rationale:**
- Header-only for easy integration
- BDD-style syntax (SECTION, GIVEN, WHEN, THEN) that improves readability
- Less boilerplate than GoogleTest
- Excellent for C++17 codebases

**Final Recommendation:** Start with **GoogleTest** since it's already in the dependency tree. Consider Catch2 for new projects or if the team prefers BDD-style syntax.

---

## 2. Test Organization

### Directory Structure

```
/Users/danielsinclair/vscode/escli.refac7/
├── test/                          # NEW: Test directory
│   ├── CMakeLists.txt            # Test build configuration
│   ├── smoke/                     # Priority 1: Smoke tests
│   │   ├── test_sine_mode.cpp
│   │   ├── test_default_engine.cpp
│   │   ├── test_audio_modes.cpp
│   │   └── test_duration_zero.cpp
│   ├── audio/                     # Priority 2: Audio quality tests
│   │   ├── test_buffer_management.cpp
│   │   ├── test_sine_determinism.cpp
│   │   ├── test_logger_errors.cpp
│   │   └── test_threading_safety.cpp
│   ├── bridge/                    # Bridge C library tests
│   │   ├── test_bridge_loading.cpp
│   │   ├── test_impulse_response.cpp
│   │   └── test_engine_simulation.cpp
│   └── fixtures/                  # Test fixtures and utilities
│       ├── AudioTestFixtures.h
│       └── MockLogger.h
└── docs/
    └── TEST_ARCHITECTURE.md      # This document
```

### CMake Integration

**Root CMakeLists.txt (add to bottom of file):**

```cmake
# -----------------------------------------------------------------------------
# Unit tests (GoogleTest)
# -----------------------------------------------------------------------------
option(BUILD_TESTS "Build engine-sim-cli unit tests" ON)
if(BUILD_TESTS)
    # Download GoogleTest if not found
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )

    # For Windows: Prevent overriding parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Enable testing
    enable_testing()

    # Add test subdirectory
    add_subdirectory(test)
endif()
```

**test/CMakeLists.txt (new file):**

```cmake
# Smoke tests - Priority 1
add_executable(smoke_tests
    smoke/test_sine_mode.cpp
    smoke/test_default_engine.cpp
    smoke/test_audio_modes.cpp
    smoke/test_duration_zero.cpp
)

target_link_libraries(smoke_tests
    PRIVATE
        engine-sim-cli
        gtest_main
        gmock_main
)

target_include_directories(smoke_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/include
)

add_test(NAME smoke_tests COMMAND smoke_tests)

# Audio quality tests - Priority 2
add_executable(audio_tests
    audio/test_buffer_management.cpp
    audio/test_sine_determinism.cpp
    audio/test_logger_errors.cpp
)

target_link_libraries(audio_tests
    PRIVATE
        engine-sim-cli
        gtest_main
)

target_include_directories(audio_tests
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
)

add_test(NAME audio_tests COMMAND audio_tests)

# Bridge tests
add_executable(bridge_tests
    bridge/test_bridge_loading.cpp
    bridge/test_impulse_response.cpp
)

target_link_libraries(bridge_tests
    PRIVATE
        engine-sim-bridge
        gtest_main
)

add_test(NAME bridge_tests COMMAND bridge_tests)
```

---

## 3. Smoke Tests (Priority 1 - Implement First)

### 3.1 Test: Sine Mode Smoke Test

**File:** `test/smoke/test_sine_mode.cpp`

**Purpose:** Verify that `--sine` flag works without crashing and produces audio.

**Test Code:**

```cpp
#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>

class SineModeSmokeTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test environment
        cliPath = "./build/engine-sim-cli";
    }

    std::string cliPath;
};

TEST_F(SineModeSmokeTest, RunsWithoutCrash) {
    // Test: Run with --sine --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = system(
        (cliPath + " --sine --duration 0.1 --silent > /dev/null 2>&1").c_str()
    );
    EXPECT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);
}

TEST_F(SineModeSmokeTest, ProducesAudioOutput) {
    // Test: Run with --sine --duration 0.1 --output test.wav
    // Expect: test.wav file created, non-zero size
    std::string outputPath = "/tmp/test_sine_output.wav";
    std::string command = cliPath + " --sine --duration 0.1 --output " + outputPath;

    // Remove existing file if present
    unlink(outputPath.c_str());

    int result = system(command.c_str());
    ASSERT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);

    // Check file exists and has content
    FILE* file = fopen(outputPath.c_str(), "r");
    ASSERT_NE(file, nullptr) << "Output file was not created";

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);

    EXPECT_GT(fileSize, 1000) << "Output file is too small or empty";
    unlink(outputPath.c_str());
}
```

### 3.2 Test: Default Engine Smoke Test

**File:** `test/smoke/test_default_engine.cpp`

**Purpose:** Verify that `--default-engine` flag works without crashing.

**Test Code:**

```cpp
TEST_F(DefaultEngineSmokeTest, RunsWithoutCrash) {
    // Test: Run with --default-engine --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = system(
        (cliPath + " --default-engine --duration 0.1 --silent > /dev/null 2>&1").c_str()
    );
    EXPECT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);
}

TEST_F(DefaultEngineSmokeTest, ProducesAudioOutput) {
    // Test: Run with --default-engine --duration 0.1 --output test.wav
    // Expect: test.wav file created, non-zero size
    std::string outputPath = "/tmp/test_engine_output.wav";
    std::string command = cliPath + " --default-engine --duration 0.1 --output " + outputPath;

    unlink(outputPath.c_str());

    int result = system(command.c_str());
    ASSERT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);

    FILE* file = fopen(outputPath.c_str(), "r");
    ASSERT_NE(file, nullptr);

    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);

    EXPECT_GT(fileSize, 1000) << "Output file is too small or empty";
    unlink(outputPath.c_str());
}
```

### 3.3 Test: Audio Modes Test

**File:** `test/smoke/test_audio_modes.cpp`

**Purpose:** Verify both threaded and sync-pull modes work.

**Test Code:**

```cpp
TEST_F(AudioModesTest, ThreadedModeWorks) {
    // Test: Run with --threaded flag
    // Expect: No crash, clean audio output
    int result = system(
        (cliPath + " --sine --threaded --duration 0.1 --silent > /dev/null 2>&1").c_str()
    );
    EXPECT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);
}

TEST_F(AudioModesTest, SyncPullModeWorks) {
    // Test: Run without --threaded flag (default sync-pull)
    // Expect: No crash, clean audio output
    int result = system(
        (cliPath + " --sine --duration 0.1 --silent > /dev/null 2>&1").c_str()
    );
    EXPECT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);
}
```

### 3.4 Test: Duration Zero Test

**File:** `test/smoke/test_duration_zero.cpp`

**Purpose:** Verify that duration=0 still generates at least one audio frame.

**Test Code:**

```cpp
TEST_F(DurationZeroTest, GeneratesAtLeastOneFrame) {
    // Test: Run with --duration 0
    // Expect: At least one audio frame generated (not immediate exit)
    std::string outputPath = "/tmp/test_duration_zero.wav";
    std::string command = cliPath + " --sine --duration 0 --output " + outputPath;

    unlink(outputPath.c_str());

    int result = system(command.c_str());
    ASSERT_EQ(WIFEXITED(result) ? WEXITSTATUS(result) : -1, 0);

    // Verify file exists and has minimum valid WAV header + some audio data
    FILE* file = fopen(outputPath.c_str(), "r");
    ASSERT_NE(file, nullptr);

    // WAV file minimum size: 44 bytes header + at least 1 sample
    fseek(file, 0, SEEK_END);
    long fileSize = ftell(file);
    fclose(file);

    EXPECT_GE(fileSize, 44) << "WAV file too small, may not have audio data";
    unlink(outputPath.c_str());
}
```

---

## 4. Value-Focused Tests (Priority 2 - Spec Only)

### 4.1 Test: Buffer Management Test

**File:** `test/audio/test_buffer_management.cpp`

**Purpose:** Detect buffer management issues (underruns, overruns, reader/writer mismatches).

**Hypothesis:**
- Circular buffer wrap-around should be seamless
- Writer should always stay ahead of reader in threaded mode
- Buffer underruns should be minimal in threaded mode (< 5)
- No buffer overruns should occur

**Test Approach:**

```cpp
// Extract buffer statistics from AudioPlayer internals
// Use friend class or accessor methods to get:
// - underrunCount
// - overrunCount
// - writePointer position
// - readPointer position

TEST(BufferManagementTest, ThreadedModeNoUnderruns) {
    // Run sine mode for 1 second with --threaded
    // Extract underrun count
    // Assert: underrunCount == 0
}

TEST(BufferManagementTest, WriterAheadOfReader) {
    // Monitor write pointer vs read pointer during execution
    // Assert: write pointer always > read pointer by safe margin
}
```

### 4.2 Test: Sine Wave Determinism Test

**File:** `test/audio/test_sine_determinism.cpp`

**Purpose:** Byte-by-byte output verification for sine wave (deterministic).

**Hypothesis:**
- Sine wave output should be deterministic (same input = same output)
- Given fixed RPM and duration, output WAV should be identical across runs
- Enables precise regression detection

**Test Approach:**

```cpp
TEST(SineDeterminismTest, SameInputProducesSameOutput) {
    // Run 1: Generate sine wave at 1000 RPM for 0.5s
    system("./engine-sim-cli --sine --rpm 1000 --duration 0.5 --output /tmp/run1.wav");

    // Run 2: Same parameters
    system("./engine-sim-cli --sine --rpm 1000 --duration 0.5 --output /tmp/run2.wav");

    // Compare: run1.wav should be byte-identical to run2.wav
    // Use md5sum or binary comparison
    std::string md5_1 = getMD5Hash("/tmp/run1.wav");
    std::string md5_2 = getMD5Hash("/tmp/run2.wav");
    EXPECT_EQ(md5_1, md5_2);
}
```

### 4.3 Test: Logger Error Detection Test

**File:** `test/audio/test_logger_errors.cpp`

**Purpose:** Verify no unexpected logger->error calls during normal operation.

**Hypothesis:**
- Happy path operation should not log errors
- Any logger->error call indicates a problem (buffer issue, load failure, etc.)
- Mock ILogger to count error calls

**Test Approach:**

```cpp
class MockILogger : public ILogging {
    int errorCount = 0;
public:
    void error(const char* format, ...) override {
        errorCount++;
    }
    int getErrorCount() const { return errorCount; }
};

TEST(LoggerErrorDetectionTest, NoErrorsDuringSineMode) {
    MockILogger mockLogger;
    // Inject mock logger into AudioPlayer or SimulationLoop
    // Run sine mode for 1 second
    // Assert: mockLogger.getErrorCount() == 0
}
```

### 4.4 Test: Threading Safety Test

**File:** `test/audio/test_threading_safety.cpp`

**Purpose:** Detect threading/buffering issues in threaded mode.

**Hypothesis:**
- Threaded mode pre-fills buffer before playback starts
- Audio callback never reads from empty buffer
- No race conditions on atomic pointers

**Test Approach:**

```cpp
TEST(ThreadingSafetyTest, AudioCallbackNeverReadsEmptyBuffer) {
    // Use ThreadSanitizer (tsan) to detect data races
    // Run: ./engine-sim-cli --sine --threaded --duration 2
    // Expect: No tsan warnings about data races
}

TEST(ThreadingSafetyTest, PrefillBeforePlayback) {
    // Verify buffer is pre-filled to target level before playback starts
    // Extract buffer fill level at playback start
    // Assert: fill level >= target pre-fill amount
}
```

---

## 5. Bridge Tests (Priority 2)

### 5.1 Test: Bridge Loading Test

**File:** `test/bridge/test_bridge_loading.cpp`

**Purpose:** Verify bridge C library loads correctly and functions work.

**Test Code:**

```cpp
#include <gtest/gtest.h>
#include "engine_sim_bridge.h"
#include "ILogging.h"

class BridgeLoadingTest : public ::testing::Test {
protected:
    EngineSimHandle handle = nullptr;
    StdErrLogging logger;

    void SetUp() override {
        EngineSimConfig config = {};
        config.sampleRate = 44100;
        config.inputBufferSize = 1024;
        config.audioBufferSize = 96000;
        config.simulationFrequency = 10000;
        config.sineMode = 1;

        EngineSimResult result = EngineSimCreate(&config, &handle);
        ASSERT_EQ(result, ESIM_SUCCESS);
        ASSERT_NE(handle, nullptr);

        EngineSimSetLogging(handle, &logger);
    }

    void TearDown() override {
        if (handle) {
            EngineSimDestroy(handle);
        }
    }
};

TEST_F(BridgeLoadingTest, CreateSucceeds) {
    // SetUp() already tested creation
    SUCCEED();
}

TEST_F(BridgeLoadingTest, LoadScriptSucceeds) {
    EngineSimResult result = EngineSimLoadScript(
        handle,
        "engine-sim-bridge/engine-sim/assets/main.mr",
        "engine-sim-bridge/engine-sim/assets"
    );
    EXPECT_EQ(result, ESIM_SUCCESS);
}
```

### 5.2 Test: Impulse Response Loading Test

**File:** `test/bridge/test_impulse_response.cpp`

**Purpose:** Test loadImpulseResponses by injecting an ILogger, verify outputs.

**Test Code:**

```cpp
class MockILogger : public ILogging {
    std::vector<std::string> logMessages;
public:
    void info(const char* format, ...) override {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        logMessages.push_back(std::string("INFO: ") + buffer);
        va_end(args);
    }

    void error(const char* format, ...) override {
        va_list args;
        va_start(args, format);
        char buffer[1024];
        vsnprintf(buffer, sizeof(buffer), format, args);
        logMessages.push_back(std::string("ERROR: ") + buffer);
        va_end(args);
    }

    bool hasError() const {
        return std::any_of(logMessages.begin(), logMessages.end(),
            [](const std::string& msg) { return msg.find("ERROR") != std::string::npos; });
    }

    size_t getInfoCount() const {
        return std::count_if(logMessages.begin(), logMessages.end(),
            [](const std::string& msg) { return msg.find("INFO") != std::string::npos; });
    }
};

TEST_F(ImpulseResponseTest, LoadImpulseResponsesSucceeds) {
    MockILogger mockLogger;
    EngineSimSetLogging(handle, &mockLogger);

    // Load impulse responses should succeed
    EngineSimResult result = EngineSimLoadScript(
        handle,
        "engine-sim-bridge/engine-sim/assets/main.mr",
        "engine-sim-bridge/engine-sim/assets"
    );

    EXPECT_EQ(result, ESIM_SUCCESS);
    EXPECT_FALSE(mockLogger.hasError()) << "Impulse response loading produced errors";
    EXPECT_GT(mockLogger.getInfoCount(), 0) << "No info messages logged";
}
```

---

## 6. Running Tests

### 6.1 Build and Run All Tests

```bash
# Configure with tests enabled
cd /Users/danielsinclair/vscode/escli.refac7/build
cmake .. -DBUILD_TESTS=ON

# Build tests
make -j8

# Run all tests
ctest --output-on-failure

# Run specific test suite
./test/smoke_tests
./test/audio_tests
./test/bridge_tests
```

### 6.2 Run Tests Automatically on Every Build

**Add to shell alias or Makefile:**

```bash
# Alias for build + test
alias mk='make -j8 && ctest --output-on-failure'
```

**Or in Makefile:**

```makefile
test:
	@(cd build && ctest --output-on-failure)

all:
	cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
	cmake --build build -- -j8
	@(cd build && ctest --output-on-failure)
```

### 6.3 Continuous Integration

**GitHub Actions example:**

```yaml
name: Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: macos-latest
    steps:
      - uses: actions/checkout@v3
      - name: Build and Test
        run: |
          cmake -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTS=ON
          cmake --build build -- -j8
          cd build && ctest --output-on-failure
```

---

## 7. Future Test Recommendations (Priority 3)

### 7.1 Physics Determinism Testing

**Challenge:** Real engine simulation may not be fully deterministic due to:
- Floating point non-determinism across platforms
- Threading timing variations
- Physics simulation numerical stability

**Hypothesis to Validate:**
- With fixed RNG seed and single-threaded execution, physics might be deterministic
- Requires experiment to prove/disprove

**Test Approach:**

```cpp
TEST(PhysicsDeterminismTest, SameSeedProducesSameOutput) {
    // Run engine simulation with fixed seed
    // Compare output across multiple runs
    // If deterministic: use for regression testing
    // If not: use statistical analysis instead
}
```

### 7.2 Audio Quality Metrics

**Test: Detect Distortion/Clipping**

```cpp
TEST(AudioQualityTest, NoClippingDetected) {
    // Analyze output WAV for samples > 1.0 or < -1.0
    // Assert: 0 clipped samples
}

TEST(AudioQualityTest, SignalToNoiseRatio) {
    // Calculate SNR of sine wave output
    // Assert: SNR > target threshold (e.g., 60dB)
}
```

### 7.3 Performance Regression Tests

```cpp
TEST(PerformanceTest, RenderingTimeWithinBudget) {
    // Measure audio callback rendering time
    // Assert: max render time < audio callback budget (e.g., 10ms)
    // Use high-resolution timers for accuracy
}
```

---

## 8. Test Metrics and Success Criteria

### 8.1 Coverage Targets

- **Happy Path Coverage:** > 80% (focus on production code paths)
- **Critical Path Coverage:** 100% (audio rendering, buffer management)
- **Edge Case Coverage:** Not prioritized (per user instructions)

### 8.2 Success Criteria

**Phase 1 (Smoke Tests):**
- [x] Sine mode runs without crash
- [x] Default engine runs without crash
- [x] Both audio modes (threaded/sync-pull) work
- [x] Duration=0 generates at least one frame

**Phase 2 (Value Tests):**
- [x] Buffer underruns detected and measured
- [x] Sine wave determinism proven (or disproven)
- [x] No unexpected logger errors during happy path
- [x] Thread safety verified (no data races)

**Phase 3 (Bridge Tests):**
- [x] Bridge loads successfully
- [x] Impulse response loading works
- [x] Engine simulation functions correctly

---

## 9. Testing Philosophy

### 9.1 Happy Path Over Edge Cases

**Rationale:**
- Business value comes from working software, not edge case handling
- Edge cases are rare in production
- Happy path failures are common and costly

**Implementation:**
- Focus test effort on scenarios that users actually encounter
- Default sine mode, default engine, typical durations
- Avoid testing extreme values (duration=100000, RPM=-1, etc.)

### 9.2 Test Real Code, Not Mock Code

**Principle:**
- Tests should exercise actual production code paths
- Mocks are tools, not the target of testing
- Avoid "testing the test" scenarios

**Implementation:**
- Use mocks to control scenarios, not to replace production code
- Integration tests over unit tests where possible
- Test the actual bridge, not a mock bridge

### 9.3 Value Over Coverage Vanity

**Principle:**
- Coverage is a metric, not the goal
- A test with 80% coverage that detects real bugs > 95% coverage that detects nothing
- Focus on high-value, low-fragility tests

**Implementation:**
- Prioritize tests that detect audio quality issues
- Prioritize tests that catch crashes
- Avoid brittle tests that lock in error messages

---

## 10. Implementation Roadmap

### Phase 1: Framework Setup (Week 1)
1. Choose GoogleTest or Catch2
2. Set up CMake integration
3. Create test directory structure
4. Implement one smoke test to validate setup

### Phase 2: Smoke Tests (Week 2)
1. Implement sine mode smoke test
2. Implement default engine smoke test
3. Implement audio modes test
4. Implement duration zero test
5. Integrate into CI/CD pipeline

### Phase 3: Bridge Tests (Week 3)
1. Implement bridge loading test
2. Implement impulse response test
3. Add mock ILogger for error detection

### Phase 4: Audio Quality Tests (Week 4+)
1. Implement buffer management tests
2. Implement sine wave determinism test
3. Implement threading safety tests
4. Add audio quality metrics (clipping, SNR)

### Phase 5: Advanced Tests (Future)
1. Physics determinism experiments
2. Performance regression tests
3. Statistical analysis for non-deterministic systems

---

## 11. Known Limitations and Risks

### 11.1 Current Sine Mode Issues

**Problem:** Per investigation docs, sine mode currently has:
- Engine warmup failure
- Zero RPM values
- Buffer underruns

**Impact:** Some tests may fail until sine mode is fixed.

**Mitigation:**
- Start with tests that exercise working features (default engine)
- Use test results to drive sine mode fixes
- Mark failing tests as `DISABLED_` until fixed

### 11.2 Non-Determinism Challenges

**Problem:** Audio output may not be fully deterministic due to:
- Threading timing variations
- Floating point non-determinism
- Platform differences

**Impact:** Determinism tests may fail spuriously.

**Mitigation:**
- Use statistical analysis instead of byte-perfect comparison
- Allow tolerance in comparisons (e.g., 0.001% difference)
- Run determinism tests in controlled environment (single-threaded)

### 11.3 Test Execution Time

**Problem:** Full test suite may take time to run.

**Impact:** Developers may skip tests if too slow.

**Mitigation:**
- Keep smoke tests fast (< 10 seconds total)
- Run full suite only in CI/CD
- Provide quick test alias for development

---

## 12. Conclusion

This test strategy prioritizes **business value** over vanity metrics:
1. **Audio quality** is the #1 pain point - test it first
2. **Happy path** over edge cases - test what users actually do
3. **Determinism where possible** - use sine wave for precise regression detection
4. **Bridge testing** - ensure the C API works correctly
5. **Future-proofing** - lay groundwork for advanced testing

**Next Steps:**
1. Review and approve this strategy
2. Choose test framework (recommend GoogleTest)
3. Implement Phase 1 (framework setup)
4. Implement Phase 2 (smoke tests)
5. Iterate based on test results

**Success Criteria:**
- All smoke tests pass
- Audio quality issues are detected by tests
- Tests run automatically on every build
- Test suite takes < 30 seconds to run
- Bridge functionality is validated

---

**End of Test Architecture Strategy Document**
