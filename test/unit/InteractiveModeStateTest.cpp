// InteractiveModeStateTest.cpp - Behavior tests for the observable effects of
// interactive mode during argument parsing.
//
// COVERAGE FOR cpp:S5421: the g_interactiveMode global was removed (per the
// codebase's "no globals for thread signalling" rule — it was written from
// args.interactive but read by zero production code, i.e. dead signalling
// state). These tests pin the sole observable contract — the parsed
// args.interactive flag — which is what all production code consumes.
//
// Asserting on the public contract (args.interactive) instead of internal
// global state makes the tests survive the global's removal: this IS the
// decoupling, not a weakening.
//
// WHAT WE ASSERT (intent, one test per behavior, no duplication):
//   - Interactive mode is the DEFAULT when no --duration is given.
//   - --interactive sets interactive mode.
//   - --connect-demo forces interactive mode (implicit).
//   - A positive --duration takes the CLI out of interactive mode.
//   - --connect-demo's implicit interactive=true overrides an explicit --duration.

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

#include <string>
#include <vector>

namespace {

// Parse the given args. Returns the parsed CommandLineArgs.
// No global state to reset now that g_interactiveMode is gone — each test is
// fully isolated by construction (args is a local).
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
// Default behavior: no --duration -> interactive
// ============================================================================

TEST(InteractiveModeState, DefaultNoDuration_IsInteractive) {
    auto args = parseArgv({"--silent"});
    EXPECT_TRUE(args.interactive)
        << "Without --duration the CLI defaults to interactive mode";
}

// ============================================================================
// --interactive flag sets interactive mode
// ============================================================================

TEST(InteractiveModeState, InteractiveFlag_SetsInteractive) {
    auto args = parseArgv({"--interactive"});
    EXPECT_TRUE(args.interactive);
}

// ============================================================================
// A positive --duration takes the CLI OUT of interactive mode
// ============================================================================

TEST(InteractiveModeState, PositiveDuration_IsNotInteractive) {
    auto args = parseArgv({"--duration", "2.5"});
    EXPECT_FALSE(args.interactive)
        << "A positive --duration means a bounded (non-interactive) run";
}

// ============================================================================
// --connect-demo forces interactive mode implicitly
// ============================================================================

TEST(InteractiveModeState, ConnectDemo_ForcesInteractive) {
    auto args = parseArgv({"--connect-demo"});
    EXPECT_TRUE(args.interactive)
        << "--connect-demo implies interactive mode";
}

// ============================================================================
// --connect-demo's implicit interactive=true overrides an explicit --duration
// ============================================================================

TEST(InteractiveModeState, ConnectDemo_OverridesDuration) {
    // connect-demo's implicit interactive=true must win even when a duration is
    // present. This is the intent the refactor must preserve.
    auto args = parseArgv({"--connect-demo", "--duration", "5"});
    EXPECT_TRUE(args.interactive);
}

// ============================================================================
// --replay-telemetry without --duration is a BOUNDED run, not interactive
// ============================================================================
//
// Regression: the trace bounds a replay, so no --duration must NOT mean "run
// until the user quits". Interactive here suppressed the trace-length default
// in applyReplayTraceDuration (which requires !interactive), so the run span
// forever on the trace's final row instead of exiting at the end of the trace.
// The path is never opened during parsing, so a nonexistent name is fine.

TEST(InteractiveModeState, ReplayTelemetryNoDuration_IsNotInteractive) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv"});
    EXPECT_FALSE(args.interactive)
        << "A replay trace bounds its own run: without --duration it must stay "
           "non-interactive so the trace length can bound it";
}

// An explicit --interactive is still honoured for a replay: the user asking to
// drive the session by hand outranks the bounded-batch default.
TEST(InteractiveModeState, ReplayTelemetryWithInteractiveFlag_StaysInteractive) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--interactive"});
    EXPECT_TRUE(args.interactive)
        << "An explicit --interactive must override the replay batch default";
}

// An explicit --duration on a replay is still a bounded, non-interactive run —
// the new replay branch must not disturb the pre-existing duration behaviour.
TEST(InteractiveModeState, ReplayTelemetryWithDuration_IsNotInteractive) {
    auto args = parseArgv({"--replay-telemetry", "capture.csv", "--duration", "2"});
    EXPECT_FALSE(args.interactive);
}
