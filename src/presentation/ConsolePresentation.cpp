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

namespace {
// Gear selector character lookup table
char gearSelectorChar(int selector) {
    using GS = bridge::GearSelector;
    switch (static_cast<GS>(selector)) {
        case GS::PARK:    return 'P';
        case GS::REVERSE: return 'R';
        case GS::NEUTRAL: return 'N';
        case GS::DRIVE:   return 'D';
        default:
            // Values 2-8 = manual gear select (DRIVE=1, so manual starts at 2)
            if (selector >= 2 && selector <= 8) {
                return '0' + selector;
            }
            return '?';
    }
}
}

ConsolePresentation::ConsolePresentation()
    : initialized_(false)
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

std::string ConsolePresentation::formatRPM(const EngineState& state, std::ostringstream& out) const {
    // RPM
    int rpm = static_cast<int>(state.engine.rpm);
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
    // Gear: [Gear:XMG] format where X=selector, M/A=mode, G=gear number
    {
        char selectorChar = gearSelectorChar(state.controls.gearSelector);
        char modeChar = state.controls.gearAutoMode ? 'A' : 'M';
        int gearNum = state.drivetrain.gear; // BridgeGear: 0=neutral, 1-8=gears

        out << "[Gear:" << selectorChar << modeChar << gearNum << "] ";
    }
    return out.str();
}

std::string ConsolePresentation::formatSpeedState(const EngineState& state, std::ostringstream& out) const {

    // Road speed — displayed as whole-number mph (right-aligned 3-char field)
    {
        int mph = static_cast<int>(std::round(state.drivetrain.vehicleSpeedKmh * EngineSimDefaults::KMH_TO_MPH));
        out << "[" << std::setw(3) << mph << " mph] ";
    }
    return out.str();
}

std::string ConsolePresentation::formatTargetSpeedState(const EngineState& state, std::ostringstream& out) const {

    // Commanded road-speed target (','/'.' keys). Visible even in neutral where
    // the engine isn't driven by road speed. Negative sentinel = not commanded.
    if (state.controls.commandedSpeedKmh >= 0.0) {
        int tgtMph = static_cast<int>(std::round(state.controls.commandedSpeedKmh * EngineSimDefaults::KMH_TO_MPH));
        out << ANSIColors::INFO << "[Tgt: " << std::setw(3) << tgtMph << " mph]"
            << ANSIColors::RESET << " ";
    }
    return out.str();
}

std::string ConsolePresentation::formatTorqueState(const EngineState& state, std::ostringstream& out) const {

    // Engine torque and drivetrain torque: green=positive (power), red=negative (braking)
    {
        int engTorque = static_cast<int>(state.engine.engineTorqueNm);
        int drvTorque = static_cast<int>(state.engine.drivetrainTorqueNm);

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
        std::string freqColor = state.audio.simulationFrequency <= 10000 ? ANSIColors::GREEN
                              : state.audio.simulationFrequency <= 15000 ? ANSIColors::YELLOW
                              : ANSIColors::WARNING;

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
        int progress = static_cast<int>((currentTime / duration) * 50);
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
