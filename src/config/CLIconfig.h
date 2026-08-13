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
    bool deterministic = false;  // --deterministic: headless fixed-timestep replay (gate/diagnosis)
    float holdThrottle = -1.0f;  // -1 sentinel; 0..1 holds throttle for non-interactive driving/diagnostics
    bool autoStart = false;      // --start: auto-crank the engine (implicit with --replay-telemetry)

    ReplayArgs replay;
    GearboxArgs gearbox;
    AudioTimingArgs audio;

    // Live telemetry: read decoded CSV from stdin (vehicle-sim --stdout-csv piped in),
    // one row per frame. Live and recorded replay share the same stdin CSV contract,
    // so the consumer cannot tell them apart. Implies --start (fires starter on frame 0).
    bool liveTelemetry = false;  // --live-telemetry

    // Live clutch wheel-coupling mode (--wheel-coupling): "pin" (default —
    // mirrors replay: the slip lock and the sim vehicle speed are pinned to the
    // CSV road speed; this is the road-driven path the road-test tunes against),
    // "free" (slip lock uses the actual simulated wheel speed; sim speed stays
    // independent so the mph-vs-target diagnostic is visible), or "torque".
    std::string wheelCoupling = "pin";

    // Coupling MODEL (--coupling-model): how the live clutch pressure is derived.
    // "torque-converter" (default — fluid-coupling pump/turbine + TR/K curves, the
    // chosen approach), "clutch-map" (declarative smooth governor curve; never
    // opens the clutch, so no bang-bang oscillation — fallback for comparison), or
    // "legacy" (historical slip-lock + binary creep-drag relief, the path that
    // oscillated, kept for A/B comparison).
    std::string couplingModel = "torque-converter";

    // Selective per-frame debug output (see DiagnosticOutputFilter). Each flag
    // unmutes one optional diagnostic line; all default off.
    presentation::DiagnosticOutputFilter diagnostics;  // populated by --diagnostic-frames / --diagnostic-freq

    // Machine-parseable CSV output alongside the console line. One row per frame
    // with all per-frame fields (timecode, rpm, gas, gear, clutch%, roadImplied,
    // relief, torques, state). Empty = no CSV. For automated smoke-tests /
    // lug-stall spelunking without grepping color-coded console text.
    std::string csvOut;
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
