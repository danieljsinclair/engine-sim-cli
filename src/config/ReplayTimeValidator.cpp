// ReplayTimeValidator.cpp - Replay time-slice validation.

#include "ReplayTimeValidator.h"
#include "CLIconfig.h"
#include "CliException.h"
#include "input/IReplayTimeline.h"

#include <iostream>

// Validate replay time-slicing args against the actual trace duration.
// Throws CliException with a descriptive message if validation fails.
void validateReplayTimeSlicing(const CommandLineArgs& args,
                               input::IReplayTimeline* replay) {
    if (!replay) return;
    const double traceDur = replay->durationS();

    if (args.replay.startFromS >= 0.0 && traceDur > 0.0 && args.replay.startFromS >= traceDur) {
        std::cerr << "ERROR: --start-from " << args.replay.startFromS
                  << "s is past end of trace (" << traceDur << "s)\n";
        throw CliException("start-from beyond trace duration");
    }
    if (args.replay.endAtS >= 0.0 && traceDur > 0.0 && args.replay.endAtS > traceDur) {
        std::cerr << "WARNING: --end-at " << args.replay.endAtS
                  << "s is past end of trace (" << traceDur
                  << "s); will play to end\n";
        replay->setEndAtS(-1.0);
    }
    if (args.replay.startFromS >= 0.0 && args.replay.endAtS >= 0.0
        && args.replay.startFromS >= args.replay.endAtS) {
        std::cerr << "ERROR: --start-from (" << args.replay.startFromS
                  << "s) must be before --end-at (" << args.replay.endAtS << "s)\n";
        throw CliException("start-from >= end-at");
    }
}
