// InteractiveModeStateTest.cpp - Behavior tests for the observable effects of
// interactive mode during argument parsing.
//
// COVERAGE FOR cpp:S5421: the g_interactiveMode global was removed (per the
// codebase's "no globals for thread signalling" rule). These tests now pin the
// sole observable contract — the parsed args.interactive flag — which is what
// all production code consumes.
//
// WHAT WE ASSERT (intent, not mechanism):
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
CommandLineArgs parseArgvFresh(std::vector<std::string> args) {
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
    auto args = parseArgvFresh({"--silent"});
    EXPECT_TRUE(args.interactive)
        << "Without --duration the CLI defaults to interactive mode";
}

// Same default-interactive scenario, asserted on the flag (the sole contract
// now that the redundant signalling global is gone).
TEST(InteractiveModeState, DefaultNoDuration_FlagIsTrue) {
    auto args = parseArgvFresh({"--silent"});
    EXPECT_TRUE(args.interactive);
}

// ============================================================================
// --interactive flag
// ============================================================================

TEST(InteractiveModeState, InteractiveFlag_SetsInteractive) {
    auto args = parseArgvFresh({"--interactive"});
    EXPECT_TRUE(args.interactive);
}

// ============================================================================
// --duration takes the CLI OUT of interactive mode
// ============================================================================

TEST(InteractiveModeState, PositiveDuration_IsNotInteractive) {
    auto args = parseArgvFresh({"--duration", "2.5"});
    EXPECT_FALSE(args.interactive)
        << "A positive --duration means a bounded (non-interactive) run";
}

TEST(InteractiveModeState, PositiveDuration_FlagIsFalse) {
    auto args = parseArgvFresh({"--duration", "2.5"});
    EXPECT_FALSE(args.interactive);
}

// ============================================================================
// --connect-demo forces interactive mode implicitly
// ============================================================================

TEST(InteractiveModeState, ConnectDemo_ForcesInteractive) {
    auto args = parseArgvFresh({"--connect-demo"});
    EXPECT_TRUE(args.interactive)
        << "--connect-demo implies interactive mode";
}

TEST(InteractiveModeState, ConnectDemo_FlagIsTrue) {
    auto args = parseArgvFresh({"--connect-demo"});
    EXPECT_TRUE(args.interactive);
}

TEST(InteractiveModeState, ConnectDemo_OverridesDuration) {
    // connect-demo's implicit interactive=true must win even if a duration is
    // present. This is the intent the refactor must preserve.
    auto args = parseArgvFresh({"--connect-demo", "--duration", "5"});
    EXPECT_TRUE(args.interactive);
}
