// ConsolePresentation.cpp - Console text presentation implementation
// Implements IPresentation for text output to console

#include "interfaces/ConsolePresentation.h"

#include <iostream>
#include <iomanip>
#include <chrono>

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
    
    auto now = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(now - lastDiagTime_).count() > (config_.diagnosticIntervalMs / 1000.0)) {
        lastDiagTime_ = now;
        showDiagnostics(state);
    }
}

void ConsolePresentation::showDiagnostics(const EngineState& state) {
    std::cout << "\n[" << static_cast<int>(state.rpm) << " RPM]"
              << " [Throttle: " << static_cast<int>(state.throttle * 100) << "%]"
              << " [Flow: " << std::fixed << std::setprecision(2) << state.exhaustFlow << " m3/s]"
              << " [Underruns: " << state.underrunCount << "]";
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
