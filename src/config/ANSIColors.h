#ifndef CONSOLE_COLORS_H
#define CONSOLE_COLORS_H

#include <string>
#include "simulator/engine_sim_bridge.h"

namespace ANSIColors {
    const std::string GREEN = "\x1b[32m";
    const std::string YELLOW = "\x1b[33m";
    const std::string RED = "\x1b[31m";
    const std::string CYAN = "\x1b[36m";
    const std::string RESET = "\x1b[0m";

    const std::string OK = GREEN;
    const std::string INFO = CYAN;
    const std::string WARNING = YELLOW;
    const std::string ERROR = RED;

    std::string infoMessage(const std::string& msg);
    std::string OKMessage(const std::string& type);
    std::string warningMessage(const std::string& msg);
    std::string errorMessage(const std::string& msg);

    std::string getDispositionColour(bool isGreen = false, bool isYellow = false, bool isRed = true);
}
#endif//CONSOLE_COLORS_H
