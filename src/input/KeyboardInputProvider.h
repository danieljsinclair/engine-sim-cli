// KeyboardInputProvider.h - Keyboard input provider header

#ifndef KEYBOARD_INPUT_PROVIDER_H
#define KEYBOARD_INPUT_PROVIDER_H

#include "io/IInputProvider.h"
#include "KeyboardInput.h"
#include "common/ILogging.h"
#include <memory>

namespace input {

class KeyboardInputProvider : public IInputProvider {
public:
    explicit KeyboardInputProvider(ILogging* logger = nullptr);
    ~KeyboardInputProvider() override;

    bool Initialize() override;
    void Shutdown() override;
    bool IsConnected() const override;

    EngineInput OnUpdateSimulation(double dt) override;

    std::string GetProviderName() const override;
    std::string GetLastError() const override;

private:
    void processKeyPress(int key);

    KeyboardInput* keyboardInput_;

    double throttle_;
    double baselineThrottle_;
    bool ignition_;
    bool starterSwitch_;
    int lastKey_;
    std::string lastError_;

    std::unique_ptr<ConsoleLogger> defaultLogger_;
    ILogging* logger_;
};

} // namespace input

#endif // KEYBOARD_INPUT_PROVIDER_H
