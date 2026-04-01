// test_path_resolution.cpp - Path resolution scenario tests
//
// Purpose: Verify that path resolution works correctly for various scenarios.
// This tests the critical path resolution logic in both the CLI and bridge.
//
// TDD Phase: RED - Skeleton tests that compile (implementation to follow)

#include <gtest/gtest.h>
#include <cstdlib>
#include <unistd.h>
#include <cstdio>
#include <string>
#include <filesystem>
#include "SmokeTestHelper.h"

class PathResolutionSmokeTest : public ::testing::Test {
protected:
    // Helper to run CLI from project root directory
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }

    // Helper to get project root
    std::string getProjectRoot() const {
        return SmokeTestHelper::getProjectRoot();
    }

    void SetUp() override {
        // Save current working directory
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            originalCwd = std::string(cwd);
        }
    }

    void TearDown() override {
        // Restore original working directory
        if (!originalCwd.empty()) {
            chdir(originalCwd.c_str());
        }
    }

private:
    std::string originalCwd;
};

// ============================================================================
// Test Scenario 1: Default engine path resolution (hardcoded path works)
// ============================================================================

TEST_F(PathResolutionSmokeTest, DefaultEnginePathResolves) {
    // Test: Run with --default-engine flag
    // Expect: CLI successfully loads the hardcoded default engine path
    // Implementation note: Tests that the hardcoded path "engine-sim-bridge/engine-sim/assets/main.mr"
    // resolves correctly from project root

    int result = runCLI("--default-engine --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;

    EXPECT_EQ(exitCode, 0) << "Default engine path resolution failed. "
                            << "The hardcoded path 'engine-sim-bridge/engine-sim/assets/main.mr' "
                            << "could not be resolved from project root.";
}

// ============================================================================
// Test Scenario 2: Relative script path resolution (e.g., ./engines/v8.mr)
// ============================================================================

TEST_F(PathResolutionSmokeTest, RelativeScriptPathResolves) {
    // Test: Run with relative path to script
    // Expect: CLI resolves relative path correctly and loads the script
    // Implementation note: Tests relative path resolution using ./ prefix

    std::string projectRoot = getProjectRoot();
    std::string defaultEnginePath = "engine-sim-bridge/engine-sim/assets/main.mr";

    // Verify the file exists first
    std::filesystem::path fullPath = std::filesystem::path(projectRoot) / defaultEnginePath;
    if (!std::filesystem::exists(fullPath)) {
        GTEST_SKIP() << "Default engine file not found at: " << fullPath.string();
    }

    int result = runCLI("--script " + defaultEnginePath + " --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;

    EXPECT_EQ(exitCode, 0) << "Relative script path resolution failed for: " << defaultEnginePath;
}

// ============================================================================
// Test Scenario 3: Absolute script path resolution
// ============================================================================

TEST_F(PathResolutionSmokeTest, AbsoluteScriptPathResolves) {
    // Test: Run with absolute path to script
    // Expect: CLI handles absolute path correctly and loads the script
    // Implementation note: Tests absolute path resolution

    std::string projectRoot = getProjectRoot();
    std::string defaultEnginePath = "engine-sim-bridge/engine-sim/assets/main.mr";
    std::filesystem::path fullPath = std::filesystem::path(projectRoot) / defaultEnginePath;

    // Verify the file exists
    if (!std::filesystem::exists(fullPath)) {
        GTEST_SKIP() << "Default engine file not found at: " << fullPath.string();
    }

    std::string absolutePath = fullPath.string();
    int result = runCLI("--script \"" + absolutePath + "\" --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;

    EXPECT_EQ(exitCode, 0) << "Absolute script path resolution failed for: " << absolutePath;
}

// ============================================================================
// Test Scenario 4: Asset base path derivation from script path
// ============================================================================

TEST_F(PathResolutionSmokeTest, AssetBasePathDerivedCorrectly) {
    // Test: Load a script and verify asset base path is derived correctly
    // Expect: CLI derives asset base path from script location
    // Implementation note: Tests that assetBasePath is correctly derived
    // when script contains /assets/ in its path

    // The default engine script is at: engine-sim-bridge/engine-sim/assets/main.mr
    // Asset base path should be: engine-sim-bridge/engine-sim

    std::string projectRoot = getProjectRoot();
    std::string defaultEnginePath = "engine-sim-bridge/engine-sim/assets/main.mr";
    std::filesystem::path fullPath = std::filesystem::path(projectRoot) / defaultEnginePath;

    if (!std::filesystem::exists(fullPath)) {
        GTEST_SKIP() << "Default engine file not found at: " << fullPath.string();
    }

    // Run the CLI - if asset base path is wrong, impulse responses won't load
    int result = runCLI("--script " + defaultEnginePath + " --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;

    EXPECT_EQ(exitCode, 0) << "Asset base path derivation failed. "
                            << "Impulse responses could not be loaded. "
                            << "Script: " << defaultEnginePath;
}

// ============================================================================
// Test Scenario 5: File existence validation error handling
// ============================================================================

TEST_F(PathResolutionSmokeTest, MissingFileReturnsError) {
    // Test: Attempt to load a non-existent script file
    // Expect: CLI returns non-zero exit code with appropriate error message
    // Implementation note: Tests file existence validation in prepareScriptConfig()

    std::string nonexistentPath = "path/to/nonexistent/file.mr";
    int result = runCLI("--script " + nonexistentPath + " --duration 0.1 --silent 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;

    EXPECT_NE(exitCode, 0) << "Missing file should return non-zero exit code";
}

// ============================================================================
// Test Scenario 6: Special path formats (~/, ../, etc.)
// ============================================================================

TEST_F(PathResolutionSmokeTest, CurrentDirectoryPathResolves) {
    // Test: Run with ./ prefix in path
    // Expect: CLI resolves current directory reference correctly
    // Implementation note: Tests that ./ is handled properly

    std::string projectRoot = getProjectRoot();
    std::string defaultEnginePath = "engine-sim-bridge/engine-sim/assets/main.mr";
    std::filesystem::path fullPath = std::filesystem::path(projectRoot) / defaultEnginePath;

    if (!std::filesystem::exists(fullPath)) {
        GTEST_SKIP() << "Default engine file not found at: " << fullPath.string();
    }

    // Change to project root and use ./
    chdir(projectRoot.c_str());

    std::string relativeWithCurrent = "./" + defaultEnginePath;
    int result = runCLI("--script " + relativeWithCurrent + " --duration 0.1 --silent > /dev/null 2>&1");
    int exitCode = WIFEXITED(result) ? WEXITSTATUS(result) : -1;

    EXPECT_EQ(exitCode, 0) << "Current directory path resolution failed for: " << relativeWithCurrent;
}

// NOTE: ParentDirectoryPathResolves test removed
// Reason: SmokeTestHelper.runCLI() always changes to projectRoot before running CLI,
// which negates any chdir() done in the test. Testing parent directory paths
// would require bypassing the test helper or modifying it, which tests test
// infrastructure rather than CLI behavior. Manual testing confirms ../ paths work.
