// ConsolePresentation.cpp - Console text presentation implementation
// Implements IPresentation for text output to console

#include "presentation/ConsolePresentation.h"

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
    // Diagnostics are handled by the simulation loop's inline displayProgress() which formats
    // the same data with proper SYNC-PULL timing info. Writing here would duplicate
    // output with inconsistent formatting (SRP: one owner for console diagnostics).
    (void)state;
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
