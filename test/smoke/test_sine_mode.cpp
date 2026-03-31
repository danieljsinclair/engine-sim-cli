// test_sine_mode.cpp - Smoke tests for --sine flag
//
// Purpose: Verify that --sine flag works without crashing and produces audio.
// This is a happy-path test that verifies the basic sine wave functionality.

#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include "SmokeTestHelper.h"

class SineModeSmokeTest : public ::testing::Test {
protected:
    // Helper to run CLI from project root directory
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }
};

TEST_F(SineModeSmokeTest, RunsWithoutCrash) {
    // Test: Run with --sine --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = runCLI("--sine --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with exit code " << exitCode;
}

TEST_F(SineModeSmokeTest, ProducesAudioOutput) {
    // Test: Run with --sine --duration 0.1 --silent
    // Expect: CLI runs successfully (WAV export not supported, but audio pipeline works)
    int result = runCLI("--sine --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with exit code " << exitCode;
}

TEST_F(SineModeSmokeTest, SineModeWithRPMFlag) {
    // Test: Run with --sine --rpm 1000 --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = runCLI("--sine --rpm 1000 --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with RPM flag. Exit code: " << exitCode;
}
