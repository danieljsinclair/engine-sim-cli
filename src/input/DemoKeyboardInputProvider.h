// DemoKeyboardInputProvider.h - CLI-side keyboard dispatcher for demo mode
// Bridges CLI's KeyboardInput to bridge's IDemoControls interface
// Eliminates RealKeyboardInputAdapter by handling key dispatch in the CLI

#ifndef DEMO_KEYBOARD_INPUT_PROVIDER_H
#define DEMO_KEYBOARD_INPUT_PROVIDER_H

#include "io/IInputProvider.h"
#include "input/KeyboardInput.h"
#include <memory>

namespace input {

class IDemoControls;

class DemoKeyboardInputProvider : public IInputProvider {
public:
    DemoKeyboardInputProvider(
        std::unique_ptr<::KeyboardInput> keyboard,
        std::unique_ptr<IInputProvider> provider,
        IDemoControls* controls);
    ~DemoKeyboardInputProvider() override = default;

    bool Initialize() override;
    void Shutdown() override;
    bool IsConnected() const override;
    EngineInput OnUpdateSimulation(double dt) override;
    std::string GetProviderName() const override;
    std::string GetLastError() const override;

private:
    void dispatchKey(int key);
    double keyToThrottle(int key);

    std::unique_ptr<::KeyboardInput> keyboard_;
    std::unique_ptr<IInputProvider> provider_;
    IDemoControls* controls_;  // Non-owning pointer to same object as provider_
    std::string lastError_;
};

} // namespace input

#endif // DEMO_KEYBOARD_INPUT_PROVIDER_H