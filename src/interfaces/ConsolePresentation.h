// ConsolePresentation.h - Console presentation header

#ifndef CONSOLE_PRESENTATION_H
#define CONSOLE_PRESENTATION_H

#include "interfaces/IPresentation.h"
#include <chrono>

namespace presentation {

class ConsolePresentation : public IPresentation {
public:
    ConsolePresentation();
    ~ConsolePresentation() override;
    
    bool Initialize(const PresentationConfig& config) override;
    void Shutdown() override;
    
    void ShowEngineState(const EngineState& state) override;
    void ShowMessage(const std::string& message) override;
    void ShowError(const std::string& error) override;
    void ShowProgress(double currentTime, double duration) override;
    
    void Update(double dt) override;

private:
    void showDiagnostics(const EngineState& state);
    
    PresentationConfig config_;
    std::chrono::steady_clock::time_point lastDiagTime_;
    bool initialized_;
};

} // namespace presentation

#endif // CONSOLE_PRESENTATION_H
