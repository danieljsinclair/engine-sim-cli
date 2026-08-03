// ExecutablePathTest.cpp - Verify resource resolution is independent of CWD.
//
// The CLI resolves presets relative to the running executable, not PWD, so it
// must find them when launched from an unrelated directory (e.g. /tmp).

#include <gtest/gtest.h>
#include <unistd.h>
#include <cstdio>
#include <sys/syslimits.h>
#include <filesystem>
#include <string>

#include "config/ExecutablePath.h"

namespace {

class ExecutablePathTest : public ::testing::Test {
protected:
    void SetUp() override {
        char cwd[PATH_MAX];
        ASSERT_NE(getcwd(cwd, sizeof(cwd)), nullptr);
        originalCwd_ = cwd;
    }

    void TearDown() override {
        if (!originalCwd_.empty()) {
            (void)chdir(originalCwd_.c_str());
        }
    }

    std::string originalCwd_;
};

// The executable's directory must be reported absolute (the test binary dir).
TEST_F(ExecutablePathTest, DirectoryIsAbsolute) {
    const std::string dir = cli::ExecutablePath::directory();
    ASSERT_FALSE(dir.empty());
    EXPECT_EQ(dir.front(), '/');
    EXPECT_TRUE(std::filesystem::exists(dir));
}

// Core requirement: presets are discovered even when CWD is /tmp. Resolution
// walks up from the executable dir to the project root, independent of PWD.
TEST_F(ExecutablePathTest, PresetDirResolvesFromForeignCwd) {
    ASSERT_EQ(chdir("/tmp"), 0) << "could not chdir to /tmp";

    const std::string resolved =
        cli::ExecutablePath::resolveResource("engine-sim-bridge/preset/");

    ASSERT_FALSE(resolved.empty());
    EXPECT_TRUE(std::filesystem::exists(resolved))
        << "Preset dir not found via exe-relative resolution from /tmp: " << resolved;
    EXPECT_TRUE(std::filesystem::is_directory(resolved));
}

// When the resource genuinely does not exist, resolution still returns a
// non-empty path (so callers fail with a clear "not found" rather than "").
TEST_F(ExecutablePathTest, MissingResourceReturnsNonEmptyPath) {
    const std::string resolved =
        cli::ExecutablePath::resolveResource("engine-sim-bridge/does_not_exist/");
    EXPECT_FALSE(resolved.empty());
}

} // namespace
