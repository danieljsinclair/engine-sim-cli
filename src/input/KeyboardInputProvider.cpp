// KeyboardInputProvider.cpp - Keyboard input provider implementation
// Wraps existing KeyboardInput for IInputProvider interface

#include "KeyboardInputProvider.h"

#include <algorithm>

extern std::atomic<bool> g_running;

namespace input {

KeyboardInputProvider::KeyboardInputProvider(ILogging* logger, double initialDynoTorqueScale)
    : keyboardInput_(nullptr)
    , throttle_(0.1)
    , baselineThrottle_(0.1)
    , ignition_(true)
    , starterSwitch_(false)
    , dynoTorqueScale_(initialDynoTorqueScale)
    , gearDelta_(0)
    , lastKey_(-1)
    , defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
{
}

KeyboardInputProvider::~KeyboardInputProvider() {
    Shutdown();
}

bool KeyboardInputProvider::Initialize() {
    keyboardInput_ = new KeyboardInput();
    logger_->info(LogMask::UI, "Interactive mode enabled. Press Q to quit.");
    return true;
}

void KeyboardInputProvider::Shutdown() {
    if (keyboardInput_) {
        delete keyboardInput_;
        keyboardInput_ = nullptr;
    }
}

bool KeyboardInputProvider::IsConnected() const {
    return true;  // Keyboard is always connected in CLI
}

EngineInput KeyboardInputProvider::OnUpdateSimulation(double dt) {
    (void)dt;

    if (!keyboardInput_ || !g_running.load()) {
        EngineInput input;
        input.shouldContinue = false;
        return input;
    }

    int key = keyboardInput_->getKey();
    processKeyPress(key);

    // Decay throttle if W not pressed
    if (throttle_ > baselineThrottle_) {
        throttle_ = std::max(baselineThrottle_, throttle_ * 0.5);
    }

    EngineInput input;
    input.throttle = throttle_;
    input.ignition = ignition_;
    input.starterSwitch = starterSwitch_;
    input.dynoTorqueScale = dynoTorqueScale_;
    input.gearDelta = gearDelta_;
    gearDelta_ = 0;  // Reset after consuming
    input.shouldContinue = true;
    return input;
}

void KeyboardInputProvider::processKeyPress(int key) {
    if (key < 0) {
        lastKey_ = -1;
        return;
    }

    if (key == lastKey_) {
        return;
    }

    switch (key) {
        case 27: case 'q': case 'Q':
            g_running.store(false);
            break;
        
        // ENGINE START
        case 'i': case 'I': {
            static bool ignitionState = true;
            ignitionState = !ignitionState;
            ignition_ = ignitionState;
            logger_->info(LogMask::UI, "Ignition %s", ignitionState ? "enabled" : "disabled");
            break;
        }
        case 's': case 'S': {
            static bool starterState = false;
            starterState = !starterState;
            starterSwitch_ = starterState;
            logger_->info(LogMask::UI, "Starter motor %s", starterState ? "enabled" : "disabled");
            break;
        }
        
        // THROTTLE CONTROL
        case ' ':
            throttle_ = 0.0;
            baselineThrottle_ = 0.0;
            break;
        case 'r': case 'R':
            throttle_ = 0.2;
            baselineThrottle_ = throttle_;
            break;

        case 'a': case 'w': case 'W': case 65:  // UP arrow (macOS)
            throttle_ = std::min(1.0, throttle_ + 0.05);
            baselineThrottle_ = throttle_;
            break;
        case 'z': case 'Z': case 66:  // DOWN arrow (macOS)
            throttle_ = std::max(0.0, throttle_ - 0.05);
            baselineThrottle_ = throttle_;
            break;

        // Dyno Torque Control
        case 'e':  // Decrease dyno torque (release traction control)
            dynoTorqueScale_ = std::max(0.0, dynoTorqueScale_ - 0.1);
            logger_->info(LogMask::UI, "Dyno torque: %.0f%%", dynoTorqueScale_ * 100.0);
            break;
        case 'd':  // Increase dyno torque (apply traction control)
            dynoTorqueScale_ = std::min(1.0, dynoTorqueScale_ + 0.1);
            logger_->info(LogMask::UI, "Dyno torque: %.0f%%", dynoTorqueScale_ * 100.0);
            break;
        case 'c':  // Full release (free-revving)
            dynoTorqueScale_ = 0.0;
            logger_->info(LogMask::UI, "Dyno torque: RELEASED (0%%)");
            break;

        // GEAR CONTROL
        case ']':  // Shift up
            gearDelta_ = 1;
            break;
        case '[':  // Shift down
            gearDelta_ = -1;
            break;
    }
    lastKey_ = key;
}

std::string KeyboardInputProvider::GetProviderName() const {
    return "Keyboard";
}

std::string KeyboardInputProvider::GetLastError() const {
    return lastError_;
}

} // namespace input
