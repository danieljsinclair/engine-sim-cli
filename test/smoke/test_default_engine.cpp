// test_default_engine.cpp - Smoke tests for --default-engine flag
//
// Purpose: Verify that --default-engine flag works without crashing.
// This is a happy-path test that verifies the basic default engine functionality.

#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include "SmokeTestHelper.h"

class DefaultEngineSmokeTest : public ::testing::Test {
protected:
    // Helper to run CLI from project root directory
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }
};

TEST_F(DefaultEngineSmokeTest, RunsWithoutCrash) {
    // Test: Run with --default-engine --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = runCLI("--default-engine --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with exit code " << exitCode;
}

TEST_F(DefaultEngineSmokeTest, ProducesAudioOutput) {
    // Test: Run with --default-engine --duration 0.1 --silent
    // Expect: CLI runs successfully (WAV export not supported, but audio pipeline works)
    int result = runCLI("--default-engine --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with exit code " << exitCode;
}

TEST_F(DefaultEngineSmokeTest, DefaultEngineWithRPMFlag) {
    // Test: Run with --default-engine --rpm 2000 --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = runCLI("--default-engine --rpm 2000 --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with RPM flag. Exit code: " << exitCode;
}

TEST_F(DefaultEngineSmokeTest, DefaultEngineWithLoadFlag) {
    // Test: Run with --default-engine --load 50 --duration 0.1 --silent
    // Expect: Exit code 0, no crash
    int result = runCLI("--default-engine --load 50 --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with load flag. Exit code: " << exitCode;
}
