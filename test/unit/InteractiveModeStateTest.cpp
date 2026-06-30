// InteractiveModeStateTest.cpp - Behavior tests for the observable effects of
// interactive mode during argument parsing.
//
// TARGETS REFACTOR COVERAGE FOR cpp:S5421 (g_interactiveMode global):
//   The Tier-3 refactor eliminates the g_interactiveMode global (per the
//   codebase's "no globals for thread signalling" rule). These tests pin the
//   OBSERVABLE behavior that depends on it — namely, that interactive mode is
//   correctly detected and signalled — so the refactor can preserve that
//   behavior however it replaces the global.
//
// WHAT WE ASSERT (intent, not mechanism):
//   - Interactive mode is the DEFAULT when no --duration is given.
//   - --interactive sets interactive mode.
//   - --connect-demo forces interactive mode (implicit).
//   - A positive --duration takes the CLI out of interactive mode.
//   Each scenario is checked both on the parsed args.interactive flag (the
//   public contract) AND on g_interactiveMode (the current signalling channel),
//   so a refactor that removes the global but keeps args.interactive correct
//   will still pass the args-level assertions; the global assertions document
//   the current coupling and will be updated when the global is removed.
//
// NOTE: g_interactiveMode is process-global mutable state. Each test resets it
// before parsing to avoid inter-test leakage (each test honors SRP and must not
// depend on another test's outcome).

#include <gtest/gtest.h>
#include "config/CLIconfig.h"

#include <string>
#include <vector>

namespace {

// Reset the global to a known baseline, then parse the given args.
// Returns the parsed CommandLineArgs.
CommandLineArgs parseArgvFresh(std::vector<std::string> args) {
    g_interactiveMode.store(false);  // isolate from prior tests
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

TEST(InteractiveModeState, DefaultNoDuration_SignalsInteractiveGlobal) {
    auto args = parseArgvFresh({"--silent"});
    EXPECT_TRUE(g_interactiveMode.load());
}

// ============================================================================
// --interactive flag
// ============================================================================

TEST(InteractiveModeState, InteractiveFlag_SetsInteractive) {
    auto args = parseArgvFresh({"--interactive"});
    EXPECT_TRUE(args.interactive);
    EXPECT_TRUE(g_interactiveMode.load());
}

// ============================================================================
// --duration takes the CLI OUT of interactive mode
// ============================================================================

TEST(InteractiveModeState, PositiveDuration_IsNotInteractive) {
    auto args = parseArgvFresh({"--duration", "2.5"});
    EXPECT_FALSE(args.interactive)
        << "A positive --duration means a bounded (non-interactive) run";
}

TEST(InteractiveModeState, PositiveDuration_DoesNotSignalInteractiveGlobal) {
    auto args = parseArgvFresh({"--duration", "2.5"});
    EXPECT_FALSE(g_interactiveMode.load());
}

// ============================================================================
// --connect-demo forces interactive mode implicitly
// ============================================================================

TEST(InteractiveModeState, ConnectDemo_ForcesInteractive) {
    auto args = parseArgvFresh({"--connect-demo"});
    EXPECT_TRUE(args.interactive)
        << "--connect-demo implies interactive mode";
}

TEST(InteractiveModeState, ConnectDemo_SignalsInteractiveGlobal) {
    auto args = parseArgvFresh({"--connect-demo"});
    EXPECT_TRUE(g_interactiveMode.load());
}

TEST(InteractiveModeState, ConnectDemo_OverridesDuration) {
    // connect-demo's implicit interactive=true must win even if a duration is
    // present. This is the intent the refactor must preserve.
    auto args = parseArgvFresh({"--connect-demo", "--duration", "5"});
    EXPECT_TRUE(args.interactive);
    EXPECT_TRUE(g_interactiveMode.load());
}

// ============================================================================
// Consistency invariant: args.interactive and the global agree
//   (documents the current coupling the refactor must replace coherently)
// ============================================================================

TEST(InteractiveModeState, GlobalAgreesWithArgsFlag_Interactive) {
    auto args = parseArgvFresh({"--interactive"});
    EXPECT_EQ(args.interactive, g_interactiveMode.load());
}

TEST(InteractiveModeState, GlobalAgreesWithArgsFlag_NonInteractive) {
    auto args = parseArgvFresh({"--duration", "1.0"});
    EXPECT_EQ(args.interactive, g_interactiveMode.load());
}
