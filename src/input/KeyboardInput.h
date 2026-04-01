// KeyboardInput.h - Non-blocking keyboard input class
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef KEYBOARD_INPUT_H
#define KEYBOARD_INPUT_H

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

class KeyboardInput {
public:
    KeyboardInput();
    ~KeyboardInput();

    // Get key press, returns -1 if no key pressed
    int getKey();

private:
#ifndef _WIN32
    termios oldSettings;
    bool initialized;
#endif
};

#endif // KEYBOARD_INPUT_H
