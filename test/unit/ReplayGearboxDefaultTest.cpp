// ReplayGearboxDefaultTest.cpp - Behavior tests for the gearbox default
// resolved in processArgs() (CLIconfig.cpp).
//
// OWNER RULING (2026-09-08): --auto is the DEFAULT in ALL modes (interactive,
// replay, live); --manual is the explicit opt-out. The earlier replay-scoped
// default (D2, 2026-09-03: replay auto-shift including interactive replay) is
// subsumed — the default is now mode-agnostic. Gear keys still win over the
// auto box once running.
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
// Non-replay runs keep the AUTO default (the default is mode-agnostic)
// ============================================================================

TEST(ReplayGearboxDefault, InteractiveWithoutReplay_StillDefaultsToAutomatic) {
    // Owner ruling 2026-09-08: --auto is the default in ALL modes, so a bare
    // --interactive run (no --auto/--manual) resolves to AUTO.
    auto args = parseArgv({"--interactive"});
    EXPECT_TRUE(args.gearbox.automatic)
        << "the auto default is mode-agnostic; a bare interactive run must be auto";
    EXPECT_FALSE(args.gearbox.manual)
        << "No flag was passed; manual must not be implied";
}

// ============================================================================
// F6 characterization net (consolidation wave B, 2026-09-07; default widened
// 2026-09-08). resolveReplayGearboxDefault (CLIconfig.cpp, anonymous
// namespace) moves CLI-side -> bridge. The companion suite above already
// pins: default->auto (interactive and plain replay), --manual opt-out
// (interactive), explicit --auto, and the non-replay interactive default.
// Missing branches pinned here: the non-interactive --manual opt-out, and
// the LIVE path (stdin CSV, telemetryPath empty) which is now auto-defaulted
// too (the predicate is mode-agnostic).
// ============================================================================

TEST(ReplayGearboxDefault, PlainReplay_ManualOptsOutWithoutInteractive) {
    // The --interactive variant is pinned above; the opt-out must not depend
    // on it — the branch is `manual` alone.
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--manual"});
    EXPECT_TRUE(args.gearbox.manual);
    EXPECT_FALSE(args.gearbox.automatic)
        << "--manual is the explicit opt-out with or without --interactive";
}

TEST(ReplayGearboxDefault, LiveTelemetry_IsAutoDefaulted) {
    // Owner ruling 2026-09-08: --auto is the default in ALL modes, including
    // --live-telemetry. The live path drives the twin, which auto-shifts, so
    // the mode-agnostic default applies here too.
    auto args = parseArgv({"--live-telemetry"});
    EXPECT_TRUE(args.twin.liveTelemetry);
    EXPECT_TRUE(args.replay.telemetryPath.empty());
    EXPECT_TRUE(args.gearbox.automatic)
        << "the auto default now covers the live-telemetry path";
    EXPECT_FALSE(args.gearbox.manual);
}
