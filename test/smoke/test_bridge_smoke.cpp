// test_bridge_smoke.cpp - Bridge smoke tests
//
// Purpose: Verify the engine-sim-bridge library loads and basic functions work.

#include <gtest/gtest.h>
#include <cstdlib>
#include "SmokeTestHelper.h"

class BridgeSmokeTest : public ::testing::Test {
protected:
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }
};

TEST_F(BridgeSmokeTest, LoadsWithoutCrash) {
    // Test: Run CLI with default engine, short duration
    // Expect: Exit code 0, no crash
    int result = runCLI("--default-engine --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with exit code " << exitCode;
}

TEST_F(BridgeSmokeTest, BridgeSineModeWorks) {
    // Test: Run CLI with sine mode
    // Expect: Exit code 0
    int result = runCLI("--sine --duration 0.1 --silent > /dev/null 2>&1");

    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;
    EXPECT_EQ(exitCode, 0) << "CLI failed with --sine. Exit code: " << exitCode;
}