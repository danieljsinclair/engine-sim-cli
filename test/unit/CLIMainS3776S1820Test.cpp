// CLIMainS3776S1820Test.cpp
//
// Test architect A (writer) — pre-refactor coverage for the S3776 (cognitive
// complexity) / S1820 (too many struct fields) SRP refactor in CLIMain.cpp +
// CLIconfig.h.
//
// WHY THIS FILE EXISTS:
// A separate fix agent will extract SRP-compliant units out of the giant
// main()/createInputProvider blob and the over-stuffed CommandLineArgs struct to
// make the `--live-telemetry --start-from` plumbing (and the negative-exhaust-
// flow reversion bug) tractable. Tests MUST exist BEFORE that behavior change so
// the refactor is regression-locked. See the agreed seam in the header comment
// of each section below.
//
// GLOBAL RULE (red-phase MUST compile): every test in this file compiles and the
// file links into unit_tests unconditionally. Two classes of tests:
//
//   (A) LINKABLE-NOW tests — exercise REAL production code that is already
//       externally linkable today: parseArguments() (CLIconfig.cpp, normal
//       linkage). These run for real right now.
//
//   (B) EXTRACTED-UNIT SPEC tests — pin the BEHAVIOR the fix agent must expose
//       (audio-mode selection, telemetry-source factory start-from wiring, the
//       broken-out CommandLineArgs sub-structs). They are compiled but the
//       assertions are gated behind SLIPLOCK_REFACTOR_EXPOSED so the file links
//       cleanly before the fix agent lands the symbols. Once the fix agent
//       exposes the seams, flipping the macro switch turns these into live
//       red→green tests with zero rewrite. The spec (intent, not messages) is
//       the contract.
//
// COVERAGE STATUS (honest): RUNNING coverage today = the --start-from PARSE
// LAYER only (parseArguments -> args.replay.startFromS_). The FORWARD hop — that
// the extracted provider factory actually forwards startFromS_ into the live
// SYNC-PULL path (the real bug: "--start-from ignored in live real-time mode")
// — is NOT pinned by anything that runs. That code lives in CLIMain.cpp's
// anonymous namespace and is not in unit_tests' link set. Forward-hop coverage
// is a FIX-AGENT DELIVERABLE, locked behind SLIPLOCK_REFACTOR_EXPOSED (Section B).
// The bug is NOT fully covered until that seam is exposed and Section B goes live.

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "config/CLIconfig.h"          // CommandLineArgs, parseArguments
#include "config/CliException.h"        // CliException
#include "config/ExecutablePath.h"     // cli::ExecutablePath (resolveConfigPaths dep)

// ============================================================================
// Section A — LINKABLE-NOW: parseArguments owns the --start-from plumbing.
//
// This is the REAL production entry point that parses --start-from / --end-at
// into doubles (CLIconfig.cpp processArgs lines 244-257) and sets the
// live/replay input flags. The flag -> startFromS_ wiring is the same field the
// fix agent must keep correct through the createInputProvider refactor, so we
// lock it at the highest linkable surface TODAY.
// ============================================================================

namespace {

// Run parseArguments over an argv vector; returns the populated args.
CommandLineArgs parseArgs(const std::vector<std::string>& argv) {
    std::vector<char*> argvPtrs;
    argvPtrs.reserve(argv.size() + 1);
    for (const auto& a : argv) {
        argvPtrs.push_back(const_cast<char*>(a.c_str()));
    }
    argvPtrs.push_back(nullptr);

    CommandLineArgs args;
    bool ok = parseArguments(static_cast<int>(argv.size()), argvPtrs.data(), args);
    EXPECT_TRUE(ok) << "parseArguments rejected a valid flag set: "
                    << argv[0];
    return args;
}

}  // namespace

