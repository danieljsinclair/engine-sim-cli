// ConsolePresentation.cpp - Console text presentation implementation
// Implements IPresentation for text output to console
// SRP: Single responsibility - formats and outputs EngineState to console

#include "ConsolePresentation.h"

#include <iostream>
#include <iomanip>
#include <sstream>

namespace presentation {

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

void ConsolePresentation::ShowEngineState(const EngineState& state) {
    if (!config_.showDiagnostics) {
        return;
    }

    std::cout << formatEngineState(state) << "\n" << std::flush;
}

std::string ConsolePresentation::formatEngineState(const EngineState& state) const {
    std::ostringstream out;

    // RPM
    int rpm = static_cast<int>(state.rpm);
    if (rpm < 10 && state.rpm > 0) rpm = 0;
    out << "[" << std::setw(5) << rpm << " RPM] ";

    // Starter & Ignition — labels plain, digits colored
    auto boolColor = [](bool on) { return on ? ANSIColors::GREEN : ANSIColors::RED; };
    out << "[S:" << boolColor(state.starterMotorEngaged) << (state.starterMotorEngaged ? 1 : 0)
        << ANSIColors::RESET << " I:" << boolColor(state.ignition) << (state.ignition ? 1 : 0)
        << ANSIColors::RESET << "] ";

    // Preset short name (empty is fine — just a double space)
    out << state.presetShortName << " ";

    // Engine state and Throttle
    out <<  EnginePhaseName(state.enginePhase) <<  " [Gas: " << std::setw(3) << static_cast<int>(state.throttle * 100) << "%] ";

    // Gear
    out << "[Gear: " << state.gear << "] ";

    // Dyno load (shown when torque is being applied)
    if (state.dynoTorque > 0) {
        if (state.dynoTargetRPM > 0) {
            out << "[Dyno: " << static_cast<int>(state.dynoTargetRPM) << " RPM "
                << static_cast<int>(state.dynoTorque) << " ft*lbs] ";
        } else {
            out << "[Load: " << static_cast<int>(state.dynoTorque) << " ft*lbs] ";
        }
    }

    // Underruns
    out << "[UR: " << state.underrunCount << "] ";

    // Exhaust flow (cm³/s)
    out << ANSIColors::INFO << "[Flow: " << std::fixed << std::showpos << std::setw(8)
        << std::setprecision(3) << (state.exhaustFlow * 1000000.0) << std::noshowpos << " cm3/s]"
        << ANSIColors::RESET << " ";

    // Audio timing diagnostics
    if (state.renderMs > 0.0) {
        std::string budgetColor = ANSIColors::getDispositionColour(state.budgetPct < 80, state.budgetPct < 100);
        out << "[" << state.audioMode << "]"
            << " req=" << std::setw(3) << state.framesRequested
            << " got=" << std::setw(3) << state.framesRendered
            << " took=" << std::setw(5) << std::fixed << std::setprecision(1) << state.renderMs << "ms"
            << " room=" << std::setw(5) << std::showpos << std::setprecision(1) << state.headroomMs
            << std::noshowpos << "ms"
            << budgetColor << "budget: "  << std::setw(3) << std::setprecision(0) << state.budgetPct << "%" << ANSIColors::RESET << " ";
    }

    // Callback throughput metrics
    if (state.callbackRateHz > 0.0) {
        double neededKfps = state.sampleRate / 1000.0;
        double generatingKfps = state.generatingRateFps / 1000.0;

        // Simulation frequency: green <=10000, yellow <=15000, orange >15000
        std::string freqColor = state.simulationFrequency <= 10000 ? ANSIColors::GREEN
                              : state.simulationFrequency <= 15000 ? ANSIColors::YELLOW
                              : ANSIColors::WARNING;

        // Colour coding: green if generating >= needed, yellow if >= 90%, red otherwise
        std::string genColor = ANSIColors::getDispositionColour(
            generatingKfps >= neededKfps, generatingKfps >= neededKfps * 0.9);

        // Trend: green if >= 0, yellow if < 0, red if < -1.0
        std::string trendColor = ANSIColors::getDispositionColour(
            state.trendPct >= 0.0, state.trendPct >= -1.0);

        out << "[Freq=" << freqColor << state.simulationFrequency << ANSIColors::RESET
            << " calls=" << std::setw(4) << std::fixed << std::setprecision(0) << state.callbackRateHz << "Hz "
            << "need" << std::setw(5) << std::setprecision(1) << neededKfps << "kfps "
            << "actual=" << genColor << std::setw(5) << generatingKfps << "kfps" << ANSIColors::RESET << " "
            << "trend=" << trendColor << std::setw(5) << std::showpos << std::setprecision(1) << state.trendPct
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
