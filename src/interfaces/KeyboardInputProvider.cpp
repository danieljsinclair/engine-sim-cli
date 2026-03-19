// KeyboardInputProvider.cpp - Keyboard input provider implementation
// Wraps existing KeyboardInput for IInputProvider interface

#include "interfaces/KeyboardInputProvider.h"

#include <iostream>
#include <algorithm>

extern std::atomic<bool> g_running;

namespace input {

KeyboardInputProvider::KeyboardInputProvider()
    : keyboardInput_(nullptr)
    , throttle_(0.1)
    , baselineThrottle_(0.1)
    , ignition_(true)
    , starterSwitch_(false)
    , lastKey_(-1)
{
}

KeyboardInputProvider::~KeyboardInputProvider() {
    Shutdown();
}

bool KeyboardInputProvider::Initialize() {
    keyboardInput_ = new KeyboardInput();
    std::cout << "\nInteractive mode enabled. Press Q to quit.\n";
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

EngineInput KeyboardInputProvider::GetEngineInput() const {
    EngineInput input;
    input.throttle = throttle_;
    input.ignition = ignition_;
    input.starterMotor = starterSwitch_;
    return input;
}

double KeyboardInputProvider::GetThrottle() const {
    return throttle_;
}

bool KeyboardInputProvider::GetIgnition() const {
    return ignition_;
}

bool KeyboardInputProvider::GetStarterSwitch() const {
    return starterSwitch_;
}

void KeyboardInputProvider::Update(double dt) {
    (void)dt;
    
    if (!keyboardInput_) {
        return;
    }
    
    int key = keyboardInput_->getKey();
    processKeyPress(key);
    
    // Decay throttle if W not pressed
    if (throttle_ > baselineThrottle_) {
        throttle_ = std::max(baselineThrottle_, throttle_ * 0.5);
    }
}

bool KeyboardInputProvider::ShouldContinue() const {
    return g_running.load();
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
        case 'w': case 'W':
            throttle_ = std::min(1.0, throttle_ + 0.05);
            baselineThrottle_ = throttle_;
            break;
        case ' ':
            throttle_ = 0.0;
            baselineThrottle_ = 0.0;
            break;
        case 'r': case 'R':
            throttle_ = 0.2;
            baselineThrottle_ = throttle_;
            break;
        case 'a': {
            static bool ignitionState = true;
            ignitionState = !ignitionState;
            ignition_ = ignitionState;
            std::cout << "Ignition " << (ignitionState ? "enabled" : "disabled") << "\n";
            break;
        }
        case 's': {
            static bool starterState = false;
            starterState = !starterState;
            starterSwitch_ = starterState;
            std::cout << "Starter motor " << (starterState ? "enabled" : "disabled") << "\n";
            break;
        }
        case 65:  // UP arrow (macOS)
            throttle_ = std::min(1.0, throttle_ + 0.05);
            baselineThrottle_ = throttle_;
            break;
        case 66:  // DOWN arrow (macOS)
            throttle_ = std::max(0.0, throttle_ - 0.05);
            baselineThrottle_ = throttle_;
            break;
        case 'k': case 'K':  // Alternative UP
            throttle_ = std::min(1.0, throttle_ + 0.05);
            baselineThrottle_ = throttle_;
            break;
        case 'j': case 'J':  // Alternative DOWN
            throttle_ = std::max(0.0, throttle_ - 0.05);
            baselineThrottle_ = throttle_;
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
