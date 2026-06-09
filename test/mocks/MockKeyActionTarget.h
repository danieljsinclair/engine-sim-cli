// MockKeyActionTarget.h - Mock IKeyActionTarget for testing KeyboardInputProvider
// Records all dispatched calls for behavioral verification.

#ifndef MOCK_KEY_ACTION_TARGET_H
#define MOCK_KEY_ACTION_TARGET_H

#include "input/IKeyActionTarget.h"
#include <string>
#include <vector>

class MockKeyActionTarget : public input::IKeyActionTarget {
public:
    std::vector<std::string> calls;
    double lastThrottleLevel = -1.0;
    double lastThrottleDelta = 0.0;
    double lastBrakeLevel = -1.0;
    double lastDynoDelta = 0.0;
    bool quitCalled = false;
    bool starterCalled = false;
    bool presetCalled = false;
    int shiftUpCount = 0;
    int shiftDownCount = 0;
    bool ignitionToggled = false;
    bool dynoReleased = false;

    void quit() override {
        quitCalled = true;
        calls.push_back("quit");
    }

    void setThrottle(double level) override {
        lastThrottleLevel = level;
        calls.push_back("setThrottle(" + std::to_string(level) + ")");
    }

    void adjustThrottle(double delta) override {
        lastThrottleDelta = delta;
        calls.push_back("adjustThrottle(" + std::to_string(delta) + ")");
    }

    void shiftUp() override {
        shiftUpCount++;
        calls.push_back("shiftUp");
    }

    void shiftDown() override {
        shiftDownCount++;
        calls.push_back("shiftDown");
    }

    void toggleIgnition() override {
        ignitionToggled = true;
        calls.push_back("toggleIgnition");
    }

    void setStarter() override {
        starterCalled = true;
        calls.push_back("setStarter");
    }

    void cyclePreset() override {
        presetCalled = true;
        calls.push_back("cyclePreset");
    }

    void adjustDynoTorque(double delta) override {
        lastDynoDelta = delta;
        calls.push_back("adjustDynoTorque(" + std::to_string(delta) + ")");
    }

    void releaseDynoTorque() override {
        dynoReleased = true;
        calls.push_back("releaseDynoTorque");
    }

    void setBrake(double level) override {
        lastBrakeLevel = level;
        calls.push_back("setBrake(" + std::to_string(level) + ")");
    }
};

#endif // MOCK_KEY_ACTION_TARGET_H
