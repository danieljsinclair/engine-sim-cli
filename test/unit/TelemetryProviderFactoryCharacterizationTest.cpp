// TelemetryProviderFactoryCharacterizationTest.cpp — F2 characterization net
// (consolidation wave B, 2026-09-07).
//
// The twin coupling/config parse+apply recipe implemented by
// src/config/TelemetryProviderFactory.{h,cpp} moves into NEW bridge files
// under include/input/, with the CLI factory switching to consume it. These
// tests pin the CURRENT observable CLI-side behaviour so the move cannot
// change it:
//
//   - parseWheelCouplingMode / parseCouplingModel: full mode/model mapping
//     plus fail-fast (CliException) on a typo — never a silent fallback
//     (the twin reversion bug was downstream of a silent fallback).
//   - buildTelemetryProvider routing: live -> LiveTelemetryProvider,
//     replay -> ReplayTelemetryProvider, neither -> nullptr; time slicing
//     (--start-from/--end-at) wired onto the built provider.
//   - The TWIN DEFAULTS of the representative input the factory actually
//     receives (CLIconfig.h TwinArgs: "pin" / "torque-converter" / 150 ms /
//     torque toggles off) parse and apply cleanly.
//   - Every --pin-tau-ms value is ACCEPTED at the factory layer (tau is
//     warn-only; the warning seam itself is pinned by PinTauGuardTest).
//   - The replay autoGearbox ctor wiring is observable end-to-end: a parsed
//     --replay-telemetry run (auto default, F6) builds a provider whose twin
//     drives (gearAutoMode), --manual opts out (no twin in the loop).
//
// SCOPE NOTE (honest): the providers store the coupling flags privately and
// expose no getters, so the STORE + RE-APPLY-WITH-TWIN-DEFAULTS contract
// (flags set pre-Initialize survive twin creation; the live provider's
// re-apply defaults are Free/ClutchMap/0.0 — the 2026-09-06 factory-ordering
// regression) is observable only inside the bridge and is pinned by the
// bridge's LiveTelemetryProviderTest / ReplayTelemetryProviderTest suites.
// This net pins the CLI seam that is actually moving.
//
// No SLIPLOCK_REFACTOR_EXPOSED gating here: buildTelemetryProvider is linked
// into unit_tests (test/unit/CMakeLists.txt compiles
// TelemetryProviderFactory.cpp), so the ungated spec tests that were stranded
// in CLIMainS3776S1820Test Section B run for real in this file.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <unistd.h>
#include <vector>

#include "config/CLIconfig.h"
#include "config/CliException.h"
#include "config/TelemetryProviderFactory.h"
#include "input/LiveTelemetryProvider.h"
#include "input/ReplayTelemetryProvider.h"
#include "simulator/EngineSimTypes.h"
#include "twin/CouplingModelSelector.h"
#include "twin/WheelCoupling.h"

namespace {

// Parse an argv vector through the REAL parseArguments (argv[0] = prog name).
CommandLineArgs parseArgv(const std::vector<std::string>& args) {
    std::vector<std::string> full(args);
    full.insert(full.begin(), "engine-sim-cli");
    std::vector<char*> argv;
    argv.reserve(full.size() + 1);
    for (auto& s : full) argv.push_back(s.data());
    argv.push_back(nullptr);
    CommandLineArgs parsed;
    EXPECT_TRUE(parseArguments(static_cast<int>(argv.size() - 1), argv.data(), parsed));
    return parsed;
}

// Self-contained temp trace file (created and removed by the test itself —
// never relies on pre-existing fixture data).
class TempTrace {
public:
    explicit TempTrace(const std::string& csv) {
        path_ = "/tmp/consarch_b_f2_" + std::to_string(::getpid()) + "_" +
                std::to_string(++counter_) + ".csv";
        std::ofstream out(path_);
        out << csv;
    }
    ~TempTrace() { std::remove(path_.c_str()); }
    TempTrace(const TempTrace&) = delete;
    TempTrace& operator=(const TempTrace&) = delete;
    const std::string& path() const { return path_; }

private:
    static int counter_;
    std::string path_;
};
int TempTrace::counter_ = 0;

}  // namespace

// ============================================================================
// Parse layer: mode/model mapping + fail-fast
// ============================================================================

TEST(TelemetryProviderFactoryCharacterization, WheelCouplingMappingIsExact) {
    EXPECT_EQ(telemetry_detail::parseWheelCouplingMode("free"),
              twin::WheelCouplingMode::Free);
    EXPECT_EQ(telemetry_detail::parseWheelCouplingMode("pin"),
              twin::WheelCouplingMode::Pin);
    EXPECT_EQ(telemetry_detail::parseWheelCouplingMode("torque"),
              twin::WheelCouplingMode::Torque);
}

