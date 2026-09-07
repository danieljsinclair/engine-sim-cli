// CLIconfig.h - CLI argument parsing
// Audio/simulation constants moved to bridge (AudioLoopConfig.h)

#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include <string>
#include "simulator/EngineSimTypes.h"
#include "io/IPresentation.h"  // DiagnosticOutputFilter

// Forward declarations
class SimulationConfig;

// ============================================================================
// Command Line Arguments
// ============================================================================

// Replay telemetry CSV input (--replay-telemetry and its time-slicing args).
struct ReplayArgs {
    std::string telemetryPath;       // --replay-telemetry <csv>: scripted driving from a telemetry CSV
    std::string startFrom;           // --start-from <time>: raw string, parsed after CLI
    std::string endAt;               // --end-at <time>: raw string, parsed after CLI
    double startFromS = -1.0;        // parsed seconds
    double endAtS = -1.0;            // parsed seconds
};

// Gearbox mode and logging (--auto / --manual / --gearbox-log).
struct GearboxArgs {
    bool automatic = false;          // Automatic gearbox (--auto flag), default is manual
    bool manual = false;             // Explicit --manual flag (manual is already the default)
    std::string logPath;             // Empty = no gearbox logging, path = enable CSV logging
};

// Audio/simulation timing overrides (0-sentinel: 0 means use EngineSimDefaults).
struct AudioTimingArgs {
    int simulationFrequency = 0;     // Physics Hz
    double synthLatency = 0.0;       // Synth latency seconds
    int preFillMs = 0;               // Pre-fill buffer ms — 0 means use SimulationConfig default (50)
    float crankingVolume = 0.0f;     // resolved by bridge/SimulationConfig
};

struct CommandLineArgs {
    std::string engineConfig;
    std::string outputWav;
    double duration = 0.0;        // 0-sentinel, resolved by bridge/SimulationConfig
    double targetLoad = -1.0;     // -1 = no dyno, 0.0-1.0 = load torque fraction
    bool interactive = false;
    bool playAudio = false;
    bool connectDemo = false;      // Run VirtualICE twin demo with automatic gearbox
    bool sineMode = false;       // Generate sine wave test tone instead of engine audio
    bool syncPull = true;        // Use sync pull model by default
    bool silent = false;         // Run full audio pipeline but with zero volume
    float holdThrottle = -1.0f;  // -1 sentinel; 0..1 holds throttle for non-interactive driving/diagnostics
    bool autoStart = false;      // --start: auto-crank the engine (implicit with --replay-telemetry)

    ReplayArgs replay;
    GearboxArgs gearbox;
    AudioTimingArgs audio;

    // Selective per-frame debug output (see DiagnosticOutputFilter). Each flag
    // unmutes one optional diagnostic line; all default off.
    presentation::DiagnosticOutputFilter diagnostics;  // populated by --diagnostic-frames / --diagnostic-freq
};

// ============================================================================
// Function Declarations
// ============================================================================

void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], CommandLineArgs& args);
void ShowConfigHeader(const SimulationConfig& config, const char* engineAPIVersion);

// Parse a time string (plain seconds "30.5", mm:ss "1:30.5", or hh:mm:ss "0:01:30.5") into seconds.
// Returns -1.0 on invalid input.
double parseReplayTimeToSeconds(const std::string& s);

#endif // CLI_CONFIG_H
