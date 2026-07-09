// ReplayTimeValidator.h - Testable replay time-slice validation.
//
// Extracted from CLIMain.cpp's anonymous namespace so it can be unit-tested.
// The original was an internal-linkage free function (untestable in isolation);
// promoting it to external linkage against the IReplayTimeline interface lets
// the validator be driven directly from a test with a stand-in timeline.
//
// Behaviour is identical to the original anonymous-namespace implementation.

#ifndef CLI_REPLAY_TIME_VALIDATOR_H
#define CLI_REPLAY_TIME_VALIDATOR_H

class CommandLineArgs;
namespace input { class IReplayTimeline; }

// Validate replay time-slicing args (--start-from / --end-at) against the
// actual trace duration. Emits ERROR/WARNING diagnostics to stderr and throws
// CliException when the slice is unsatisfiable (start past end of trace, or
// start >= end). A start/end past the end of trace clamps to play-to-end.
// No-op when replay is nullptr.
void validateReplayTimeSlicing(const CommandLineArgs& args, input::IReplayTimeline* replay);

#endif  // CLI_REPLAY_TIME_VALIDATOR_H
