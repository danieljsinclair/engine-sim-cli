#include <string>
#include <sstream>
#include <iostream>

#include "ConsoleColors.h"
#include "engine_sim_bridge.h"


namespace ANSIColors {
    std::string colorMode(const std::string& mode) {
        if (mode == "threaded") {
            return GREEN + "[" + mode + "]" + RESET;
        }
        return YELLOW + "[" + mode + "]" + RESET;
    }
    
    std::string colorEngineType(const std::string& type) {
        if (type == "REAL ENGINE") {
            return GREEN + "[" + type + "]" + RESET;
        } else if (type == "SINE") {
            return CYAN + "[" + type + "]" + RESET;
        }
        return YELLOW + "[" + type + "]" + RESET;
    }
    
    std::string colorPreFill(const std::string& msg) {
        return CYAN + msg + RESET;
    }
    
    std::string colorWarning(const std::string& msg) {
        return YELLOW + msg + RESET;
    }
}

namespace DisplayHelper {
    void outputProgress(bool interactive, const std::string& prefix, 
        double currentTime, double duration, int progress, 
        const EngineSimStats& stats, double throttle, int underrunCount) {
        (void)stats;
        (void)throttle;
        (void)underrunCount;
        if (interactive) {
            std::cout << prefix << "\n" << std::flush;
        } else {
            static int lastProgress = 0;
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% | RPM: " << static_cast<int>(stats.currentRPM)
                          << " | Throttle: " << static_cast<int>(throttle * 100) << "%"
                          << " | Underruns: " << underrunCount << "\r" << std::flush;
                lastProgress = progress;
            }
        }
    }
}

