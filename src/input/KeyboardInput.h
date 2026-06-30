// KeyboardInput.h - Non-blocking keyboard input class
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef KEYBOARD_INPUT_H
#define KEYBOARD_INPUT_H

#include "input/IKeyboardInput.h"

#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// ============================================================================
// Terminal Keyboard Input (Non-blocking)
// ============================================================================

class KeyboardInput : public IKeyboardInput {
public:
    KeyboardInput();
    ~KeyboardInput() override;

    // Owns process-global terminal state (termios). Copying would restore the
    // saved settings twice (double-cleanup), so copy is disabled. The class is
    // never copied or moved in practice (held via std::unique_ptr).
    KeyboardInput(const KeyboardInput&) = delete;
    KeyboardInput& operator=(const KeyboardInput&) = delete;

    // Get key press, returns -1 if no key pressed
    int getKey() override;

private:
#ifndef _WIN32
    termios oldSettings{};
    bool initialized{false};
#endif
};

#endif // KEYBOARD_INPUT_H
