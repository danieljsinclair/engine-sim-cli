// KeyboardInputProvider.cpp - Keyboard input provider implementation
// Wraps existing KeyboardInput for IInputProvider interface

#include "KeyboardInputProvider.h"
#include "KeyboardInput.h"
#include "session/ISimulatorSession.h"

#include <algorithm>

namespace input {

KeyboardInputProvider::KeyboardInputProvider(ILogging* logger, double initialDynoTorqueScale)
    : keyboardInput_(nullptr)
    , throttle_(0.1)
    , baselineThrottle_(0.1)
    , ignition_(true)
    , starterButton_(false)
    , dynoTorqueScale_(initialDynoTorqueScale)
    , gearDelta_(0)
    , gearSelector_(0)
    , brakeLevel_(0.0)
    , defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
{
}

KeyboardInputProvider::KeyboardInputProvider(std::unique_ptr<IKeyboardInput> keyboard, ILogging* logger)
    : keyboardInput_(std::move(keyboard))
    , throttle_(0.1)
    , baselineThrottle_(0.1)
    , ignition_(true)
    , starterButton_(false)
    , dynoTorqueScale_(-1.0)
    , gearDelta_(0)
    , gearSelector_(0)
    , brakeLevel_(0.0)
    , defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
{
}

KeyboardInputProvider::~KeyboardInputProvider() {
    Shutdown();
}

bool KeyboardInputProvider::Initialize() {
    if (!keyboardInput_) {
        keyboardInput_ = std::make_unique<::KeyboardInput>();
    }
    logger_->info(LogMask::UI, "Interactive mode enabled. Press Q to quit.");
    return true;
}

void KeyboardInputProvider::Shutdown() {
    keyboardInput_.reset();
}

bool KeyboardInputProvider::IsConnected() const {
    return true;  // Keyboard is always connected in CLI
}

EngineInput KeyboardInputProvider::OnUpdateSimulation(double dt) {
    if (!keyboardInput_) {
        return EngineInput{};
    }

    // Drain all pending OS key events into key state tracker
    keyState_.drainInput([this]() { return keyboardInput_->getKey(); }, dt * 1000.0);

    // Dispatch based on key state
    processKeyState();

    // Decay throttle if W not pressed
    if (throttle_ > baselineThrottle_) {
        throttle_ = std::max(baselineThrottle_, throttle_ * 0.5);
    }

    EngineInput input;
    input.throttle = throttle_;
    input.ignition = ignition_;
    input.starterButton = starterButton_;
    input.dynoTorqueScale = dynoTorqueScale_;
    input.gearDelta = gearDelta_;

    input.gearSelector = gearSelector_;
    input.gearAutoMode = false;
    gearDelta_ = 0;  // Reset after consuming

    input.presetCycle = presetCycle_;
    presetCycle_ = false;
    starterButton_ = false;  // Momentary: reset after consuming

    input.brakeLevel = brakeLevel_;

    return input;
}

void KeyboardInputProvider::setSession(ISimulatorSession* session) {
    session_ = session;
}

bool KeyboardInputProvider::keyActive(int key) const {
    return keyState_.isKeyPressed(key) || keyState_.isKeyRepeating(key);
}

void KeyboardInputProvider::processKeyState() {
    // Quit: edge-triggered (pressed)
    if (keyState_.isKeyPressed('q') || keyState_.isKeyPressed('Q') || keyState_.isKeyPressed(27)) {
        if (session_) session_->stop();
        return;
    }

    // Throttle: edge-triggered (one-shot) for set-to-value keys
    if (keyState_.isKeyPressed(' ')) {
        throttle_ = 0.0;
        baselineThrottle_ = 0.0;
    }
    if (keyState_.isKeyPressed('r') || keyState_.isKeyPressed('R')) {
        throttle_ = 0.2;
        baselineThrottle_ = throttle_;
    }
    // Throttle ramp: responds to OS repeat (hold to gradually increase/decrease)
    if (keyActive('a') || keyActive('w') || keyActive('W') || keyActive(65)) {
        throttle_ = std::min(1.0, throttle_ + 0.05);
        baselineThrottle_ = throttle_;
    }
    if (keyActive('z') || keyActive('Z') || keyActive(66)) {
        throttle_ = std::max(0.0, throttle_ - 0.05);
        baselineThrottle_ = throttle_;
    }

    // Dyno Torque: responds to OS repeat (hold to gradually adjust)
    if (keyActive('e')) {
        dynoTorqueScale_ = std::max(0.0, dynoTorqueScale_ - 0.1);
        logger_->info(LogMask::UI, "Dyno torque: %.0f%%", dynoTorqueScale_ * 100.0);
    }
    if (keyActive('d')) {
        dynoTorqueScale_ = std::min(1.0, dynoTorqueScale_ + 0.1);
        logger_->info(LogMask::UI, "Dyno torque: %.0f%%", dynoTorqueScale_ * 100.0);
    }
    if (keyState_.isKeyPressed('c')) {
        dynoTorqueScale_ = 0.0;
        logger_->info(LogMask::UI, "Dyno torque: RELEASED (0%%)");
    }

    // Gear: edge-triggered (pressed)
    if (keyState_.isKeyPressed(']')) {
        gearDelta_ = 1;
        if (gearSelector_ < 8) gearSelector_++;
    }
    if (keyState_.isKeyPressed('[')) {
        gearDelta_ = -1;
        if (gearSelector_ > 0) gearSelector_--;
    }

    // Ignition: edge-triggered (pressed)
    if (keyState_.isKeyPressed('i') || keyState_.isKeyPressed('I')) {
        static bool ignitionState = true;
        ignitionState = !ignitionState;
        ignition_ = ignitionState;
    }

    // Starter: edge-triggered (pressed)
    if (keyState_.isKeyPressed('s') || keyState_.isKeyPressed('S')) {
        starterButton_ = true;
    }

    // Preset: edge-triggered (pressed)
    if (keyState_.isKeyPressed('p') || keyState_.isKeyPressed('P')) {
        presetCycle_ = true;
    }

    // Brake: level-triggered (down) — KeyHoldBridge handles hold/timeout
    brakeLevel_ = keyState_.isKeyDown('b') ? 1.0 : 0.0;
}

std::string KeyboardInputProvider::GetProviderName() const {
    return "Keyboard";
}

std::string KeyboardInputProvider::GetLastError() const {
    return lastError_;
}

} // namespace input
