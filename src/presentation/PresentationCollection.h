// PresentationCollection.h - A list-of-IPresentation dispatcher.
//
// OCP: a composite of N presentations (console + CSV + future ones) behind one
// IPresentation. Adding a presentation = add to the list, not a new field or a
// new primary/secondary pair. The simulation loop holds one IPresentation; this
// fans every call out to all children. Each child owns its own concern (console
// text, CSV rows, ...) — SRP: this type's only concern is fan-out dispatch.

#ifndef COMPOSITE_PRESENTATION_H
#define COMPOSITE_PRESENTATION_H

#include "io/IPresentation.h"
#include <memory>
#include <vector>

namespace presentation {

class PresentationCollection final : public IPresentation {
public:
    // Construct empty; append children with add(). Initialize() fans out once
    // all children are registered.
    PresentationCollection() = default;
    ~PresentationCollection() override;

    PresentationCollection(const PresentationCollection&) = delete;
    PresentationCollection& operator=(const PresentationCollection&) = delete;

    // Append a child presentation. Returns *this for fluent registration.
    // OCP: any number of presentations may be added; order = call order.
    PresentationCollection& add(std::unique_ptr<IPresentation> child);

    bool Initialize(const PresentationConfig& config) override;
    void Shutdown() override;
    void ShowSimulatorStates(const EngineState& state) override;
    void ShowMessage(const std::string& message) override;
    void ShowError(const std::string& error) override;
    void ShowProgress(double currentTime, double duration) override;
    void Update(double dt) override;

private:
    std::vector<std::unique_ptr<IPresentation>> children_;
};

}  // namespace presentation

#endif  // COMPOSITE_PRESENTATION_H
