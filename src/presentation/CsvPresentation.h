// CsvPresentation.h - Machine-parseable CSV output presentation.
//
// An IPresentation strategy that writes one CSV row per frame to a file,
// alongside the console line (the CLI composes console + csv via a composite).
// OCP: a new presentation mode — the loop and EngineState are unchanged. Every
// per-frame field (including the hidden ones: clutchPressure, roadImpliedRpm,
// creepReliefFired, both torques, state) is a column, so a lug/stall spelunks
// with every variable visible and the smoke-test assertions parse CSV, not
// color-coded console text.

#ifndef CSV_PRESENTATION_H
#define CSV_PRESENTATION_H

#include "io/IPresentation.h"
#include <fstream>
#include <string>

namespace presentation {

class CsvPresentation final : public IPresentation {
public:
    explicit CsvPresentation(std::string path);
    ~CsvPresentation() noexcept override;

    CsvPresentation(const CsvPresentation&) = delete;
    CsvPresentation& operator=(const CsvPresentation&) = delete;

    bool Initialize(const PresentationConfig& config) override;
    void Shutdown() override;

    void ShowSimulatorStates(const EngineState& state) override;
    void ShowMessage(const std::string& message) override;
    void ShowError(const std::string& error) override;
    void ShowProgress(double currentTime, double duration) override;
    void Update(double dt) override;

    // Suppress CSV row writes (e.g. during the warm-start prefix). The file
    // handle stays open (so the stream state is not disturbed); we only skip
    // the row write. Re-enabling resumes writing rows mid-stream.
    void setCsvEmissionEnabled(bool enabled) override;

private:
    std::string path_;
    std::ofstream out_;
    bool headerWritten_ = false;
    bool csvEmissionEnabled_ = true;  // toggled by setCsvEmissionEnabled()
};

} // namespace presentation

#endif // CSV_PRESENTATION_H
