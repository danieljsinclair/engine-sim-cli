// DemoKeyboardInputProvider.cpp - CLI-side keyboard dispatcher for demo mode
// Bridges CLI's KeyboardInput to bridge's IDemoControls interface

#include "DemoKeyboardInputProvider.h"
#include "input/IDemoControls.h"

namespace input {

DemoKeyboardInputProvider::DemoKeyboardInputProvider(
    std::unique_ptr<::KeyboardInput> keyboard,
    std::unique_ptr<IInputProvider> provider,
    IDemoControls* controls)
    : keyboard_(std::move(keyboard))
    , provider_(std::move(provider))
    , controls_(controls) {
}

bool DemoKeyboardInputProvider::Initialize() {
    if (!keyboard_) {
        lastError_ = "KeyboardInput is null";
        return false;
    }
    if (!provider_) {
        lastError_ = "IInputProvider is null";
        return false;
    }
    if (!controls_) {
        lastError_ = "IDemoControls is null";
        return false;
    }

    // The underlying provider should already be initialized
    return provider_->Initialize();
}

void DemoKeyboardInputProvider::Shutdown() {
    if (provider_) {
        provider_->Shutdown();
    }
    keyboard_.reset();
    provider_.reset();
    controls_ = nullptr;
}

bool DemoKeyboardInputProvider::IsConnected() const {
    return keyboard_ && provider_ && provider_->IsConnected();
}

EngineInput DemoKeyboardInputProvider::OnUpdateSimulation(double dt) {
    if (!keyboard_ || !provider_ || !controls_) {
        return EngineInput{};
    }

    // Read and dispatch keyboard input each frame
    int key = keyboard_->getKey();
    dispatchKey(key);

    // Delegate to the underlying provider
    EngineInput input = provider_->OnUpdateSimulation(dt);

    return input;
}

std::string DemoKeyboardInputProvider::GetProviderName() const {
    return "DemoKeyboardInputProvider";
}

std::string DemoKeyboardInputProvider::GetLastError() const {
    return lastError_;
}

void DemoKeyboardInputProvider::dispatchKey(int key) {
    if (!controls_ || key < 0) {
        return;
    }

    // Quit: q, Q, ESC
    if (key == 'q' || key == 'Q' || key == 27) {
        controls_->requestExit();
        return;
    }

    // Throttle: 1-9, 0
    double throttle = keyToThrottle(key);
    if (throttle >= 0.0) {
        controls_->setThrottle(throttle);
        return;
    }

    // Gear: ] = up, [ = down
    if (key == ']') {
        controls_->shiftUp();
        return;
    }
    if (key == '[') {
        controls_->shiftDown();
        return;
    }

    // Ignition: i, I
    if (key == 'i' || key == 'I') {
        controls_->toggleIgnition();
        return;
    }
}

double DemoKeyboardInputProvider::keyToThrottle(int key) {
    if (key >= '1' && key <= '9') {
        return static_cast<double>(key - '0') / 10.0;
    }
    if (key == '0') {
        return 1.0;
    }
    return -1.0;
}

} // namespace input