// DemoKeyboardInputProvider.cpp - CLI-side keyboard dispatcher for demo mode
// Bridges CLI's KeyboardInput to bridge's IDemoControls interface

#include "DemoKeyboardInputProvider.h"
#include "input/IDemoControls.h"

namespace input {

DemoKeyboardInputProvider::DemoKeyboardInputProvider(
    std::unique_ptr<IKeyboardInput> keyboard,
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

    // Drain all pending OS key events into key state tracker
    keyState_.drainInput([this]() { return keyboard_->getKey(); }, dt * 1000.0);

    // Dispatch based on key state
    processKeyState();

    return provider_->OnUpdateSimulation(dt);
}

std::string DemoKeyboardInputProvider::GetProviderName() const {
    return "DemoKeyboardInputProvider";
}

std::string DemoKeyboardInputProvider::GetLastError() const {
    return lastError_;
}

void DemoKeyboardInputProvider::processKeyState() {
    if (!controls_) return;

    // Quit: edge-triggered (pressed)
    if (keyState_.isKeyPressed('q') || keyState_.isKeyPressed('Q') || keyState_.isKeyPressed(27)) {
        controls_->requestExit();
        return;
    }

    // Throttle: level-triggered (down) — first matching number wins
    for (int k = '1'; k <= '9'; ++k) {
        if (keyState_.isKeyDown(k)) {
            controls_->setThrottle(keyToThrottle(k));
            return;
        }
    }
    if (keyState_.isKeyDown('0')) {
        controls_->setThrottle(1.0);
        return;
    }

    // Gear: edge-triggered (pressed)
    if (keyState_.isKeyPressed(']')) controls_->shiftUp();
    if (keyState_.isKeyPressed('[')) controls_->shiftDown();

    // Ignition: edge-triggered (pressed)
    if (keyState_.isKeyPressed('i') || keyState_.isKeyPressed('I')) {
        controls_->toggleIgnition();
    }

    // Brake: level-triggered (down) — KeyHoldBridge handles hold/timeout
    controls_->setBrake(keyState_.isKeyDown('b') ? 1.0 : 0.0);
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
