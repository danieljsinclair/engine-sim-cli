// ConsolePresentation.cpp - Console text presentation implementation
// Implements IPresentation for text output to console
// SRP: Single responsibility - formats and outputs EngineState to console

#include "ConsolePresentation.h"
#include "SteeringGauge.h"
#include "simulator/GearConventions.h"

#include <cmath>
#include <iostream>
#include <iomanip>
#include <sstream>

#include "../engine-sim-bridge/include/simulator/EngineSimTypes.h"

namespace presentation {

// Gear selector character lookup. Exposed via the header so tests verify the
// real production mapping rather than a duplicated copy.
char gearSelectorChar(int selector) {
    using GS = bridge::GearSelector;
    switch (static_cast<GS>(selector)) {
        case GS::PARK:    return 'P';
        case GS::REVERSE: return 'R';
        case GS::NEUTRAL: return 'N';
        case GS::DRIVE:   return 'D';
        default:
            // Manual gear-selection positions share the BridgeGear numbering
            // (FIRST=1 .. EIGHTH=8). DRIVE is 99, so these never collide.
            // All of 1-8 render as their digit; previously '1' fell through to '?'.
            if (selector >= 1 && selector <= 8) {
                return static_cast<char>('0' + selector);
            }
            return '?';
    }
}

// Third field: what the transmission is actually doing (P/R/N/1-8).
// PARK/REVERSE come from the selector (the physics has no reverse/park gear);
// NEUTRAL/DRIVE/forward reflect the physical gear number.
char gearChar(int selector, int physicalGear) {
    using GS = bridge::GearSelector;
    switch (static_cast<GS>(selector)) {
        case GS::PARK:    return 'P';   // transmission parked/locked
        case GS::REVERSE: return 'R';
        default: break;                 // NEUTRAL/DRIVE/manual -> physical gear
    }
    if (physicalGear == 0) return 'N';
    if (physicalGear >= 1 && physicalGear <= 8) return static_cast<char>('0' + physicalGear);
    return '?';
}

// "[selector][mode][gear]". Manual mirrors the selector for the gear field
// (selector == gear); auto derives it from the physical gear via gearChar.
std::string gearTriple(int selector, bool autoMode, int physicalGear) {
    const char field1 = gearSelectorChar(selector);
    const char field2 = autoMode ? 'A' : 'M';
    const char field3 = autoMode ? gearChar(selector, physicalGear) : gearSelectorChar(selector);
    return std::string(1, field1) + field2 + field3;
}

ConsolePresentation::ConsolePresentation(SteeringStyle style)
    : steeringGauge_(makeSteeringGauge(style)) {}

ConsolePresentation::~ConsolePresentation() {
    Shutdown();
}

bool ConsolePresentation::Initialize(const PresentationConfig& config) {
    config_ = config;
    lastDiagTime_ = std::chrono::steady_clock::now();
    initialized_ = true;
    return true;
}

void ConsolePresentation::Shutdown() {
    initialized_ = false;
}

void ConsolePresentation::ShowSimulatorStates(const EngineState& state) {
    if (!config_.showDiagnostics) {
        return;
    }

    std::cout << formatSimulatorState(state) << "\n" << std::flush;
}

std::string ConsolePresentation::formatSimulatorState(const EngineState& state) const {
    std::ostringstream out;

    formatReplayTimestamp(state, out);
    formatRPM(state, out);
    formatStarterState(state, out);
    formatNameState(state, out);
    formatSteeringState(state, out);
    formatPedalState(state, out);
    formatGearState(state, out);
    formatSpeedState(state, out);
    formatTargetSpeedState(state, out);
    formatTorqueState(state, out);
    formatDynoState(state, out);
    formatFlowState(state, out);
    formatAudioState(state, out);
    return out.str();
}

// Display absolute replay timestamp [mm:ss.ms] when replaying.
std::string ConsolePresentation::formatReplayTimestamp(const EngineState& state, std::ostringstream& out) const {
    if (state.drivetrain.replayTimestampS >= 0.0) {
        auto totalMs = static_cast<int>(state.drivetrain.replayTimestampS * 1000.0);
        auto hours = totalMs / 3600000;
        auto minutes = (totalMs % 3600000) / 60000;
        auto seconds = (totalMs % 60000) / 1000;
        auto ms = totalMs % 1000;
        if (hours > 0) {
            out << "[" << std::setw(2) << std::setfill('0') << hours << ":"
                << std::setw(2) << minutes << ":"
                << std::setw(2) << seconds << "."
                << std::setw(3) << ms << "] ";
        } else {
            out << "[" << std::setw(2) << std::setfill('0') << minutes << ":"
                << std::setw(2) << seconds << "."
                << std::setw(3) << ms << "] ";
        }
        out << std::setfill(' ');
    }
    return out.str();
}

std::string ConsolePresentation::formatRPM(const EngineState& state, std::ostringstream& out) const {
    // RPM
    auto rpm = static_cast<int>(state.engine.rpm);
    if (rpm < EngineSimDefaults::RPM_DISPLAY_FLOOR && state.engine.rpm > 0) rpm = 0;
    out << "[" << std::setw(5) << rpm << " RPM] ";
    return out.str();
}

std::string ConsolePresentation::formatStarterState(const EngineState& state, std::ostringstream& out) const {
    // Starter & Ignition — labels plain, digits colored
    auto boolColor = [](bool on) { return on ? ANSIColors::GREEN : ANSIColors::RED; };
    out << "[S:" << boolColor(state.engine.starterEngaged) << (state.engine.starterEngaged ? 1 : 0)
        << ANSIColors::RESET << " I:" << boolColor(state.controls.ignition) << (state.controls.ignition ? 1 : 0)
        << ANSIColors::RESET << "] ";
    return out.str();
}

std::string ConsolePresentation::formatNameState(const EngineState& state, std::ostringstream& out) const {

    // Preset short name (empty is fine — just a double space)
    out << state.presetShortName << " ";
    return out.str();
}

std::string ConsolePresentation::formatPedalState(const EngineState& state, std::ostringstream& out) const {

    // Engine phase and Throttle + Brake
    out << EnginePhaseName(state.engine.phase) << " [Gas: " << std::setw(3) << static_cast<int>(state.controls.throttle * 100) << "%";

    // Binary brake indicator: red 'B' when the vehicle brake is on (pedal
    // pressed — keyboard 'B' or a brake_light=1 CSV row, same signal),
    // plain '-' otherwise (off or unreported).
    if (state.controls.brakeLight.value_or(false)) {
        out << " " << ANSIColors::RED << "B" << ANSIColors::RESET;
    } else {
        out << " -";
    }

    out << "] ";
    return out.str();
}


std::string ConsolePresentation::formatGearState(const EngineState& state, std::ostringstream& out) const {
    // [Gear:XMG] where X=selector, M/A=mode, G=actual gear (transmission state).
    // Inline coupling-engagement readout, labeled by what the number IS:
    // `TC NN%` when the torque-converter model produced it (fluid coupling
    // engagement: 0% = decoupled, creep floor at standstill, 100% = coupled),
    // `Cl NN%` for the clutch-map/legacy friction-clutch pressure. Either way
    // the number is normalized coupling engagement — a slow-speed lug / stall
    // is visible at a glance (relief opening shows 0%, engaged slip 5-100%).
    out << "[Gear:"
        << gearTriple(state.controls.gearSelector, state.controls.gearAutoMode, state.drivetrain.gear)
        << "] ";
    if (state.drivetrain.clutchPressure >= 0.0) {
        const auto clutchColor = (state.drivetrain.clutchPressure <= 0.001)
            ? ANSIColors::GREEN    // relieved (open) — the engine idles decoupled
            : ANSIColors::RESET;
        out << clutchColor << '['
            << (state.drivetrain.couplingIsTorqueConverter ? "TC " : "Cl ")
            << std::setw(3) << static_cast<int>(std::round(state.drivetrain.clutchPressure * 100.0))
            << "%]" << ANSIColors::RESET << " ";
    }
    return out.str();
}

std::string ConsolePresentation::formatSpeedState(const EngineState& state, std::ostringstream& out) const {

    // Road speed — displayed as whole-number mph (right-aligned 3-char field)
    {
        auto mph = static_cast<int>(std::round(state.drivetrain.vehicleSpeedKmh * EngineSimDefaults::KMH_TO_MPH));
        out << "[" << std::setw(3) << mph << " mph] ";
    }
    return out.str();
}

// Steering wheel angle (signed, one decimal). Absent when the telemetry feed
// carries no steering (keyboard/demo/non-DBC sources) — the console degrades
// to nothing rather than printing a fake zero.
//
// v3 (owner design): the direction is the owner-authored SteeringGauge — a
// 12-position clock face of two-cell braille glyphs (0 deg = 12 o'clock,
// 90 deg = 3 o'clock full right; sector fences at 14/44/74/.../344 deg).
// Every glyph is the same two-cell width (braille blank U+2800 pads empty
// cells), so the component stays a constant-width block. v4 adds the
// selectable 8-way arrow variant (--steering-style arrows). Layout:
// "[<gauge> <angle>]" where the angle is right-justified to 6 chars (covers
// -359.0 .. 359.0) so the line never jitters.
std::string ConsolePresentation::formatSteeringState(const EngineState& state, std::ostringstream& out) const {
    if (state.controls.steeringAngleDeg.has_value()) {
        const double deg = *state.controls.steeringAngleDeg;

        // Save stream format state so we don't corrupt downstream components.
        std::ios_base::fmtflags savedFlags = out.flags();
        std::streamsize savedPrec = out.precision();

        // v4 (owner): steering-wheel icon commented out — the owner found it
        // over-bearing and wants to judge the gauge alone on a real trace.
        // To reinstate, uncomment this line (literal wheel U+1F6DE):
        // out << "🛞";
        out << '[' << steeringGauge_->getSteeringString(static_cast<int>(deg)) << ' '
            << std::fixed << std::setprecision(1) << std::setw(6) << std::right
            << deg << "] ";

        // Restore stream format state for the next component.
        out.flags(savedFlags);
        out.precision(savedPrec);
    }
    return out.str();
}

std::string ConsolePresentation::formatTargetSpeedState(const EngineState& state, std::ostringstream& out) const {

    // Commanded road-speed target (','/'.' keys). Visible even in neutral where
    // the engine isn't driven by road speed. Negative sentinel = not commanded.
    if (state.controls.commandedSpeedKmh >= 0.0) {
        auto tgtMph = static_cast<int>(std::round(state.controls.commandedSpeedKmh * EngineSimDefaults::KMH_TO_MPH));
        out << ANSIColors::INFO << "[Tgt: " << std::setw(3) << tgtMph << " mph]"
            << ANSIColors::RESET << " ";
    }
    return out.str();
}

std::string ConsolePresentation::formatTorqueState(const EngineState& state, std::ostringstream& out) const {

    // Engine torque and drivetrain torque: green=positive (power), red=negative (braking)
    {
        auto engTorque = static_cast<int>(state.engine.engineTorqueNm);
        auto drvTorque = static_cast<int>(state.engine.drivetrainTorqueNm);

        const std::string& engColor = (engTorque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;
        const std::string& drvColor = (drvTorque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;

        out << engColor << "[Eng: "
            << std::setw(3) << std::showpos << engTorque << "nm"
            << " <--> "
            << drvColor
            << std::setw(3) << drvTorque << "nm"
            << ": Drive]"
            << std::noshowpos << ANSIColors::RESET << " ";
    }
    return out.str();
}

std::string ConsolePresentation::formatDynoState(const EngineState& state, std::ostringstream& out) const {

    // Dyno load. Shown only when the dyno is actually applying torque: the
    // previous gate keyed on ENGINE torque, so every non-dyno run printed a
    // dead "[Load: 0 ft*lbs]". dynoTorque is Nm internally — convert for the
    // ft*lbs label.
    if (state.drivetrain.dynoTargetRPM > 0) {
        out << "[Dyno: " << static_cast<int>(state.drivetrain.dynoTargetRPM) << " RPM "
            << static_cast<int>(state.drivetrain.dynoTorque * 0.73756) << " ft*lbs] ";
    }
    else if (std::abs(state.drivetrain.dynoTorque) > 0.5) {
        out << "[Load: " << static_cast<int>(state.drivetrain.dynoTorque * 0.73756) << " ft*lbs] ";
    }
    return out.str();
}

std::string ConsolePresentation::formatFlowState(const EngineState& state, std::ostringstream& out) const {

    // Exhaust flow rate (cm³/s). exhaustFlow is a true m³/s rate: the frame's
    // port-transferred VOLUME (each transfer measured at its source side's
    // gas state) integrated over every substep, divided by the frame's
    // simulated duration. Positive = out the exhaust port, negative =
    // reversion (runner back into the cylinder — sustained negatives at part
    // throttle are a real model observation, not a readout artifact).
    out << ANSIColors::INFO << "[Flow: " << std::fixed << std::showpos << std::setw(10)
        << std::setprecision(0) << (state.engine.exhaustFlow * 1000000.0) << std::noshowpos << " cm3/s]"
        << ANSIColors::RESET << " ";

    // Synth output level: RMS of the last rendered audio block, POST-LEVELER
    // and PRE-VOLUME (the engine tone before the volume knob scales it), in
    // int16 output scale. This is the honest "what you would hear at volume
    // 1" quantity — a post-volume tap reads a flat zero on --silent benches
    // and would hide the level collapse this field exists to expose (the
    // quiet-WOT investigation). "-" when nothing has rendered yet.
    if (state.engine.synthOutputRms >= 0.0) {
        out << "[Out: " << std::fixed << std::setprecision(0) << std::setw(5)
            << state.engine.synthOutputRms << "]";
    }
    else {
        out << "[Out:     -]";
    }
    out << " ";

    return out.str();
}

std::string ConsolePresentation::formatAudioState(const EngineState& state, std::ostringstream& out) const {

    // Underruns
    out << "[UR: " << state.audio.underrunCount << "] ";

    // Audio mode label (e.g. [SYNC-PULL]) - always shown
    out << "[" << state.audio.audioMode << "]";

    // Detailed frame timing - only with --diagnostic-frames
    if (config_.diagnostics.frames && state.audio.renderMs > 0.0) {
        out << " req=" << std::setw(3) << state.audio.framesRequested
            << " got=" << std::setw(3) << state.audio.framesRendered
            << " took=" << std::setw(5) << std::fixed << std::setprecision(1) << state.audio.renderMs << "ms"
            << " room=" << std::setw(5) << std::showpos << std::setprecision(1) << state.audio.headroomMs
            << std::noshowpos << "ms";
    }

    // Budget — SYNC-PULL only. In THREADED mode the audio thread pulls work
    // at its own pace from a filled buffer, so the render-budget percentage
    // does not measure anything meaningful there; showing it misled more
    // than it informed (user-reported).
    if (state.audio.audioMode == "SYNC-PULL") {
        out << " " << ANSIColors::getDispositionColour(state.audio.budgetPct < 80, state.audio.budgetPct < 100)
            << "budget: " << std::fixed << std::setw(3) << std::setprecision(0) << state.audio.budgetPct << "%" << ANSIColors::RESET << " ";
    }

    // Throughput summary - only with --diagnostic-freq
    if (config_.diagnostics.freq) {
        double neededKfps = state.audio.sampleRate / 1000.0;
        double generatingKfps = state.audio.generatingRateFps / 1000.0;

        // Simulation frequency: green <=10000, yellow <=15000, orange >15000
        const std::string& freqColorHi = state.audio.simulationFrequency <= 15000 ? ANSIColors::YELLOW
                                                                      : ANSIColors::WARNING;
        const std::string& freqColor = state.audio.simulationFrequency <= 10000 ? ANSIColors::GREEN
                                                                      : freqColorHi;

        // Generating rate: green if >= needed, yellow if >= 90%, red otherwise
        std::string genColor = ANSIColors::getDispositionColour(
            generatingKfps >= neededKfps, generatingKfps >= neededKfps * 0.9);

        std::string trendColor = ANSIColors::getDispositionColour(
            state.audio.trendPct >= 0.0, state.audio.trendPct >= -1.0);

        out << "[Freq=" << freqColor << state.audio.simulationFrequency << ANSIColors::RESET
            << " actual=" << genColor << std::fixed << std::setw(5) << std::setprecision(1) << generatingKfps << "kfps" << ANSIColors::RESET << " "
            << "trend=" << trendColor << std::setw(5) << std::showpos << std::setprecision(1) << state.audio.trendPct
            << std::noshowpos << "%" << ANSIColors::RESET << "]";
    }

    return out.str();
}

void ConsolePresentation::ShowMessage(const std::string& message) {
    std::cout << message << "\n";
}

void ConsolePresentation::ShowError(const std::string& error) {
    std::cerr << "ERROR: " << error << "\n";
}

void ConsolePresentation::ShowProgress(double currentTime, double duration) {
    if (!config_.showProgress || !config_.interactive) {
        return;
    }

    if (duration > 0) {
        auto progress = static_cast<int>((currentTime / duration) * 50);
        std::cout << "\rProgress: [";
        for (int i = 0; i < 50; ++i) {
            std::cout << (i < progress ? '=' : ' ');
        }
        std::cout << "] " << static_cast<int>((currentTime / duration) * 100) << "%";
        std::cout << std::flush;
    }
}

void ConsolePresentation::Update(double dt) {
    (void)dt;
}

} // namespace presentation
