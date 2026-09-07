// CsvPresentationTest.cpp - Contract tests for the --csv-out writer.
//
// The CSV is a machine contract consumed by three production scripts:
// verify_driveability.py (the driveability gate), smoke_gearbox.py (parses by
// header name), and roadtest_capture.sh (road-test per-frame captures). A
// silent schema drift breaks all three with no warning from this side — these
// tests lock the header and row rendering so drift fails HERE first.
//
// WHAT IS LOCKED (and why):
//   - Header: exact column names AND order. smoke_gearbox.py tolerates
//     reordering by name, but downstream column math is positional in places,
//     so order is part of the contract.
//   - Row: every column rendered from a controlled EngineState, so a
//     formatting regression (precision, rounding, enum mapping) fails loudly.
//   - Fail-fast: a bad --csv-out path aborts startup (CliException naming the
//     offending path) instead of silently dropping the capture.
//
// NOT COVERED: the exception path in Shutdown() (close-time flush failure)
// would need an injectable failing ofstream; the cerr diagnostic there is
// teardown-path code we accept as uncovered.

#include <gtest/gtest.h>
#include "presentation/CsvPresentation.h"
#include "config/CliException.h"
#include "io/IPresentation.h"
#include "simulator/GearConventions.h"
#include "simulation/EnginePhase.h"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <vector>

using namespace presentation;
using GS = bridge::GearSelector;

namespace {

// Column order is the contract (see file header). Named indices keep the
// positional assertions readable.
constexpr int kColWallClockMs = 0;
constexpr int kColSimTimeS = 1;
constexpr int kColRelTimeS = 2;
constexpr int kColLatencyMs = 3;
constexpr int kColRpm = 4;
constexpr int kColRpmRaw = 5;
constexpr int kColEngineState = 6;
constexpr int kColThrottlePct = 7;
constexpr int kColBrake = 8;
constexpr int kColIgnition = 9;
constexpr int kColGearSelector = 10;
constexpr int kColGearAuto = 11;
constexpr int kColGearPhysical = 12;
constexpr int kColClutchPressure = 13;
constexpr int kColRoadImpliedRpm = 14;
constexpr int kColCreepRelief = 15;
constexpr int kColVehicleSpeedKmh = 16;
constexpr int kColTargetSpeedKmh = 17;
constexpr int kColSimSpeedMph = 18;
constexpr int kColEngineTorqueNm = 19;
constexpr int kColDrivetrainTorqueNm = 20;
constexpr int kColDynoTorqueNm = 21;
constexpr int kColStarterEngaged = 22;
constexpr int kColExhaustFlowCm3s = 23;
constexpr int kColSynthOutRms = 24;
constexpr int kColCount = 25;

// A fully-controlled state: every rendered field set to a value whose rounded
// / fixed-precision rendering is unambiguous (no .5 halfway digits, where
// iostreams rounding mode could differ between libc++ versions).
EngineState makeCsvState() {
    EngineState s{};
    s.audio.timestamp = 12.3456;              // sim_time_s -> 12.346
    s.drivetrain.replayTimestampS = -1.0;     // -1 = live path: use audio clock
    s.drivetrain.inputTimestampMs = -1;       // unreported -> latency -1
    s.engine.rpm = 3000.6;                    // rounds to 3001
    s.engine.rpmRaw = 3001.4;                 // rounds to 3001
    s.engine.phase = EnginePhase::Running;
    s.controls.throttle = 0.47;               // -> 47 (%)
    s.controls.brakeLight = std::nullopt;     // unreported renders as 0
    s.controls.ignition = true;
    s.controls.gearSelector = static_cast<int>(GS::DRIVE);
    s.controls.gearAutoMode = true;
    s.controls.commandedSpeedKmh = 80.0;      // 3dp -> 80.000
    s.drivetrain.gear = 3;
    s.drivetrain.clutchPressure = 0.98765;    // 4dp -> 0.9877
    s.drivetrain.roadImpliedRpm = 2850.24;    // 1dp -> 2850.2
    s.drivetrain.creepReliefFired = true;
    s.drivetrain.vehicleSpeedKmh = 64.3214;   // 3dp -> 64.321
    s.drivetrain.speedMph = 40.5;             // 3dp -> 40.500
    s.drivetrain.dynoTorque = -12.24;         // 1dp -> -12.2
    s.engine.engineTorqueNm = 182.36;         // 1dp -> 182.4
    s.engine.drivetrainTorqueNm = 604.44;     // 1dp -> 604.4
    s.engine.starterEngaged = false;
    s.engine.exhaustFlow = 0.0123456;         // x1e6, 4dp -> 12345.6000
    s.engine.synthOutputRms = -1.0;           // "nothing rendered" sentinel
    return s;
}

class CsvPresentationTest : public ::testing::Test {
protected:
    void TearDown() override {
        std::error_code ec;
        std::filesystem::remove(path_, ec);
    }

