// KeyboardInput.cpp - Non-blocking keyboard input implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#include "KeyboardInput.h"

#ifndef _WIN32
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
#endif

// ============================================================================
// Terminal Keyboard Input Implementation
// ============================================================================

#ifndef _WIN32
bool KeyboardInput::setupTerminal() {
    if (tcgetattr(STDIN_FILENO, &oldSettings) != 0) return false;
    termios newSettings = oldSettings;
    newSettings.c_lflag &= ~(ICANON | ECHO);
    newSettings.c_cc[VMIN] = 0;
    newSettings.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &newSettings) != 0) return false;

    // Set non-blocking
    int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1) return false;
    if (fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) != 0) return false;
    return true;
}
#endif

KeyboardInput::KeyboardInput()
#ifndef _WIN32
    : initialized{setupTerminal()}
#endif
{
}

KeyboardInput::~KeyboardInput() {
#ifndef _WIN32
    if (initialized) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
    }
#endif
}

int KeyboardInput::getKey() {
#ifdef _WIN32
    return _kbhit() ? _getch() : -1;
#else
    if (initialized) {
        char c;
        if (read(STDIN_FILENO, &c, 1) > 0) {
            return c;
        }
    }
    return -1;
#endif
}
