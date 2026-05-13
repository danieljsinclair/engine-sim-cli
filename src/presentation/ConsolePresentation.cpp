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
// Gear selector character lookup table — index is GearSelector value + 2 (offset PARK=-2 to 0)
char gearSelectorChar(int selector) {
    using GS = bridge::GearSelector;
    switch (static_cast<GS>(selector)) {
        case GS::PARK:    return 'P';
        case GS::REVERSE: return 'R';
        case GS::NEUTRAL: return 'N';
        case GS::DRIVE:   return 'D';
        default:
            // Values 1-8 = manual gear select
            if (selector >= static_cast<int>(GS::NEUTRAL) + 1 &&
                selector <= static_cast<int>(GS::NEUTRAL) + 8) {
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
    if (rpm < EngineSimDefaults::RPM_DISPLAY_FLOOR && state.rpm > 0) rpm = 0;
    out << "[" << std::setw(5) << rpm << " RPM] ";

    // Throttle
    out << "[Throttle: " << std::setw(4) << static_cast<int>(state.throttle * 100) << "%] ";

    // Gear: [Gear:XMG] format where X=selector, M/A=mode, G=gear number
    {
        char selectorChar = gearSelectorChar(state.gearSelector);
        char modeChar = state.gearAutoMode ? 'A' : 'M';
        int gearNum = state.gear; // BridgeGear: 0=neutral, 1-8=gears

        out << "[Gear:" << selectorChar << modeChar << gearNum << "] ";
    }

    // Road speed — displayed as whole-number mph (right-aligned 3-char field)
    {
        int mph = static_cast<int>(std::round(state.vehicleSpeedKmh * EngineSimDefaults::KMH_TO_MPH));
        out << "[" << std::setw(3) << mph << " mph] ";
    }

    // Engine torque and drivetrain torque: green=positive (power), red=negative (braking)
    {
        int engTorque = static_cast<int>(state.engineTorqueNm);
        int drvTorque = static_cast<int>(state.drivetrainTorqueNm);

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
            << " took=" << std::setw(5) << std::fixed << std::setprecision(1) << state.renderMs << "ms"
            << " room=" << std::setw(5) << std::showpos << std::setprecision(1) << state.headroomMs
            << std::noshowpos << "ms"
            << budgetColor << "budget: "  << std::setw(3) << std::setprecision(0) << state.budgetPct << "%" << ANSIColors::RESET << " ";
    }

    // Callback throughput metrics
    if (state.callbackRateHz > 0.0) {
        double neededKfps = state.sampleRate / 1000.0;
        double generatingKfps = state.generatingRateFps / 1000.0;

        std::string genColor = ANSIColors::getDispositionColour(
            generatingKfps >= neededKfps, generatingKfps >= neededKfps * 0.9);

        std::string trendColor = ANSIColors::getDispositionColour(
            state.trendPct >= 0.0, state.trendPct >= -1.0);

        out << "[calls=" << std::setw(4) << std::fixed << std::setprecision(0) << state.callbackRateHz << "Hz "
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
