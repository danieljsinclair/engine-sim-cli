// ReplayGearboxDefaultTest.cpp - Behavior tests for the replay-mode gearbox
// default resolved in processArgs() (CLIconfig.cpp).
//
// OWNER RULING (D2, 2026-09-03): the auto-shift default for --replay-telemetry
// INCLUDES --interactive runs. The owner expected "auto is the default for
// telemetry driving sims" to hold when replaying a capture interactively;
// gear keys still win over the auto box once running. --manual remains the
// explicit opt-out.
//
// DESIGN:
//   - resolveReplayGearboxDefault lives in an anonymous namespace in
//     CLIconfig.cpp, so these tests drive the OBSERVABLE CommandLineArgs
//     produced by parseArguments() — the same seam GearboxLogFilenameTest
//     uses. No production symbol is exposed for the test.
//   - We assert the resolved gearbox MODE (automatic/manual flags), not any
//     help text or implementation detail.
//   - The path passed to --replay-telemetry is never opened during parsing;
//     a sentinel name is fine.

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

#include <string>
#include <vector>

namespace {

// Build an argv vector the way CLI11 expects (argv[0] is the program name),
// then parse it. Returns the parsed CommandLineArgs.
CommandLineArgs parseArgv(std::vector<std::string> args) {
    CommandLineArgs parsed;
    args.insert(args.begin(), "engine-sim-cli");
    std::vector<char*> argv;
    argv.reserve(args.size());
    for (auto& s : args) argv.push_back(s.data());
    parseArguments(static_cast<int>(argv.size()), argv.data(), parsed);
    return parsed;
}

}  // namespace

// ============================================================================
// Replay + --interactive -> automatic gearbox by default (the D2 fix)
// ============================================================================

TEST(ReplayGearboxDefault, InteractiveReplay_DefaultsToAutomatic) {
    // The owner's interactive replay invocation: replay + --interactive, no
    // explicit --auto/--manual. Must resolve to the automatic gearbox.
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--interactive"});
    EXPECT_TRUE(args.gearbox.automatic)
        << "--interactive must not opt a replay run out of the auto-shift default";
}

TEST(ReplayGearboxDefault, InteractiveReplay_DoesNotSetManual) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--interactive"});
    EXPECT_FALSE(args.gearbox.manual)
        << "The interactive default must be AUTO, not manual";
}

// ============================================================================
// --manual still opts out (including with --interactive)
// ============================================================================

TEST(ReplayGearboxDefault, InteractiveReplay_ManualOptsOut) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--interactive", "--manual"});
    EXPECT_TRUE(args.gearbox.manual);
    EXPECT_FALSE(args.gearbox.automatic)
        << "--manual is the explicit opt-out; the default must not override it";
}

// ============================================================================
// The pre-existing default stays pinned (no regression from the D2 change)
// ============================================================================

TEST(ReplayGearboxDefault, PlainReplay_StillDefaultsToAutomatic) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv"});
    EXPECT_TRUE(args.gearbox.automatic)
        << "Non-interactive replay keeps its existing auto default";
}

TEST(ReplayGearboxDefault, ExplicitAuto_IsRedundantButHarmless) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--interactive", "--auto"});
    EXPECT_TRUE(args.gearbox.automatic);
    EXPECT_FALSE(args.gearbox.manual);
}

// ============================================================================
// Non-replay runs keep the manual default (the D2 change is replay-scoped)
// ============================================================================

TEST(ReplayGearboxDefault, InteractiveWithoutReplay_KeepsManualDefault) {
    auto args = parseArgv({"--interactive"});
    EXPECT_FALSE(args.gearbox.automatic)
        << "The auto default is tied to replay; a bare interactive run must stay manual";
    EXPECT_FALSE(args.gearbox.manual)
        << "No flag was passed; neither mode should be explicitly set";
}

// ============================================================================
// F6 characterization net (consolidation wave B, 2026-09-07).
// resolveReplayGearboxDefault (CLIconfig.cpp, anonymous namespace) moves
// CLI-side -> bridge. The D2 companion suite above already pins: replay
// default->auto (interactive and plain), --manual opt-out (interactive),
// explicit --auto, and the replay-scoped non-replay default. Missing branches
// pinned here: the non-interactive --manual opt-out, and the LIVE path
// (stdin CSV, telemetryPath empty) which the resolution must NOT touch.
// ============================================================================

TEST(ReplayGearboxDefault, PlainReplay_ManualOptsOutWithoutInteractive) {
    // The --interactive variant is pinned above; the opt-out must not depend
    // on it — the branch is `manual` alone.
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--manual"});
    EXPECT_TRUE(args.gearbox.manual);
    EXPECT_FALSE(args.gearbox.automatic)
        << "--manual is the explicit opt-out with or without --interactive";
}

TEST(ReplayGearboxDefault, LiveTelemetry_IsNotAutoDefaulted) {
    // --live-telemetry reads stdin, so replay.telemetryPath stays empty: the
    // auto default is REPLAY-scoped (LiveTelemetryProvider has no manual
    // gearbox mode to flip). The move must keep the replay-path predicate.
    auto args = parseArgv({"--live-telemetry"});
    EXPECT_TRUE(args.twin.liveTelemetry);
    EXPECT_TRUE(args.replay.telemetryPath.empty());
    EXPECT_FALSE(args.gearbox.automatic)
        << "the auto default must not leak onto the live-telemetry path";
    EXPECT_FALSE(args.gearbox.manual);
}
