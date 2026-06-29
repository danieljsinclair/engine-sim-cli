// ConsolePresentation.cpp - Console text presentation implementation
// Implements IPresentation for text output to console
// SRP: Single responsibility - formats and outputs EngineState to console

#include "ConsolePresentation.h"
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

ConsolePresentation::ConsolePresentation()
{
}

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

    auto brakeColor = ANSIColors::getDispositionColour(state.controls.brakeLevel <= 0.0, false, state.controls.brakeLevel > 0.0);
    out << brakeColor << " B:" << std::fixed << std::setprecision(1) << state.controls.brakeLevel << ANSIColors::RESET;
    
    out << "] ";
    return out.str();
}


std::string ConsolePresentation::formatGearState(const EngineState& state, std::ostringstream& out) const {
    // [Gear:XMG] where X=selector, M/A=mode, G=actual gear (transmission state).
    out << "[Gear:"
        << gearTriple(state.controls.gearSelector, state.controls.gearAutoMode, state.drivetrain.gear)
        << "] ";
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

    // Dyno load (shown when torque is being applied)
    if (state.engine.engineTorqueNm > 0) {
        if (state.drivetrain.dynoTargetRPM > 0) {
            out << "[Dyno: " << static_cast<int>(state.drivetrain.dynoTargetRPM) << " RPM "
                << static_cast<int>(state.drivetrain.dynoTorque) << " ft*lbs] ";
        } else {
            out << "[Load: " << static_cast<int>(state.drivetrain.dynoTorque) << " ft*lbs] ";
        }
    }
    return out.str();
}

std::string ConsolePresentation::formatFlowState(const EngineState& state, std::ostringstream& out) const {

    // Exhaust flow (cm³/s)
    out << ANSIColors::INFO << "[Flow: " << std::fixed << std::showpos << std::setw(8)
        << std::setprecision(3) << (state.engine.exhaustFlow * 1000000.0) << std::noshowpos << " cm3/s]"
        << ANSIColors::RESET << " ";

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

    // Budget - always shown
    out << " " << ANSIColors::getDispositionColour(state.audio.budgetPct < 80, state.audio.budgetPct < 100)
        << "budget: " << std::fixed << std::setw(3) << std::setprecision(0) << state.audio.budgetPct << "%" << ANSIColors::RESET << " ";

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
