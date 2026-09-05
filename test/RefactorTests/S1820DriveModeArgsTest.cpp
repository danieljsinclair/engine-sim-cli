// S1820DriveModeArgsTest.cpp
//
// BDD-style tests pinning the contract of the DriveModeArgs extraction
// (SonarQube S1820 partial).
//
// CONTEXT:
//   CommandLineArgs has 21 fields. We extract the 2 "drive mode" knobs
//   into a dedicated sub-struct:
//
//     bool connectDemo   // --connect-demo
//     bool sineMode      // --sine
//
//   These two flags describe the SIMULATOR DRIVE MODE (demo twin vs sine
//   tone vs piston engine) and have similar downstream effects (both can
//   force interactive, both imply a particular audio source). They are
//   grouped here so the parent struct drops to ~5 fields from the input
//   mode extraction alone.
//
//   The sub-struct lives as a nested member type (`struct DriveModeArgs`)
//   on CommandLineArgs, mirroring the TwinArgs / StartArgs precedent.
//   Accessed via args.driveMode.X after the extraction.
//
// CONTRACT under test:
//   1. Default values match the original struct's defaults.
//   2. Each flag --X sets the corresponding nested field.
//   3. The drive-mode members on InputModeArgs (where connectDemo and
//      sineMode are also used for the keyboard and audio-mode decision
//      tree) are kept in lockstep with args.driveMode.* — production
//      code must read both consistently.
//
// Per the extraction plan, the 2 fields live in BOTH `args.inputMode.*`
// (where they're read for the audio-mode/interactive decisions) AND
// `args.driveMode.*` (where they describe the simulator drive mode). The
// chosen approach is to MOVE the fields entirely — production code reads
// `args.driveMode.connectDemo` and `args.driveMode.sineMode`, and the
// nested inputMode accessor is removed for these two. These tests pin
// the canonical access path on the driveMode struct.

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

#include <string>
#include <vector>

namespace {

CommandLineArgs parseArgv(std::vector<std::string> flags) {
    CommandLineArgs parsed;
    flags.insert(flags.begin(), "engine-sim-cli");
    std::vector<char*> argv;
    argv.reserve(flags.size());
    for (auto& s : flags) argv.push_back(s.data());
    parseArguments(static_cast<int>(argv.size()), argv.data(), parsed);
    return parsed;
}

}  // namespace

// ============================================================================
// Default values
// ============================================================================

TEST(DriveModeArgsDefaults, NoFlags_ConnectDemoFalse) {
    auto args = parseArgv({"--silent"});
    EXPECT_FALSE(args.driveMode.connectDemo)
        << "--connect-demo is opt-in, never defaulted on";
}

TEST(DriveModeArgsDefaults, NoFlags_SineModeFalse) {
    auto args = parseArgv({"--silent"});
    EXPECT_FALSE(args.driveMode.sineMode)
        << "--sine is opt-in, never defaulted on";
}

// ============================================================================
// Flag round-trip
// ============================================================================

TEST(DriveModeArgsFlag, ConnectDemo_SetsDriveModeConnectDemo) {
    auto args = parseArgv({"--connect-demo"});
    EXPECT_TRUE(args.driveMode.connectDemo)
        << "--connect-demo must set driveMode.connectDemo=true";
}

TEST(DriveModeArgsFlag, Sine_SetsDriveModeSineMode) {
    auto args = parseArgv({"--sine"});
    EXPECT_TRUE(args.driveMode.sineMode)
        << "--sine must set driveMode.sineMode=true";
}

// connect-demo's implicit overrides stay observable via the
// inputMode struct (where the audio-mode decision lives).
TEST(DriveModeArgsFlag, ConnectDemo_DoesNotAutoSetSine) {
    // The two drive-mode flags are independent: --connect-demo does not
    // imply --sine (they are mutually-exclusive simulator sources).
    auto args = parseArgv({"--connect-demo"});
    EXPECT_TRUE(args.driveMode.connectDemo);
    EXPECT_FALSE(args.driveMode.sineMode);
}

TEST(DriveModeArgsFlag, Sine_DoesNotAutoSetConnectDemo) {
    auto args = parseArgv({"--sine"});
    EXPECT_TRUE(args.driveMode.sineMode);
    EXPECT_FALSE(args.driveMode.connectDemo);
}

// ============================================================================
// API pin
// ============================================================================

TEST(DriveModeArgsContract, FieldIsNamedDriveMode) {
    auto args = parseArgv({"--silent"});
    // If the extraction renamed the field, these expressions would fail
    // to compile, which is exactly what we want.
    static_cast<void>(args.driveMode.connectDemo);
    static_cast<void>(args.driveMode.sineMode);
    SUCCEED() << "Both fields exist on args.driveMode; the field is named driveMode";
}
