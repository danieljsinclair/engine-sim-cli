// GearboxReconfigureCharacterizationTest.cpp — F5 characterization net
// (consolidation wave B, 2026-09-07). THE HIGHEST-RISK MOVE OF THE WAVE:
// reconfigureGearboxProviders (src/config/CLIMain.cpp:511-556), including the
// Bug-C3 refusal policy (:520-529), moves CLI-side -> bridge.
//
// SEAM ACCESS (why the .cpp include): reconfigureGearboxProviders takes
// `const InputContext&`, and InputContext is defined inside CLIMain.cpp's
// anonymous namespace — there is no linkable symbol and no header. The only
// way to exercise the REAL production function is to compile the production
// TU into this test TU (the classic characterization seam): CLIMain.cpp is
// #included below with main() macro-renamed out of the way (gtest_main owns
// main). ZERO production files are modified.
//
// WHAT IS PINNED (current observable behaviour, all through the real function):
//   1. C3 refusal — LIVE path only: a live provider + a simulator with no
//      transmission/vehicle geometry throws CliException naming the refusal
//      ("Refusing to silently fall back to zf8hp45"). Error-over-silence is
//      the point: the twin must never silently drive on the hardcoded ZF
//      default profile when the loaded .mr supplied no geometry.
//   2. Replay path with no geometry: SILENT no-op (the refusal is deliberately
//      live-only — legacy replay scripts may legitimately run the default).
//   3. Geometry present: the matching provider's reconfigureProfile() is
//      reached with the simulator's ACTUAL ratios. Pinned behaviorally via
//      the twin's upshift cap: the Sine geometry carries exactly ONE forward
//      gear, so a reconfigured twin can no longer upshift out of 1st at
//      80 km/h WOT — where the zf8hp45 default (8 gears) does.
//   4. A non-BridgeSimulator ISimulator is skipped ENTIRELY — the cast guard
//      precedes even the C3 check.
//
// All simulator-side objects are REAL production classes (SineSimulator/
// SineEngine/SineVehicle/SineTransmission composed exactly as ISimulatorTest
// composes them; BridgeSimulator wraps the result). Providers are the real
// LiveTelemetryProvider / ReplayTelemetryProvider / DemoInputProvider.

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <unistd.h>

#include "simulator/GearConventions.h"  // bridge::GearSelector
#include "simulator/ISimulator.h"
#include "simulator/SineEngine.h"
#include "simulator/SineSimulator.h"
#include "simulator/SineTransmission.h"
#include "simulator/SineVehicle.h"

// Compile the production TU into this test TU. main is renamed so gtest_main
// keeps sole ownership of the entry point; the renamed function is never
// called. Everything CLIMain.cpp declares — including its anonymous-namespace
// InputContext and reconfigureGearboxProviders — becomes visible HERE.
#define main engineSimCliMainExcludedFromUnitTestTu
#include "config/CLIMain.cpp"
#undef main

