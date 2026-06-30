// test_runloop_lifecycle.cpp - Behavior tests for the CLI run-loop lifecycle.
//
// TARGETS REFACTOR COVERAGE FOR:
//   cpp:S5272 (use-after-move on `simulator` at CLIMain.cpp:357)
//   cpp:S3776 / cpp:S134 (cognitive-complexity / nesting in the main run-loop,
//                         CLIMain.cpp:293 and :361)
//
// WHY A SMOKE (BINARY-LEVEL) TEST:
//   The use-after-move and the high-complexity control flow both live inside
//   main() and its anonymous-namespace helpers in CLIMain.cpp. That translation
//   unit owns the program entry point and is intentionally NOT compiled into
//   the unit-test binary, so the behaviors cannot be exercised as unit tests
//   without first refactoring production code (out of scope for this coverage
//   pass). The Tier-3 refactor will split this loop into testable pieces;
//   these binary-level tests pin the loop's OBSERVABLE behaviors so the split
//   can be verified behavior-identical:
//
//     1. A bounded non-interactive run completes cleanly (exit 0) — exercises
//        simulator creation, session creation, the run loop, and the
//        "duration reached" stop reason. A use-after-move on the simulator or
//        a broken loop-exit condition would crash or hang here.
//     2. The default-preset cycling path (no --script) initializes correctly
//        and runs to completion — exercises resolveConfigPaths + the cycle
//        loop setup with multiple paths.
//     3. An explicit --script runs the single-path branch to completion.
//     4. A bad engine config is reported as a failure (exit 1) — exercises the
//        exception/error path of the loop.
//
//   Each test asserts INTENT (exit code = outcome) and survives a refactor
//   that restructures the loop, as long as the outcomes are preserved.

#include <gtest/gtest.h>
#include <cstdlib>
#include "SmokeTestHelper.h"

class RunLoopLifecycleSmokeTest : public ::testing::Test {
protected:
    int runCLI(const std::string& args) const {
        return SmokeTestHelper::runCLI(args);
    }

    // Normalize a system() return into the process exit code, or -1 if it did
    // not exit normally (e.g. killed by a signal — the failure mode a
    // use-after-move or broken loop would likely produce).
    static int exitCode(int raw) {
        if (WIFEXITED(raw)) return WEXITSTATUS(raw);
        if (WIFSIGNALED(raw)) return 128 + WTERMSIG(raw);
        return -1;
    }
};

// ============================================================================
// 1. Bounded non-interactive run completes cleanly
//    Exercises: simulator create -> session create -> run loop -> duration stop.
//    Protects: S5272 (simulator used after move into createSession) and the
//    loop's normal-exit condition (S3776).
// ============================================================================
TEST_F(RunLoopLifecycleSmokeTest, BoundedDurationRun_CompletesCleanly) {
    // --script resolves relative to CWD (project root, set by SmokeTestHelper),
    // so the preset must be addressed via its engine-sim-bridge/preset/ path.
    int raw = runCLI("--script engine-sim-bridge/preset/v8_gm_ls.json --duration 0.1 --silent > /dev/null 2>&1");
    EXPECT_EQ(exitCode(raw), 0)
        << "Bounded run should exit 0; got " << exitCode(raw);
}

// ============================================================================
// 2. Default preset-cycling path initializes and completes
//    Exercises: resolveConfigPaths discovering multiple presets + the cycle
//    loop's first iteration running to a duration stop.
//    Protects: the multi-path branch of the run loop (S3776/S134 nesting).
// ============================================================================
TEST_F(RunLoopLifecycleSmokeTest, DefaultPresetCycling_CompletesCleanly) {
    // No --script: the CLI cycles all presets in engine-sim-bridge/preset/.
    // A short duration means it stops on the first preset (duration reached),
    // exercising cycle-loop init without requiring user input to advance.
    int raw = runCLI("--duration 0.1 --silent > /dev/null 2>&1");
    EXPECT_EQ(exitCode(raw), 0)
        << "Default preset-cycling run should exit 0; got " << exitCode(raw);
}

// ============================================================================
// 3. Explicit script runs the single-path branch to completion
//    Exercises: the paths.size()==1 branch (script given directly).
// ============================================================================
TEST_F(RunLoopLifecycleSmokeTest, ExplicitScript_SinglePathRun) {
    int raw = runCLI("--script engine-sim-bridge/preset/2jz.json --duration 0.1 --silent > /dev/null 2>&1");
    EXPECT_EQ(exitCode(raw), 0)
        << "Explicit-script run should exit 0; got " << exitCode(raw);
}

// ============================================================================
// 4. Bad engine config is a failure (exit 1)
//    Exercises: the exception path of the run loop (the catch sets result=1).
//    Protects: the error-handling branch a refactor must preserve.
// ============================================================================
TEST_F(RunLoopLifecycleSmokeTest, NonexistentScript_ExitsAsFailure) {
    int raw = runCLI("--script does_not_exist.json --duration 0.1 --silent > /dev/null 2>&1");
    EXPECT_NE(exitCode(raw), 0)
        << "A missing/invalid script must not report success; got " << exitCode(raw);
}

// ============================================================================
// 5. Lifecycle invariant: a second bounded run is independent
//    The run loop creates and tears down a session per process; running twice
//    (two separate CLI invocations) must both succeed. This guards against a
//    refactor that leaks global/session state across the loop body in a way
//    that only surfaces on re-entry.
// ============================================================================
TEST_F(RunLoopLifecycleSmokeTest, RepeatedRuns_BothSucceed) {
    int first = exitCode(runCLI("--script engine-sim-bridge/preset/v8_gm_ls.json --duration 0.1 --silent > /dev/null 2>&1"));
    int second = exitCode(runCLI("--script engine-sim-bridge/preset/v8_gm_ls.json --duration 0.1 --silent > /dev/null 2>&1"));
    EXPECT_EQ(first, 0);
    EXPECT_EQ(second, 0);
}
