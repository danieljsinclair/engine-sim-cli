#include <string>
#include <sstream>
#include <iostream>

#include "ANSIColors.h"
#include "simulator/EngineSimTypes.h"


namespace ANSIColors {
    
    std::string infoMessage(const std::string& msg) {
        return INFO + msg + RESET;
    }
    
    std::string OKMessage(const std::string& msg) {
        return GREEN + msg + RESET;
    }
    
    std::string warningMessage(const std::string& msg) {
        return YELLOW + msg + RESET;
    }
    std::string errorMessage(const std::string& msg) {
        return RED + msg + RESET;
    }

    // Template color function to avoid repetition
    // use a lambfunction to capture the logic for determining color based on thresholds
    std::string getDispositionColour(bool isGreen, bool isYellow, bool isRed) {
        if (isGreen) {
            return ANSIColors::GREEN;
        } else if (isYellow) {
            return ANSIColors::YELLOW;
        } else if (isRed) {
            return ANSIColors::RED;
        } else {
            return ANSIColors::RESET;
        }
    }

}
