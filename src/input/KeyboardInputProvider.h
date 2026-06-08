// KeyboardInputProvider.h - Keyboard input provider header

#ifndef KEYBOARD_INPUT_PROVIDER_H
#define KEYBOARD_INPUT_PROVIDER_H

#include "io/IInputProvider.h"
#include "IKeyboardInput.h"
#include "input/KeyHoldBridge.h"
#include "common/ILogging.h"
#include <memory>

class ISimulatorSession;

namespace input {

class KeyboardInputProvider : public IInputProvider {
public:
    explicit KeyboardInputProvider(ILogging* logger = nullptr, double initialDynoTorqueScale = -1.0);
    explicit KeyboardInputProvider(std::unique_ptr<IKeyboardInput> keyboard, ILogging* logger = nullptr);
    ~KeyboardInputProvider() override;

    bool Initialize() override;
    void Shutdown() override;
    bool IsConnected() const override;

    EngineInput OnUpdateSimulation(double dt) override;

    std::string GetProviderName() const override;
    std::string GetLastError() const override;

    /// Register the session for stop signalling. Q/Esc will call session->stop().
    void setSession(ISimulatorSession* session);

private:
    void processKeyState();

    // True on initial press OR OS key repeat (one-shot per event)
    bool keyActive(int key) const;

    std::unique_ptr<IKeyboardInput> keyboardInput_;
    KeyHoldBridge keyState_;

    double throttle_;
    double baselineThrottle_;
    bool ignition_;
    bool starterButton_;
    double dynoTorqueScale_;
    int gearDelta_;
    int gearSelector_;
    double brakeLevel_;
    std::string lastError_;
    bool presetCycle_{false};

    ISimulatorSession* session_{nullptr};

    std::unique_ptr<ConsoleLogger> defaultLogger_;
    ILogging* logger_;
};

} // namespace input

#endif // KEYBOARD_INPUT_PROVIDER_H