    // Unique per-test path; removed in TearDown. The file is created by the
    // production writer under test, never relied upon pre-existing.
    std::filesystem::path path_ = std::filesystem::temp_directory_path() /
        ("csv_presentation_test_" + std::to_string(::getpid()) + "_" +
         std::to_string(reinterpret_cast<uintptr_t>(this)) + ".csv");

    std::vector<std::string> readLines() const {
        std::ifstream in(path_);
        std::vector<std::string> lines;
        std::string line;
        while (std::getline(in, line)) {
            lines.push_back(line);
        }
        return lines;
    }

    static std::vector<std::string> split(const std::string& line) {
        std::vector<std::string> fields;
        std::string field;
        std::istringstream stream(line);
        while (std::getline(stream, field, ',')) {
            fields.push_back(field);
        }
        return fields;
    }
};

TEST_F(CsvPresentationTest, HeaderIsTheMachineContract) {
    {
        CsvPresentation presentation(path_.string());
        presentation.Initialize(PresentationConfig{});
        presentation.ShowSimulatorStates(makeCsvState());
    }  // destroyed: Shutdown() must close cleanly

    const auto lines = readLines();
    ASSERT_EQ(lines.size(), 2u) << "header + one row expected";
    EXPECT_EQ(lines[0],
        "wall_clock_ms,sim_time_s,rel_time_s,latency_ms,rpm,rpm_raw,engine_state,"
        "throttle_gas_pct,brake,ignition,gear_selector,gear_auto,gear_physical,"
        "clutch_pressure,road_implied_rpm,creep_relief_fired,vehicle_speed_kmh,"
        "target_speed_kmh,sim_speed_mph,engine_torque_nm,drivetrain_torque_nm,"
        "dyno_torque_nm,starter_engaged,exhaust_flow_cm3s,synth_out_rms");
}

TEST_F(CsvPresentationTest, RowRendersEveryColumnFromControlledState) {
    {
        CsvPresentation presentation(path_.string());
        presentation.Initialize(PresentationConfig{});
        presentation.ShowSimulatorStates(makeCsvState());
    }

    const auto fields = split(readLines().at(1));
    ASSERT_EQ(fields.size(), static_cast<size_t>(kColCount));

    // Nondeterministic column: wall clock is "now" — assert sanity, not value.
    EXPECT_GT(std::stoll(fields[kColWallClockMs]), 0);

    EXPECT_EQ(fields[kColSimTimeS], "12.346");
    EXPECT_EQ(fields[kColRelTimeS], "12.346");  // live: same clock as absolute
    EXPECT_EQ(fields[kColLatencyMs], "-1");     // unreported input timestamp
    EXPECT_EQ(fields[kColRpm], "3001");         // 3000.6 rounds away from zero
    EXPECT_EQ(fields[kColRpmRaw], "3001");
    EXPECT_EQ(fields[kColEngineState], "Running");
    EXPECT_EQ(fields[kColThrottlePct], "47");
    EXPECT_EQ(fields[kColBrake], "0");          // nullopt -> same as off
    EXPECT_EQ(fields[kColIgnition], "1");
    EXPECT_EQ(fields[kColGearSelector], std::to_string(static_cast<int>(GS::DRIVE)));
    EXPECT_EQ(fields[kColGearAuto], "1");
    EXPECT_EQ(fields[kColGearPhysical], "3");
    EXPECT_EQ(fields[kColClutchPressure], "0.9877");
    EXPECT_EQ(fields[kColRoadImpliedRpm], "2850.2");
    EXPECT_EQ(fields[kColCreepRelief], "1");
    EXPECT_EQ(fields[kColVehicleSpeedKmh], "64.321");
    EXPECT_EQ(fields[kColTargetSpeedKmh], "80.000");
    EXPECT_EQ(fields[kColSimSpeedMph], "40.500");
    EXPECT_EQ(fields[kColEngineTorqueNm], "182.4");
    EXPECT_EQ(fields[kColDrivetrainTorqueNm], "604.4");
    EXPECT_EQ(fields[kColDynoTorqueNm], "-12.2");
    EXPECT_EQ(fields[kColStarterEngaged], "0");
    EXPECT_EQ(fields[kColExhaustFlowCm3s], "12345.6000");
    EXPECT_EQ(fields[kColSynthOutRms], "-1.0000");  // sentinel, still 4dp
}

TEST_F(CsvPresentationTest, BrakeLightOnRendersAsOne) {
    {
        CsvPresentation presentation(path_.string());
        presentation.Initialize(PresentationConfig{});
        EngineState state = makeCsvState();
        state.controls.brakeLight = true;
        presentation.ShowSimulatorStates(state);
    }
    EXPECT_EQ(split(readLines().at(1))[kColBrake], "1");
}

TEST_F(CsvPresentationTest, LatencyIsWallClockMinusInputTimestamp) {
    {
        CsvPresentation presentation(path_.string());
        presentation.Initialize(PresentationConfig{});
        EngineState state = makeCsvState();
        // Input stamped 100ms in the past: latency must be ~100ms. Wide upper
        // bound only — this is a wall-clock sanity check, not timing CI.
        const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count();
        state.drivetrain.inputTimestampMs = nowMs - 100;
        presentation.ShowSimulatorStates(state);
    }
    const long long latency = std::stoll(split(readLines().at(1))[kColLatencyMs]);
    EXPECT_GE(latency, 100);
    EXPECT_LT(latency, 60000);
}

TEST_F(CsvPresentationTest, ReplayTimestampOverridesAudioClockWhenReported) {
    {
        CsvPresentation presentation(path_.string());
        presentation.Initialize(PresentationConfig{});
        EngineState state = makeCsvState();
        state.drivetrain.replayTimestampS = 45.6789;  // >= 0: replay clock wins
        presentation.ShowSimulatorStates(state);
    }
    const auto fields = split(readLines().at(1));
    EXPECT_EQ(fields[kColSimTimeS], "45.679");
    EXPECT_EQ(fields[kColRelTimeS], "45.679");
}

TEST_F(CsvPresentationTest, ShowErrorWritesCommentLineAfterRows) {
    {
        CsvPresentation presentation(path_.string());
        presentation.Initialize(PresentationConfig{});
        presentation.ShowSimulatorStates(makeCsvState());
        presentation.ShowError("sensor dropout");
    }
    const auto lines = readLines();
    ASSERT_EQ(lines.size(), 3u);
    EXPECT_EQ(lines[2], "# sensor dropout");
}

TEST_F(CsvPresentationTest, EmissionDisabledSuppressesHeaderAndRows) {
    {
        CsvPresentation presentation(path_.string());
        presentation.setCsvEmissionEnabled(false);
        presentation.Initialize(PresentationConfig{});
        presentation.ShowSimulatorStates(makeCsvState());
    }
    EXPECT_TRUE(readLines().empty());
}

TEST_F(CsvPresentationTest, UnopenablePathFailsFastWithNamedPath) {
    const std::string bad = "/nonexistent-dir-for-csv-test/out.csv";
    CsvPresentation presentation(bad);
    try {
        presentation.Initialize(PresentationConfig{});
        FAIL() << "unopenable --csv-out path must abort startup";
    } catch (const CliException& e) {
        // Intent: the specific path is named so the operator can fix the flag.
        EXPECT_NE(std::string(e.what()).find(bad), std::string::npos);
    }
}

}  // namespace