// Spec: --start-from 01:30 (hh:mm:ss) -> startFromS_ == 90.0, and the raw
// string is forwarded unchanged. This is the EXACT scenario the bug report
// cites ("--live-telemetry --start-from 01:30 ignores --start-from").
TEST(CLIMainStartFromPlumbing, StartFrom_hhmmss_ParsesToSeconds) {
    auto args = parseArgs({"prog", "--live-telemetry", "--start-from", "01:30"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 90.0);
    EXPECT_EQ(args.replay.startFrom, "01:30");
    EXPECT_TRUE(args.liveTelemetry);
}

TEST(CLIMainStartFromPlumbing, StartFrom_mmss_ParsesToSeconds) {
    auto args = parseArgs({"prog", "--replay-telemetry", "trace.csv", "--start-from", "1:30"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 90.0);
    EXPECT_FALSE(args.replay.telemetryPath.empty());
}

TEST(CLIMainStartFromPlumbing, StartFrom_PlainSeconds_ParsesToSeconds) {
    auto args = parseArgs({"prog", "--live-telemetry", "--start-from", "45"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 45.0);
    EXPECT_EQ(args.replay.startFrom, "45");
}

// Spec: no --start-from -> startFromS_ stays at its -1.0 "skip" sentinel.
// Critical: the provider must TREAT -1.0 as "start from trace beginning", never
// as a real offset, or the replay/live would silently seek to t=0 with flow at
// rest (the reversion trap).
TEST(CLIMainStartFromPlumbing, NoStartFrom_KeepsSkipSentinel) {
    auto args = parseArgs({"prog", "--live-telemetry"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, -1.0);
    EXPECT_TRUE(args.replay.startFrom.empty());
    EXPECT_EQ(args.liveTelemetry, true);
}

// Spec: --end-at mirrors --start-from parsing (seconds + mm:ss).
TEST(CLIMainStartFromPlumbing, EndAt_ParsesToSeconds) {
    auto args = parseArgs({"prog", "--replay-telemetry", "trace.csv", "--start-from", "10", "--end-at", "2:00"});
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 10.0);
    EXPECT_DOUBLE_EQ(args.replay.endAtS, 120.0);
    EXPECT_EQ(args.replay.endAt, "2:00");
}

// Spec: --end-at bounds BOTH transports (stop at that relative timecode or
// input end) — it must NOT require --replay-telemetry. Live (stdin pipe) runs
// stop at the bound via the provider's EOF report.
TEST(CLIMainStartFromPlumbing, EndAt_AcceptsLiveTelemetryWithoutReplay) {
    auto args = parseArgs({"prog", "--live-telemetry", "--end-at", "1:05"});
    EXPECT_DOUBLE_EQ(args.replay.endAtS, 65.0);
    EXPECT_EQ(args.liveTelemetry, true);
}

// Spec: invalid --start-from string -> parseArguments returns false (fail-fast
// on a malformed time, never silently defaulting to t=0). Asserts the TYPE of
// failure (boolean false), not the exact stderr text.
TEST(CLIMainStartFromPlumbing, InvalidStartFrom_FailsParse) {
    std::vector<char*> argvPtrs;
    std::vector<std::string> argv = {"prog", "--live-telemetry", "--start-from", "not-a-time"};
    for (auto& a : argv) argvPtrs.push_back(const_cast<char*>(a.c_str()));
    argvPtrs.push_back(nullptr);

    CommandLineArgs args;
    bool ok = parseArguments(static_cast<int>(argv.size()), argvPtrs.data(), args);
    EXPECT_FALSE(ok);
}

// Spec: --live-telemetry and --replay-telemetry are mutually exclusive at parse
// time (the provider factory MUST never see both true). This guards the
// factory's branch that the fix agent extracts.
TEST(CLIMainInputRouting, LiveAndReplayExclusive_ParseFails) {
    std::vector<char*> argvPtrs;
    std::vector<std::string> argv = {"prog", "--live-telemetry", "--replay-telemetry", "t.csv"};
    for (auto& a : argv) argvPtrs.push_back(const_cast<char*>(a.c_str()));
    argvPtrs.push_back(nullptr);

    CommandLineArgs args;
    bool ok = parseArguments(static_cast<int>(argv.size()), argvPtrs.data(), args);
    EXPECT_FALSE(ok);
}

// Spec: --live-telemetry WITH --script drives a NAMED .mr (the C63 scenario from
// the bug). engineConfig must receive the script path (not drop to preset[0]).
TEST(CLIMainInputRouting, LiveWithScript_SetsEngineConfig) {
    auto args = parseArgs({"prog", "--live-telemetry", "--script", "C63_M156_V3.mr", "--start-from", "01:30"});
    EXPECT_TRUE(args.liveTelemetry);
    EXPECT_EQ(args.engineConfig, "C63_M156_V3.mr");
    EXPECT_DOUBLE_EQ(args.replay.startFromS, 90.0);
}

// ============================================================================
// Section B — EXTRACTED-UNIT SPEC (gated by SLIPLOCK_REFACTOR_EXPOSED).
//
// >>> BLOCKED / NOT YET RUNNING — honest status per review <<<
// The brief's CRITICAL requirement is to pin the FORWARD hop of --start-from:
// that the *live provider / SimConfig builder* actually forwards startFromS_
// into the SYNC-PULL live path (the real bug: "--start-from ignored in live
// real-time mode"). Section A only pins the PARSE hop (parseArguments sets
// args.replay.startFromS correctly) — which already works. The forward hop is
// owned by createInputProvider() / a to-be-extracted telemetry-source factory,
// which currently live in CLIMain.cpp's anonymous namespace (internal linkage)
// and are NOT compiled into unit_tests. So a running forward-hop test cannot
// exist until the fix agent EXPOSES those seams (buildTelemetryProvider /
// resolveAudioMode / a startFromS_ getter on the provider).
//
// We do NOT pretend to cover it. The tests below are the SPEC the fix agent must
// satisfy: when they flip SLIPLOCK_REFACTOR_EXPOSED on (after exposing the
// seams), these become live red→green assertions with no rewrite. Until then,
// forward-hop coverage is BLOCKED and explicitly flagged as such.
//
// CONTRACT (fix agent MUST honor, or Section B becomes a COMPILE break, not a
// red test — red-phase rule applies to them too). The extraction must expose
// these exact public symbols:
//   - AudioMode resolveAudioMode(const SimulationConfig&)
//       -> AudioMode::Deterministic | SyncPull | Threaded
//   - std::unique_ptr<input::IInputProvider>
//       buildTelemetryProvider(const CommandLineArgs&, ILogging*)
//       -> returns LiveTelemetryProvider (--live-telemetry) or
//          ReplayTelemetryProvider (--replay-telemetry)
//   - getStartFromS() / getEndAtS() getters on the provider (readable after the
//       builder returns it), with the provider castable from input::IInputProvider
// If the fix agent's extraction names these differently, update the symbols
// below in lockstep — do NOT silently rename and leave Section B stranded.
//
// AGREED REFACTOR SEAM (architect A + B):
//   1. AudioMode resolveAudioMode(const SimulationConfig&)
//        deterministic -> AudioMode::Deterministic
//        else syncPull -> AudioMode::SyncPull
//        else           -> AudioMode::Threaded
//      (extracts CLIMain.cpp lines 499-506, a flattened S3358/S3776 site)
//   2. std::unique_ptr<input::IInputProvider> buildTelemetryProvider(args, ...)
//        --live-telemetry    -> LiveTelemetryProvider with startFromS_ wired,
//                               wheel-coupling + coupling-model forwarded,
//                               warmBootToRunning() called AFTER coupling flags.
//        --replay-telemetry -> ReplayTelemetryProvider with startFromS_/endAtS_
//                               wired + validateReplayTimeSlicing.
//      (extracts createInputProvider lines 106-279 — the S3776 core)
//   3. CommandLineArgs (S1820) -> composed of SRP sub-structs so no single
//      struct exceeds the field threshold. The live clutch coupling flags move
//      OUT of the top-level CommandLineArgs into a dedicated TwinArgs sub-struct
//      (today they are direct members: CLIconfig.h:71 wheelCoupling, :79
//      couplingModel). The seam doc names the target; the concrete test for the
//      split is deferred until the sub-struct exists.
// ============================================================================

#ifdef SLIPLOCK_REFACTOR_EXPOSED

#include "strategy/IAudioBuffer.h"        // AudioMode
#include "simulation/SimulationLoop.h"    // SimulationConfig
#include "input/IInputProvider.h"         // input::IInputProvider
#include "input/ITelemetryProvider.h"

// Spec 1: audio-mode selection maps the three config states deterministically.
TEST(CLIMainRefactorAudioMode, Deterministic_SelectsDeterministic) {
    SimulationConfig cfg;
    cfg.deterministic = true;
    cfg.syncPull = true;
    EXPECT_EQ(resolveAudioMode(cfg), AudioMode::Deterministic);
}

TEST(CLIMainRefactorAudioMode, SyncPull_SelectsSyncPull) {
    SimulationConfig cfg;
    cfg.deterministic = false;
    cfg.syncPull = true;
    EXPECT_EQ(resolveAudioMode(cfg), AudioMode::SyncPull);
}

TEST(CLIMainRefactorAudioMode, Threaded_SelectsThreaded) {
    SimulationConfig cfg;
    cfg.deterministic = false;
    cfg.syncPull = false;
    EXPECT_EQ(resolveAudioMode(cfg), AudioMode::Threaded);
}

// Spec 2 (FORWARD HOP — the bug-pinning test): the telemetry-source factory
// wires startFromS_ identically for live and replay, so --start-from is honored
// on BOTH paths. This is the regression lock for "--start-from ignored in live
// SYNC-PULL mode." Asserts the builder returns the right concrete provider type
// AND that the startFromS_ it carries equals the parsed arg (90.0 == 01:30).
TEST(CLIMainRefactorTelemetryFactory, Live_WiresStartFromAndReturnsLiveProvider) {
    CommandLineArgs args;
    args.liveTelemetry = true;
    args.replay.startFromS = 90.0;
    args.replay.startFrom = "01:30";
    args.wheelCoupling = "pin";
    args.couplingModel = "torque-converter";

    auto provider = buildTelemetryProvider(args, /*logger=*/nullptr);
    ASSERT_NE(provider, nullptr);
    EXPECT_NE(dynamic_cast<input::LiveTelemetryProvider*>(provider.get()), nullptr);
    EXPECT_DOUBLE_EQ(provider->getStartFromS(), 90.0);
}

TEST(CLIMainRefactorTelemetryFactory, Replay_WiresStartFromAndReturnsReplayProvider) {
    CommandLineArgs args;
    args.replay.telemetryPath = "trace.csv";
    args.replay.startFromS = 30.0;
    args.replay.endAtS = 120.0;
    args.wheelCoupling = "pin";
    args.couplingModel = "clutch-map";

    auto provider = buildTelemetryProvider(args, /*logger=*/nullptr);
    ASSERT_NE(provider, nullptr);
    auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(provider.get());
    ASSERT_NE(replay, nullptr);
    EXPECT_DOUBLE_EQ(replay->getStartFromS(), 30.0);
    EXPECT_DOUBLE_EQ(replay->getEndAtS(), 120.0);
}

// Spec 3: invalid --wheel-coupling on the live path MUST fail-fast (the bug
// report's twin reversion is downstream of a SILENT fallback — we forbid that).
TEST(CLIMainRefactorTelemetryFactory, Live_InvalidWheelCoupling_Throws) {
    CommandLineArgs args;
    args.liveTelemetry = true;
    args.wheelCoupling = "bogus-mode";
    EXPECT_THROW(buildTelemetryProvider(args, nullptr), CliException);
}

#endif  // SLIPLOCK_REFACTOR_EXPOSED
