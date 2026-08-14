// CsvPresentation.cpp - Machine-parseable CSV output presentation.
// See CsvPresentation.h for design.

#include "presentation/CsvPresentation.h"
#include "simulation/EnginePhase.h"
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace presentation {

namespace {
// One-line name for the engine phase (plain text — no ANSI; the console's
// EnginePhaseName is colored, which we don't want in a CSV column).
const char* phaseName(EnginePhase phase) {
    switch (phase) {
        case EnginePhase::Stopped:   return "Stopped";
        case EnginePhase::Cranking:  return "Cranking";
        case EnginePhase::Stopping:  return "Stopping";
        case EnginePhase::Running:   return "Running";
        default: return "Unknown";
    }
}
}  // namespace

// ============================ CsvPresentation ============================

CsvPresentation::CsvPresentation(std::string path) : path_(std::move(path)) {}

CsvPresentation::~CsvPresentation() { Shutdown(); }

bool CsvPresentation::Initialize(const PresentationConfig& /*config*/) {
    out_.open(path_, std::ios::out | std::ios::trunc);
    // FAIL-FAST: a bad --csv-out path must abort at startup, not silently drop
    // the CSV (the caller would believe it is being written). Per the project's
    // exception-specificity rule, throw a specific std::runtime_error naming the
    // offending path — do not return false for the caller to interpret.
    if (!out_.is_open()) {
        throw std::runtime_error("CsvPresentation: cannot open '" + path_ + "' for writing");
    }
    return true;
}

void CsvPresentation::Shutdown() {
    if (out_.is_open()) out_.close();
}

void CsvPresentation::ShowSimulatorStates(const EngineState& s) {
    if (!out_.is_open()) return;
    if (!headerWritten_) {
        out_ << "time_s,rpm,rpm_raw,engine_state,throttle_gas_pct,brake,ignition,"
             << "gear_selector,gear_auto,gear_physical,"
             << "clutch_pressure,road_implied_rpm,creep_relief_fired,"
             << "vehicle_speed_kmh,target_speed_kmh,sim_speed_mph,"
             << "engine_torque_nm,drivetrain_torque_nm,dyno_torque_nm,"
             << "starter_engaged,exhaust_flow_cm3s,synth_out_rms\n";
        headerWritten_ = true;
    }
    const double timeS = s.drivetrain.replayTimestampS >= 0.0
                             ? s.drivetrain.replayTimestampS
                             : s.audio.timestamp;
    out_ << std::fixed << std::setprecision(3);
    out_ << timeS << ','
         << static_cast<long long>(std::round(s.engine.rpm)) << ','
         << static_cast<long long>(std::round(s.engine.rpmRaw)) << ','
         << phaseName(s.engine.phase) << ','
         << static_cast<int>(std::round(s.controls.throttle * 100.0)) << ','
         << std::setprecision(2) << s.controls.brakeLevel << ','
         << (s.controls.ignition ? 1 : 0) << ','
         << s.controls.gearSelector << ','
         << (s.controls.gearAutoMode ? 1 : 0) << ','
         << s.drivetrain.gear << ','
         << std::setprecision(4) << s.drivetrain.clutchPressure << ','
         << std::setprecision(1) << s.drivetrain.roadImpliedRpm << ','
         << (s.drivetrain.creepReliefFired ? 1 : 0) << ','
         << std::setprecision(3) << s.drivetrain.vehicleSpeedKmh << ','
         << s.controls.commandedSpeedKmh << ','
         << s.drivetrain.speedMph << ','
         << std::setprecision(1) << s.engine.engineTorqueNm << ','
         << s.engine.drivetrainTorqueNm << ','
         << s.drivetrain.dynoTorque << ','
         << (s.engine.starterEngaged ? 1 : 0) << ','
         << std::setprecision(4) << s.engine.exhaustFlow * 1e6 << ','
         // Post-leveler, pre-volume synth output RMS (int16 scale); -1 =
         // nothing rendered yet. Same quantity as the console [Out:] field.
         << s.engine.synthOutputRms << '\n';
    out_.flush();
}

void CsvPresentation::ShowMessage(const std::string& /*message*/) {}
void CsvPresentation::ShowError(const std::string& error) {
    // Errors are valuable for spelunking — surface them as a CSV comment line.
    if (out_.is_open()) out_ << "# " << error << '\n';
}
void CsvPresentation::ShowProgress(double /*currentTime*/, double /*duration*/) {}
void CsvPresentation::Update(double /*dt*/) {}

}  // namespace presentation