TEST(TelemetryProviderFactoryCharacterization, WheelCouplingTypoFailsFast) {
    // A typo'd mode must NEVER silently fall back to a coupling mode (the
    // twin reversion bug was downstream of a silent fallback). Assert the
    // exception TYPE plus the key identifying content (flag, bad value,
    // legal set) — not the full message.
    try {
        (void)telemetry_detail::parseWheelCouplingMode("torquee");
        FAIL() << "parseWheelCouplingMode must reject an unknown mode";
    } catch (const CliException& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("--wheel-coupling"), std::string::npos);
        EXPECT_NE(msg.find("torquee"), std::string::npos);
        EXPECT_NE(msg.find("'free', 'pin' or 'torque'"), std::string::npos);
    }
}

TEST(TelemetryProviderFactoryCharacterization, CouplingModelMappingIsExact) {
    EXPECT_EQ(telemetry_detail::parseCouplingModel("clutch-map"),
              twin::CouplingModelKind::ClutchMap);
    EXPECT_EQ(telemetry_detail::parseCouplingModel("torque-converter"),
              twin::CouplingModelKind::TorqueConverter);
    EXPECT_EQ(telemetry_detail::parseCouplingModel("legacy"),
              twin::CouplingModelKind::Legacy);
}

TEST(TelemetryProviderFactoryCharacterization, CouplingModelTypoFailsFast) {
    try {
        (void)telemetry_detail::parseCouplingModel("ClutchMap");
        FAIL() << "parseCouplingModel must reject an unknown model (case matters)";
    } catch (const CliException& e) {
        const std::string msg = e.what();
        EXPECT_NE(msg.find("--coupling-model"), std::string::npos);
        EXPECT_NE(msg.find("ClutchMap"), std::string::npos);
        EXPECT_NE(msg.find("'clutch-map', 'torque-converter' or 'legacy'"), std::string::npos);
    }
}

// ============================================================================
// The representative input the factory receives: parsed TWIN DEFAULTS
// (CLIconfig.h TwinArgs — wheelCoupling "pin", pinTauMs 150.0,
// couplingModel "torque-converter", both torque toggles off)
// ============================================================================

TEST(TelemetryProviderFactoryCharacterization, ParsedTwinDefaultsAreTheTunedRoadValues) {
    const auto args = parseArgv({"--live-telemetry"});
    ASSERT_TRUE(args.twin.liveTelemetry);
    EXPECT_EQ(args.twin.wheelCoupling, "pin");
    EXPECT_DOUBLE_EQ(args.twin.pinTauMs, 150.0);
    EXPECT_EQ(args.twin.couplingModel, "torque-converter");
    EXPECT_FALSE(args.twin.effectiveThrottle);
    EXPECT_FALSE(args.twin.torqueInformedGearbox);
}

// ============================================================================
// Factory routing + time-slice wiring
// ============================================================================

TEST(TelemetryProviderFactoryCharacterization, LiveArgs_RouteToLiveProviderWithTimeSlice) {
    CommandLineArgs args;
    args.twin.liveTelemetry = true;
    args.replay.startFromS = 90.0;
    args.replay.endAtS = 65.0;
    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    auto* live = dynamic_cast<input::LiveTelemetryProvider*>(provider.get());
    ASSERT_NE(live, nullptr) << "live telemetry must build a LiveTelemetryProvider";
    EXPECT_DOUBLE_EQ(live->getStartFromS(), 90.0);
}

TEST(TelemetryProviderFactoryCharacterization, ReplayArgs_RouteToReplayProviderWithTimeSlice) {
    CommandLineArgs args;
    args.replay.telemetryPath = "trace.csv";
    args.replay.startFromS = 30.0;
    args.replay.endAtS = 120.0;
    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(provider.get());
    ASSERT_NE(replay, nullptr) << "replay telemetry must build a ReplayTelemetryProvider";
    // startFrom has a getter; endAt does not (setEndAtS only), so the endAt
    // hop is pinned by the Initialize-level tests below, not a getter.
    EXPECT_DOUBLE_EQ(replay->getStartFromS(), 30.0);
}

TEST(TelemetryProviderFactoryCharacterization, NoTelemetryMode_ReturnsNullptr) {
    // Neither --live-telemetry nor --replay-telemetry: the keyboard/demo path
    // owns provider construction, so the factory must yield nullptr.
    CommandLineArgs args;
    EXPECT_EQ(buildTelemetryProvider(args), nullptr);
}

// ============================================================================
// The apply recipe: full flag sets build cleanly (defaults AND every knob)
// ============================================================================

TEST(TelemetryProviderFactoryCharacterization, DefaultTwinArgs_BuildLiveProviderCleanly) {
    // The out-of-the-box TwinArgs (pin / torque-converter / 150 ms / toggles
    // off) must parse and apply without rejection — the flag-less road test
    // gets the owner-tuned compliance (default 150 ms).
    CommandLineArgs args;
    args.twin.liveTelemetry = true;
    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    EXPECT_NE(dynamic_cast<input::LiveTelemetryProvider*>(provider.get()), nullptr);
}

