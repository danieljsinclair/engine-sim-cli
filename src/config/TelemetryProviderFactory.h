// TelemetryProviderFactory.h - SRP extraction from CLIMain.cpp (S3776).
//
// Builds the live/replay telemetry input provider from CommandLineArgs:
// construction + validated coupling/torque flag wiring + time-slice setters.
// Lifecycle (Initialize) and trace-duration-dependent slicing validation stay
// with the caller (CLIMain) — validation needs the parsed trace duration,
// which only exists after Initialize() opens the file. Pinned by
// test/unit/CLIMainS3776S1820Test.cpp Section B (SLIPLOCK_REFACTOR_EXPOSED).

#ifndef CLI_TELEMETRY_PROVIDER_FACTORY_H
#define CLI_TELEMETRY_PROVIDER_FACTORY_H

#include <memory>

#include "io/IInputProvider.h"

class CommandLineArgs;
class ILogging;

// Build the telemetry provider for --live-telemetry (LiveTelemetryProvider on
// std::cin) or --replay-telemetry (ReplayTelemetryProvider on the CSV path).
// Returns nullptr when neither mode is requested (the keyboard/demo path owns
// that construction). All coupling flags are validated here and fail fast
// (CliException) on a typo'd mode/model/tau — the twin reversion bug was
// downstream of a silent fallback, so none is ever silently corrected.
//
// Ordering contract (why the flags live here, before the caller runs
// Initialize): the providers store the coupling/torque configs and re-apply
// them when Initialize() creates the twin, so a pre-Initialize set is
// equivalent to the historical post-Initialize set (see the setters'
// store + re-apply contract on LiveTelemetryProvider.h).
std::unique_ptr<input::IInputProvider> buildTelemetryProvider(const CommandLineArgs& args);

#endif  // CLI_TELEMETRY_PROVIDER_FACTORY_H
