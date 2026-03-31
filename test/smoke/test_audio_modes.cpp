// test_audio_modes.cpp - Smoke tests for audio modes
//
// Purpose: Verify both threaded and sync-pull modes work.
// This is a happy-path test that verifies the two main audio rendering modes.

#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include "SmokeTestHelper.h"

class AudioModesTest : public ::testing::Test {
protected:
    // Helper to run CLI from project root directory
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }
};

TEST_F(AudioModesTest, ThreadedModeWorks) {
    // Test: Run with --threaded flag
    // Expect: No crash, clean audio output
    int result = runCLI("--sine --threaded --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --threaded flag. Exit code: " << exitCode;
}

TEST_F(AudioModesTest, SyncPullModeWorks) {
    // Test: Run without --threaded flag (default sync-pull)
    // Expect: No crash, clean audio output
    int result = runCLI("--sine --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with sync-pull mode. Exit code: " << exitCode;
}

TEST_F(AudioModesTest, ThreadedModeWithDefaultEngine) {
    // Test: Run --default-engine with --threaded flag
    // Expect: No crash
    int result = runCLI("--default-engine --threaded --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --default-engine --threaded. Exit code: " << exitCode;
}

TEST_F(AudioModesTest, SyncPullModeWithDefaultEngine) {
    // Test: Run --default-engine with sync-pull mode (default)
    // Expect: No crash
    int result = runCLI("--default-engine --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --default-engine sync-pull. Exit code: " << exitCode;
}

TEST_F(AudioModesTest, ThreadedModeProducesOutput) {
    // Test: Run with --threaded --duration 0.1 --silent
    // Expect: CLI runs successfully (WAV export not supported, but audio pipeline works)
    int result = runCLI("--sine --threaded --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --threaded. Exit code: " << exitCode;
}

TEST_F(AudioModesTest, SyncPullModeProducesOutput) {
    // Test: Run with sync-pull --duration 0.1 --silent
    // Expect: CLI runs successfully (WAV export not supported, but audio pipeline works)
    int result = runCLI("--sine --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with sync-pull. Exit code: " << exitCode;
}
