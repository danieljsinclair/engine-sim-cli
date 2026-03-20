#ifndef CONSOLE_COLORS_H
#define CONSOLE_COLORS_H

namespace ANSIColors {
    const std::string GREEN = "\x1b[32m";
    const std::string YELLOW = "\x1b[33m";
    const std::string CYAN = "\x1b[36m";
    const std::string RESET = "\x1b[0m";
    
    std::string colorMode(const std::string& mode);
    std::string colorEngineType(const std::string& type);
    std::string colorPreFill(const std::string& msg);
    std::string colorWarning(const std::string& msg);
}

#endif//CONSOLE_COLORS_H
