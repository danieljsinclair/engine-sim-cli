// KeyboardInputProvider.h - Keyboard input provider header

#ifndef KEYBOARD_INPUT_PROVIDER_H
#define KEYBOARD_INPUT_PROVIDER_H

#include "input/IInputProvider.h"
#include "KeyboardInput.h"

namespace input {

class KeyboardInputProvider : public IInputProvider {
public:
    KeyboardInputProvider();
    ~KeyboardInputProvider() override;
    
    bool Initialize() override;
    void Shutdown() override;
    bool IsConnected() const override;
    
    EngineInput GetEngineInput() const override;
    double GetThrottle() const override;
    bool GetIgnition() const override;
    bool GetStarterSwitch() const override;
    
    void Update(double dt) override;
    
    // Loop control - keyboard knows when to quit
    bool ShouldContinue() const override;
    
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
};

} // namespace input

#endif // KEYBOARD_INPUT_PROVIDER_H
