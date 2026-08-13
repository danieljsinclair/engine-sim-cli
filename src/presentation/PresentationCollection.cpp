// PresentationCollection.cpp - list-of-IPresentation dispatcher. See header.

#include "presentation/PresentationCollection.h"
#include <utility>

namespace presentation {

PresentationCollection::~PresentationCollection() { Shutdown(); }

PresentationCollection& PresentationCollection::add(std::unique_ptr<IPresentation> child) {
    if (child) children_.push_back(std::move(child));
    return *this;
}

bool PresentationCollection::Initialize(const PresentationConfig& config) {
    // Fan out: ALL children must initialize. A single failure aborts the run
    // (fail-fast) — partial presentation is worse than none.
    for (auto& c : children_) {
        if (!c || !c->Initialize(config)) return false;
    }
    return !children_.empty();
}

void PresentationCollection::Shutdown() {
    // Reverse order: children shut down in LIFO (mirror construction order).
    for (auto it = children_.rbegin(); it != children_.rend(); ++it) {
        if (*it) (*it)->Shutdown();
    }
}

void PresentationCollection::ShowSimulatorStates(const EngineState& state) {
    for (auto& c : children_) if (c) c->ShowSimulatorStates(state);
}

void PresentationCollection::ShowMessage(const std::string& message) {
    for (auto& c : children_) if (c) c->ShowMessage(message);
}

void PresentationCollection::ShowError(const std::string& error) {
    for (auto& c : children_) if (c) c->ShowError(error);
}

void PresentationCollection::ShowProgress(double currentTime, double duration) {
    for (auto& c : children_) if (c) c->ShowProgress(currentTime, duration);
}

void PresentationCollection::Update(double dt) {
    for (auto& c : children_) if (c) c->Update(dt);
}

}  // namespace presentation
