// test_silent_flag.cpp - Smoke tests for --silent flag
//
// Purpose: Verify that --silent flag works correctly.
// The --silent flag runs the full audio pipeline but at zero volume.
// This is useful for testing the audio pipeline without playing audio.

#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include "SmokeTestHelper.h"

class SilentFlagTest : public ::testing::Test {
protected:
    // Helper to run CLI from project root directory
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }
};

TEST_F(SilentFlagTest, SilentModeWorks) {
    // Test: Run with --silent flag
    // Expect: No crash, exit code 0
    int result = runCLI("--sine --silent --duration 0.1 > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --silent flag. Exit code: " << exitCode;
}

TEST_F(SilentFlagTest, SilentModeProducesOutput) {
    // Test: Run with --silent --duration 0.1 --silent (inner silent)
    // Expect: CLI runs successfully (WAV export not supported, but audio pipeline works)
    int result = runCLI("--sine --silent --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --silent. Exit code: " << exitCode;
}

TEST_F(SilentFlagTest, SilentModeWithDefaultEngine) {
    // Test: Run --default-engine with --silent flag
    // Expect: No crash
    int result = runCLI("--default-engine --silent --duration 0.1 > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --default-engine --silent. Exit code: " << exitCode;
}

TEST_F(SilentFlagTest, SilentModeWithThreaded) {
    // Test: Run with --silent --threaded to test silent mode with threaded audio
    // Expect: No crash
    int result = runCLI("--sine --silent --threaded --duration 0.1 > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --silent --threaded. Exit code: " << exitCode;
}

TEST_F(SilentFlagTest, SilentModeWithRPM) {
    // Test: Run with --silent --rpm to test silent mode with RPM control
    // Expect: No crash
    int result = runCLI("--sine --silent --rpm 1000 --duration 0.1 > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --silent --rpm. Exit code: " << exitCode;
}
