// ConsolePresentation.h - Console presentation header

#ifndef CONSOLE_PRESENTATION_H
#define CONSOLE_PRESENTATION_H

#include "io/IPresentation.h"
#include "config/ANSIColors.h"
#include <chrono>

namespace presentation {

// Maps a gear-selector value to its single-character display glyph (field 1).
// PARK/REVERSE/NEUTRAL/DRIVE -> P/R/N/D; manual gear-selection digits 1-8
// render as that digit; any other value renders '?'.
// Exposed (free function) so tests verify the production mapping, not a copy.
char gearSelectorChar(int selector);

// Maps the actual engaged gear — the third display field — to its glyph.
// PARK selector -> 'P' (transmission locked); REVERSE selector -> 'R';
// otherwise the physical gear: 0 -> 'N', 1..8 -> digit, else '?'.
// The third field reads "what the transmission is doing": P/R/N/1-8.
char gearChar(int selector, int physicalGear);

// Composes the 3-character gear readout "[selector][mode][gear]" (no framing).
// field1 = selectorChar(selector); field2 = autoMode ? 'A' : 'M';
// field3 = autoMode ? gearChar(selector, physical) : selectorChar(selector)
//   (manual: selector == gear, so field-3 mirrors field-1).
// Pure and public so the composite is testable without friend hacks.
std::string gearTriple(int selector, bool autoMode, int physicalGear);

class ConsolePresentation final : public IPresentation {
public:
    ConsolePresentation();
    ~ConsolePresentation() override;

    // Manages console/output state. Copying has no meaningful semantics here and
    // is never done in practice (held via std::unique_ptr or as a test fixture
    // member), so copy is disabled to prevent accidental double-Shutdown.
    ConsolePresentation(const ConsolePresentation&) = delete;
    ConsolePresentation& operator=(const ConsolePresentation&) = delete;

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

    std::string formatReplayTimestamp(const EngineState& state, std::ostringstream& out) const;
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
    bool initialized_{false};
};

} // namespace presentation

#endif // CONSOLE_PRESENTATION_H
