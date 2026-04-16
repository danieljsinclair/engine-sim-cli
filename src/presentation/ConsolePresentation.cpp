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

    // Throttle
    out << "[Throttle: " << std::setw(4) << static_cast<int>(state.throttle * 100) << "%] ";

    // Underruns
    out << "[Underruns: " << state.underrunCount << "] ";

    // Exhaust flow
    out << ANSIColors::INFO << "[Flow: " << std::fixed << std::showpos << std::setw(8)
        << std::setprecision(5) << state.exhaustFlow << std::noshowpos << " m3/s]"
        << ANSIColors::RESET << " ";

    // Audio timing diagnostics
    if (state.renderMs > 0.0) {
        std::string budgetColor = ANSIColors::getDispositionColour(state.budgetPct < 80, state.budgetPct < 100);
        out << "[" << state.audioMode << "]"
            << " req=" << std::setw(3) << state.framesRequested
            << " got=" << std::setw(3) << state.framesRendered
            << " rendered=" << std::setw(5) << std::fixed << std::setprecision(1) << state.renderMs << "ms"
            << " headroom=" << std::setw(5) << std::showpos << std::setprecision(1) << state.headroomMs
            << std::noshowpos << "ms"
            << " (" << budgetColor << std::setw(3) << std::setprecision(0)
            << state.budgetPct << "% of budget" << ANSIColors::RESET << ") ";
    }

    // Callback throughput metrics
    if (state.callbackRateHz > 0.0) {
        double neededKfps = state.sampleRate / 1000.0;
        double generatingKfps = state.generatingRateFps / 1000.0;

        // Colour coding: green if generating >= needed, yellow if >= 90%, red otherwise
        std::string genColor = ANSIColors::getDispositionColour(
            generatingKfps >= neededKfps, generatingKfps >= neededKfps * 0.9);

        // Trend: green if >= 0, yellow if < 0, red if < -1.0
        std::string trendColor = ANSIColors::getDispositionColour(
            state.trendPct >= 0.0, state.trendPct >= -1.0);

        out << "[callbacks=" << std::setw(4) << std::fixed << std::setprecision(0) << state.callbackRateHz << "Hz "
            << "needed=" << std::setw(5) << std::setprecision(1) << neededKfps << "kfps "
            << "generating=" << genColor << std::setw(5) << generatingKfps << "kfps" << ANSIColors::RESET << " "
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