namespace {

constexpr int kDrive = static_cast<int>(bridge::GearSelector::DRIVE);

// ---------------------------------------------------------------------------
// Real-geometry helpers (mirror ISimulatorTest's composition exactly)
// ---------------------------------------------------------------------------

// SineTransmission = exactly ONE forward gear (ratio 1.0); SineVehicle =
// diff 1.0 / tire radius 0.3 m. That single-gear count is the observable the
// happy-path differential rides on.
std::unique_ptr<BridgeSimulator> makeBridgeSim(bool withGeometry) {
    auto sine = std::make_unique<SineSimulator>();
    Simulator::Parameters simParams;
    simParams.systemType = Simulator::SystemType::NsvOptimized;
    sine->initialize(simParams);
    sine->setSimulationFrequency(EngineSimDefaults::SIMULATION_FREQUENCY);
    sine->setFluidSimulationSteps(EngineSimDefaults::FLUID_SIMULATION_STEPS);
    sine->setTargetSynthesizerLatency(EngineSimDefaults::TARGET_SYNTH_LATENCY);
    if (withGeometry) {
        sine->loadSimulation(new SineEngine(), new SineVehicle(), new SineTransmission());
    }
    return std::make_unique<BridgeSimulator>(std::move(sine));
}

// InputContext is CLIMain.cpp's anonymous-namespace struct; the TU include
// above makes it nameable here.
InputContext makeContext(std::unique_ptr<input::IInputProvider> provider) {
    InputContext ctx;
    ctx.provider = std::move(provider);
    return ctx;
}

// Live provider on an in-memory CSV stream (the same construction shape
// TelemetryProviderFactory uses for --live-telemetry, minus the factory's
// std::cin binding which a unit test must not own).
struct LiveCsv {
    std::istringstream stream;
    std::unique_ptr<input::LiveTelemetryProvider> provider;
    explicit LiveCsv(const char* csv) : stream(csv) {
        provider = std::make_unique<input::LiveTelemetryProvider>(
            stream, /*autoStart=*/true);
    }
};

// Pump RPM feedback so the twin's engine catches, then run until gear
// advances past `target` or the budget is exhausted (mirrors the bridge's
// LiveTelemetryProviderTest helper).
int runUntilGearAbove(input::LiveTelemetryProvider& p, int target, int maxTicks) {
    int gear = -1;
    for (int i = 0; i < maxTicks; ++i) {
        EngineSimStats stats;
        stats.currentRPM = 900.0;
        p.provideFeedback(stats);
        gear = p.OnUpdateSimulation(0.05).gearAbsolute;
        if (gear > target) break;
    }
    return gear;
}

// Self-contained temp trace file.
class TempTrace {
public:
    explicit TempTrace(const std::string& csv) {
        path_ = "/tmp/consarch_b_f5_" + std::to_string(::getpid()) + "_" +
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

// A minimal non-BridgeSimulator ISimulator — the cast-guard collaborator.
class NullSimulator final : public ISimulator {
public:
    bool create(const ISimulatorConfig&, ILogging*, telemetry::ITelemetryWriter*) override {
        return false;
    }
    void destroy() override {}
    std::string getLastError() const override { return {}; }
    const char* getName() const override { return "NullSimulator"; }
    void update(double) override {}
    EngineSimStats getStats() const override { return {}; }
    void setThrottle(double) override {}
    bool renderOnDemand(float*, int, int32_t*) override { return false; }
    bool readAudioBuffer(float*, int, int*) override { return false; }
    bool start() override { return false; }
    void stop() override {}
    int getSimulationFrequency() const override { return 0; }
};

}  // namespace

// ============================================================================
// 1. The Bug-C3 refusal branch (live-only, error-over-silence)
// ============================================================================

TEST(GearboxReconfigureCharacterization, LiveProviderWithoutGeometry_RefusesInsteadOfZfFallback) {
    // No loadSimulation: the raw simulator has NO transmission and NO vehicle.
    auto sim = makeBridgeSim(/*withGeometry=*/false);
    LiveCsv live("time_s,throttle_pct,road_speed_kmh\n0.0,50,30\n");
    auto ctx = makeContext(std::move(live.provider));

    try {
        reconfigureGearboxProviders(sim.get(), ctx);
        FAIL() << "C3 policy: live telemetry with no transmission/vehicle geometry "
                  "must throw instead of silently falling back to zf8hp45";
    } catch (const CliException& e) {
        // Type + the key identifying content (intent, not the full sentence).
        const std::string msg = e.what();
        EXPECT_NE(msg.find("Live telemetry"), std::string::npos);
        EXPECT_NE(msg.find("no transmission"), std::string::npos);
        EXPECT_NE(msg.find("Refusing to silently fall back to zf8hp45"), std::string::npos);
        EXPECT_NE(msg.find("transmission"), std::string::npos);
    }
}

TEST(GearboxReconfigureCharacterization, ReplayProviderWithoutGeometry_IsSilentNoOp) {
    // The refusal is deliberately LIVE-only: legacy replay scripts may
    // legitimately run on the default profile, so no geometry + replay =
    // keep the provider's default, silently, and stay connected.
    auto sim = makeBridgeSim(false);
    TempTrace trace("time_s,throttle_pct,road_speed_kmh,gear_selector\n0.0,50,30,D\n");
    auto replay = std::make_unique<input::ReplayTelemetryProvider>(
        trace.path(), /*autoStart=*/true, /*autoGearbox=*/true);
    ASSERT_TRUE(replay->Initialize());
    ASSERT_TRUE(replay->IsConnected());
    auto ctx = makeContext(std::move(replay));

    EXPECT_NO_THROW(reconfigureGearboxProviders(sim.get(), ctx));
    EXPECT_TRUE(ctx.provider->IsConnected())
        << "the silent no-op must leave the replay provider untouched";
}

// ============================================================================
// 2. Happy path with geometry: the ratios actually reach the twin
// ============================================================================

TEST(GearboxReconfigureCharacterization, LiveProviderWithGeometry_RatiosReachTheTwin) {
    // Baseline (no reconfigure): the twin's default zf8hp45 profile has 8
    // forward gears and upshifts out of 1st at 80 km/h WOT.
    {
        LiveCsv live("time_s,throttle_pct,road_speed_kmh\n0.0,100,80\n");
        ASSERT_TRUE(live.provider->Initialize());
        live.provider->setIgnition(true);
        live.provider->setGearSelector(kDrive);
        ASSERT_GT(runUntilGearAbove(*live.provider, /*target=*/1, /*maxTicks=*/200), 1)
            << "baseline: the default zf8hp45 twin must upshift at 80 km/h WOT";
    }
    // Through the seam under test: reconfigure to the Sine geometry's single
    // forward gear — the twin's upshift loop is bounded by the ratio count,
    // so it can no longer leave 1st.
    {
        auto sim = makeBridgeSim(true);
        LiveCsv live("time_s,throttle_pct,road_speed_kmh\n0.0,100,80\n");
        ASSERT_TRUE(live.provider->Initialize());
        live.provider->setIgnition(true);
        live.provider->setGearSelector(kDrive);
        auto ctx = makeContext(std::move(live.provider));

        EXPECT_NO_THROW(reconfigureGearboxProviders(sim.get(), ctx));
        auto* p = dynamic_cast<input::LiveTelemetryProvider*>(ctx.provider.get());
        ASSERT_NE(p, nullptr);
        EXPECT_EQ(runUntilGearAbove(*p, /*target=*/1, /*maxTicks=*/200), 1)
            << "Sine geometry is 1 gear: the reconfigured twin must cap at 1st";
    }
}

TEST(GearboxReconfigureCharacterization, ReplayProviderWithGeometry_NoThrowAndTwinStillDrives) {
    // Replay + geometry: no throw, provider stays healthy, and the twin still
    // drives the box from the CSV (forward gear engaged in auto mode).
    // (The ratio-count cap for the REPLAY twin is pinned bridge-side in
    // ReplayTelemetryProviderTest Group 5; here we pin the seam's effect on
    // the provider through CLIMain's selection logic.)
    auto sim = makeBridgeSim(true);
    TempTrace trace("time_s,throttle_pct,road_speed_kmh,gear_selector\n0.0,100,80,D\n");
    auto replay = std::make_unique<input::ReplayTelemetryProvider>(
        trace.path(), /*autoStart=*/true, /*autoGearbox=*/true);
    ASSERT_TRUE(replay->Initialize());
    auto ctx = makeContext(std::move(replay));

    EXPECT_NO_THROW(reconfigureGearboxProviders(sim.get(), ctx));
    auto* p = dynamic_cast<input::ReplayTelemetryProvider*>(ctx.provider.get());
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(p->IsConnected());
    p->provideFeedback(EngineSimStats{});
    const auto in = p->OnUpdateSimulation(0.016);
    EXPECT_TRUE(in.gearAutoMode);
    EXPECT_GT(in.gearAbsolute, 0) << "a DRIVE row at 80 km/h must hold a forward gear";
}

TEST(GearboxReconfigureCharacterization, DemoProviderWithGeometry_RatiosReachTheDemoBox) {
    // The keyboard/--auto path: DemoInputProvider is built by CLIMain with
    // default-constructible collaborators + the zf8hp45 profile (CLIMain.cpp
    // buildKeyboardInput) — mirrored here. Driving recipe mirrors the bridge's
    // DemoChainIntegrationTest redline repro: P->R->N->D via shiftUp()s, then
    // redline RPM + rising speed feedback. After the seam reconfigures the
    // demo box to the 1-gear Sine geometry, the demo gearbox can never leave
    // 1st; the un-reconfigured zf8hp45 baseline does.
    const auto profile = twin::IceVehicleProfile::zf8hp45();

    // Drive helper: P->R->N->D, half throttle, redline RPM + rising speed.
    auto drivePastFirst = [&profile](input::DemoInputProvider& p) {
        p.setIgnition(true);
        p.shiftUp();
        p.shiftUp();
        p.shiftUp();
        p.setThrottle(0.5);
        int gear = 0;
        for (int i = 0; i < 400; ++i) {
            p.setThrottle(0.5);  // hold the pedal: DemoThrottleSource decays
            EngineSimStats stats{};
            stats.currentRPM = profile.redlineRpm * 0.98;
            stats.vehicleSpeedKmh = 30.0 + i * 0.2;
            stats.drivetrainTorqueNm = 100.0;
            p.provideFeedback(stats);
            gear = p.OnUpdateSimulation(0.05).gearAbsolute;
        }
        return gear;
    };

    // Baseline (no reconfigure): the default 8-gear zf8hp45 demo box upshifts.
    {
        auto baseline = std::make_unique<input::DemoInputProvider>(
            std::make_unique<input::DemoThrottleSource>(),
            std::make_unique<input::GearSelectorInput>(),
            std::make_unique<input::IgnitionInput>(),
            twin::IceVehicleProfile::zf8hp45());
        ASSERT_TRUE(baseline->Initialize());
        ASSERT_GT(drivePastFirst(*baseline), 1)
            << "baseline: the default zf8hp45 demo box must upshift at redline";
    }

    // Through the seam under test: reconfigured to the Sine geometry's single
    // forward gear, the demo box caps at 1st.
    auto sim = makeBridgeSim(true);
    auto demo = std::make_unique<input::DemoInputProvider>(
        std::make_unique<input::DemoThrottleSource>(),
        std::make_unique<input::GearSelectorInput>(),
        std::make_unique<input::IgnitionInput>(),
        twin::IceVehicleProfile::zf8hp45());
    ASSERT_TRUE(demo->Initialize());
    InputContext ctx;  // demo lives in demoProvider; provider stays null
    ctx.demoProvider = std::move(demo);

    EXPECT_NO_THROW(reconfigureGearboxProviders(sim.get(), ctx));
    auto* d = dynamic_cast<input::DemoInputProvider*>(ctx.demoProvider.get());
    ASSERT_NE(d, nullptr);
    EXPECT_EQ(drivePastFirst(*d), 1)
        << "Sine geometry is 1 gear: the reconfigured demo box must cap at 1st";
}

// ============================================================================
// 3. The cast guard: a non-BridgeSimulator ISimulator is skipped entirely
// ============================================================================

TEST(GearboxReconfigureCharacterization, NonBridgeSimulator_IsSkippedBeforeTheC3Check) {
    // Live provider + no geometry would THROW under the C3 policy — but the
    // simulator is not a BridgeSimulator, so the guard returns first. This
    // pins the guard's precedence over the C3 branch.
    NullSimulator sim;
    LiveCsv live("time_s,throttle_pct,road_speed_kmh\n0.0,50,30\n");
    auto ctx = makeContext(std::move(live.provider));
    EXPECT_NO_THROW(reconfigureGearboxProviders(&sim, ctx));
}

TEST(GearboxReconfigureCharacterization, NullSimulatorPointer_IsASilentNoOp) {
    LiveCsv live("time_s,throttle_pct,road_speed_kmh\n0.0,50,30\n");
    auto ctx = makeContext(std::move(live.provider));
    EXPECT_NO_THROW(reconfigureGearboxProviders(nullptr, ctx));
}