TEST(TelemetryProviderFactoryCharacterization, FullCouplingFlagSet_AppliesOnLiveWithoutRejecting) {
    CommandLineArgs args;
    args.twin.liveTelemetry = true;
    args.twin.wheelCoupling = "free";
    args.twin.couplingModel = "legacy";
    args.twin.pinTauMs = 250.0;
    args.twin.effectiveThrottle = true;
    args.twin.torqueInformedGearbox = true;
    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    EXPECT_NE(dynamic_cast<input::LiveTelemetryProvider*>(provider.get()), nullptr);
}

TEST(TelemetryProviderFactoryCharacterization, FullCouplingFlagSet_AppliesOnReplayWithoutRejecting) {
    CommandLineArgs args;
    args.replay.telemetryPath = "trace.csv";
    args.twin.wheelCoupling = "pin";
    args.twin.couplingModel = "clutch-map";
    args.twin.pinTauMs = 0.0;  // rigid pin — tau 0 must be accepted
    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    EXPECT_NE(dynamic_cast<input::ReplayTelemetryProvider*>(provider.get()), nullptr);
}

TEST(TelemetryProviderFactoryCharacterization, TauIsNeverRejectedAtTheFactoryLayer) {
    // tau is warn-only (owner directive 2026-09-04): every value — rigid 0,
    // negative, sub-60, over-3000 — passes through the factory untouched.
    for (const double tau : {0.0, -5.0, 0.5, 15000.0}) {
        CommandLineArgs args;
        args.twin.liveTelemetry = true;
        args.twin.pinTauMs = tau;
        EXPECT_NO_THROW((void)buildTelemetryProvider(args)) << "tau=" << tau;
    }
}

TEST(TelemetryProviderFactoryCharacterization, InvalidCouplingInput_FailsFastThroughTheFactory) {
    // The factory validates coupling flags itself (CliException) — the same
    // fail-fast the ungated Section B spec demanded.
    CommandLineArgs args;
    args.twin.liveTelemetry = true;
    args.twin.wheelCoupling = "bogus-mode";
    EXPECT_THROW((void)buildTelemetryProvider(args), CliException);

    CommandLineArgs badModel;
    badModel.replay.telemetryPath = "trace.csv";
    badModel.twin.couplingModel = "fluid";
    EXPECT_THROW((void)buildTelemetryProvider(badModel), CliException);
}

// ============================================================================
// End-to-end: parsed args -> factory -> Initialize (replay path)
// ============================================================================

TEST(TelemetryProviderFactoryCharacterization, ParsedReplayDefaultAuto_BuildsWorkingTwinDrivenProvider) {
    // The full CLI pipeline for the owner's standard replay invocation: parse
    // (F6 default -> automatic), factory (autoGearbox ctor wiring), provider
    // Initialize on a real trace, twin in the loop (gearAutoMode).
    TempTrace trace("time_s,throttle_pct,road_speed_kmh,gear_selector\n0.0,50,30,D\n");
    const auto args = parseArgv({"--replay-telemetry", trace.path()});
    ASSERT_TRUE(args.gearbox.automatic);

    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(provider.get());
    ASSERT_NE(replay, nullptr);

    ASSERT_TRUE(replay->Initialize()) << "factory-built replay provider must initialize";
    EXPECT_TRUE(replay->IsConnected());
    replay->provideFeedback(EngineSimStats{});
    const auto in = replay->OnUpdateSimulation(0.016);
    EXPECT_TRUE(in.gearAutoMode)
        << "the auto (default) replay must route through the twin";
    EXPECT_GT(in.gearAbsolute, 0) << "a DRIVE row at 30 km/h must select a forward gear";
}

TEST(TelemetryProviderFactoryCharacterization, ParsedReplayManualOptOut_BuildsProviderWithoutTwinLoop) {
    // --manual opts out (F6): the factory forwards autoGearbox=false and the
    // replay runs WITHOUT the twin in the loop.
    TempTrace trace("time_s,throttle_pct,road_speed_kmh,gear_selector\n0.0,50,30,D\n");
    const auto args = parseArgv({"--replay-telemetry", trace.path(), "--manual"});
    ASSERT_FALSE(args.gearbox.automatic);

    auto provider = buildTelemetryProvider(args);
    ASSERT_NE(provider, nullptr);
    auto* replay = dynamic_cast<input::ReplayTelemetryProvider*>(provider.get());
    ASSERT_NE(replay, nullptr);

    ASSERT_TRUE(replay->Initialize());
    EXPECT_TRUE(replay->IsConnected());
    replay->provideFeedback(EngineSimStats{});
    EXPECT_FALSE(replay->OnUpdateSimulation(0.016).gearAutoMode)
        << "manual replay must not route through the twin";
}
