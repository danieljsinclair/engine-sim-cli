// ConsolePresentation.h - Console presentation header

#ifndef CONSOLE_PRESENTATION_H
#define CONSOLE_PRESENTATION_H

#include "io/IPresentation.h"
#include "config/ANSIColors.h"
#include <chrono>

namespace presentation {

class ConsolePresentation : public IPresentation {
public:
    ConsolePresentation();
    ~ConsolePresentation() override;
    
    bool Initialize(const PresentationConfig& config) override;
    void Shutdown() override;
    
    void ShowSimulatorStates(const EngineState& state) override;
    void ShowMessage(const std::string& message) override;
    void ShowError(const std::string& error) override;
    void ShowProgress(double currentTime, double duration) override;
    
    void Update(double dt) override;

private:
    void showDiagnostics(const EngineState& state);
    std::string formatSimulatorState(const EngineState& state) const;

    std::string formatRPM(const EngineState& state, std::ostringstream& out) const;
    std::string formatStarterState(const EngineState& state, std::ostringstream& out) const;
    std::string formatNameState(const EngineState& state, std::ostringstream& out) const;
    std::string formatPedalState(const EngineState& state, std::ostringstream& out) const;
    std::string formatGearState(const EngineState& state, std::ostringstream& out) const;
    std::string formatSpeedState(const EngineState& state, std::ostringstream& out) const;
    std::string formatTargetSpeedState(const EngineState& state, std::ostringstream& out) const;
    std::string formatTorqueState(const EngineState& state, std::ostringstream& out) const;
    std::string formatDynoState(const EngineState& state, std::ostringstream& out) const;
    std::string formatFlowState(const EngineState& state, std::ostringstream& out) const;
    std::string formatAudioState(const EngineState& state, std::ostringstream& out) const;

    PresentationConfig config_;
    std::chrono::steady_clock::time_point lastDiagTime_;
    bool initialized_;
};

} // namespace presentation

#endif // CONSOLE_PRESENTATION_H
