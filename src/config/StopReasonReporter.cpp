// StopReasonReporter.cpp - see header for rationale.

#include "config/StopReasonReporter.h"

#include <iostream>
#include <sstream>

#include "io/IInputProvider.h"
#include "simulation/SimulationLoop.h"  // SimulationConfig

std::string playbackStopMessage(bool interactive, double durationS,
                                bool endAtReached, double endAtS,
                                bool inputExhausted) {
    // Precedence follows what ACTUALLY ended the run:
    //   1. interactive  - the user quit.
    //   2. endAtReached - the provider's --end-at bound fired (only the code
    //                     path that genuinely crossed the bound sets the flag,
    //                     so this never claims a bound that did not stop the
    //                     run, even when --duration is also set: whichever is
    //                     smaller fires first and only it leaves its mark).
    //   3. duration     - the wall-clock/sim duration timer, but ONLY when the
    //                     provider is still connected. A disconnected provider
    //                     means the data ran out first (live stream EOF at
    //                     capture end) — reporting the duration would be the
    //                     owner-reported lie ("536.726s duration reached" on a
    //                     run that stopped much earlier).
    //   4. fallback     - trace end (unbounded streaming run).
    std::ostringstream out;
    if (interactive) {
        out << "\nPlayback stopped: user quit (Q or Ctrl-C).";
    } else if (endAtReached) {
        out << "\nPlayback stopped: --end-at " << endAtS << "s reached."
            << "\n  (the run stopped at the requested timecode)";
    } else if (durationS > 0.0 && !inputExhausted) {
        out << "\nPlayback stopped: " << durationS << "s duration reached."
            << "\n  (use --interactive for open-ended, --duration <N> for longer)";
    } else {
        out << "\nPlayback stopped: end of replay trace.";
    }
    return out.str();
}

void reportStopReason(const SimulationConfig& config,
                      const input::IInputProvider* provider, double endAtS) {
    const bool endAtReached = provider && provider->endAtReached();
    const bool inputExhausted = provider && !provider->IsConnected();
    std::cout << playbackStopMessage(config.interactive, config.duration,
                                     endAtReached, endAtS, inputExhausted)
              << std::endl;
}
