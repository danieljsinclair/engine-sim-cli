// IPresentation.h - Presentation interface
// Abstracts output: console text, TUI/TMUX, headless logging, etc.
// OCP: New presentation modes can be added without modifying existing code
// DI: Presentation is injected into simulation loop

#ifndef I_PRESENTATION_H
#define I_PRESENTATION_H

#include <string>
#include <memory>
#include <functional>

namespace presentation {

// ============================================================================
// EngineState - Data to display
// ============================================================================

struct EngineState {
    double timestamp;
    double rpm;
    double throttle;
    double load;
    double speed;
    int underrunCount;
    std::string audioMode;
    bool ignition;
    bool starterMotor;
    double exhaustFlow;  // m^3/s
};

// ============================================================================
// Presentation Configuration
// ============================================================================

struct PresentationConfig {
    bool interactive = false;
    double duration = 0.0;  // 0 = infinite
    int diagnosticIntervalMs = 1000;
    bool showProgress = true;
    bool showDiagnostics = true;
};

// ============================================================================
// IPresentation Interface
// Implemented by: ConsolePresentation, TUI Presentation, HeadlessLogger
// ============================================================================

class IPresentation {
public:
    virtual ~IPresentation() = default;
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    virtual bool Initialize(const PresentationConfig& config) = 0;
    virtual void Shutdown() = 0;
    
    // ========================================================================
    // Output Methods
    // ========================================================================
    
    virtual void ShowEngineState(const EngineState& state) = 0;
    virtual void ShowMessage(const std::string& message) = 0;
    virtual void ShowError(const std::string& error) = 0;
    virtual void ShowProgress(double currentTime, double duration) = 0;
    
    virtual void Update(double dt) = 0;
};

} // namespace presentation

#endif // I_PRESENTATION_H
