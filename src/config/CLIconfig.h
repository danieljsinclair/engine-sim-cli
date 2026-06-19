// CLIconfig.h - CLI argument parsing and global state
// Audio/simulation constants moved to bridge (AudioLoopConfig.h)

#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include <atomic>
#include <string>
#include "simulator/EngineSimTypes.h"
#include "io/IPresentation.h"  // DiagnosticOutputFilter

// Forward declarations
class SimulationConfig;

// ============================================================================
// Command Line Arguments
// ============================================================================

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
    bool autoGearbox = false;    // Automatic gearbox (--auto flag), default is manual
    bool manualGearbox = false;  // Explicit --manual flag (manual is already the default)
    std::string gearboxLogPath;  // Empty = no gearbox logging, path = enable CSV logging
    float crankingVolume = 0.0f; // 0-sentinel, resolved by bridge/SimulationConfig
    float holdThrottle = -1.0f;  // -1 sentinel; 0..1 holds throttle for non-interactive driving/diagnostics
    bool autoStart = false;      // --start: auto-crank the engine (implicit with --replay-telemetry)
    std::string replayTelemetryPath;  // --replay-telemetry <csv>: scripted driving from a telemetry CSV
    std::string liveTelemetryStream;  // --live-telemetry <->: live CSV from stdin ("-" or "stdin")
    std::string replayStartFrom;      // --start-from <time>: raw string, parsed after CLI
    std::string replayEndAt;          // --end-at <time>: raw string, parsed after CLI
    double replayStartFromS = -1.0;   // parsed seconds
    double replayEndAtS = -1.0;       // parsed seconds
    int simulationFrequency = 0; // Physics Hz — 0 means use EngineSimDefaults
    double synthLatency = 0.0;   // Synth latency seconds — 0 means use EngineSimDefaults
    int preFillMs = 0;           // Pre-fill buffer ms — 0 means use SimulationConfig default (50)

    // Selective per-frame debug output (see DiagnosticOutputFilter). Each flag
    // unmutes one optional diagnostic line; all default off.
    presentation::DiagnosticOutputFilter diagnostics;  // populated by --diagnostic-frames / --diagnostic-freq
};

// ============================================================================
// Global State (required for signal handling)
// ============================================================================

extern std::atomic<bool> g_interactiveMode;

// ============================================================================
// Function Declarations
// ============================================================================

void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], CommandLineArgs& args);
void ShowConfigHeader(const SimulationConfig& config, const char* engineAPIVersion);

// Parse a time string (plain seconds "30.5", mm:ss "1:30.5", or hh:mm:ss "0:01:30.5") into seconds.
// Returns -1.0 on invalid input.
double parseTimeStringToSeconds(const std::string& s);

#endif // CLI_CONFIG_H
