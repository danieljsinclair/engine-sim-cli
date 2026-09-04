// CLIconfig.h - CLI argument parsing
// Audio/simulation constants moved to bridge (AudioLoopConfig.h)

#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include <string>
#include "simulator/EngineSimTypes.h"
#include "io/IPresentation.h"  // DiagnosticOutputFilter
#include "common/TimeParser.h"

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
    bool noBlankSkip = false;        // --no-blank-skip: anchor the arrival row exactly at the offset (default: skip blank USB-settle rows)
};

// Gearbox mode and logging (--auto / --manual / --gearbox-log).
struct GearboxArgs {
    bool automatic = false;          // Automatic gearbox. Default EXCEPT --replay-telemetry,
                                     // which resolves to auto in processArgs (PRND-only CSV).
    bool manual = false;             // Explicit --manual flag (opts replay out of the auto default)
    std::string logPath;             // Empty = no gearbox logging, path = enable CSV logging
};

// Audio/simulation timing overrides (0-sentinel: 0 means use EngineSimDefaults).
struct AudioTimingArgs {
    int simulationFrequency = 0;     // Physics Hz
    double synthLatency = 0.0;       // Synth latency seconds
    int preFillMs = 0;               // Pre-fill buffer ms — 0 means use SimulationConfig default (50)
    float crankingVolume = 0.0f;     // resolved by bridge/SimulationConfig

    // Output-stage span taming (see engine-sim include/span_tame.h for the
    // pinned parameterization). 0.0 (the default) = feature OFF = bit-identical
    // audio; the script-side audio_volume lever is exhausted (leveler
    // re-normalizes it), so span taming lives at the output stage. Forwards to
    // the bridge's ISimulatorConfig.spanTame.
    float spanTame = 0.0f;
};

// The vehicle-twin telemetry input subsystem: --live-telemetry plus the
// coupling/torque knobs that configure the twin the telemetry drives. Grouped
// so CommandLineArgs stays under the struct-field threshold (S1820) and the
// twin knobs travel together (SRP).
struct TwinArgs {
    // Live telemetry: read decoded CSV from stdin (vehicle-sim --stdout-csv
    // piped in), one row per frame. Live and recorded replay share the same
    // stdin CSV contract, so the consumer cannot tell them apart. Implies
    // --start (fires starter on frame 0).
    bool liveTelemetry = false;  // --live-telemetry

    // Live clutch wheel-coupling mode (--wheel-coupling): "pin" (default —
    // mirrors replay: the slip lock and the sim vehicle speed are pinned to the
    // CSV road speed; this is the road-driven path the road-test tunes against),
    // "free" (slip lock uses the actual simulated wheel speed; sim speed stays
    // independent so the mph-vs-target diagnostic is visible), or "torque".
    std::string wheelCoupling = "pin";

    // PIN-coupling compliance time constant in ms (--pin-tau-ms). The road
    // speed signal updates only ~5.5 Hz in ~0.9 km/h held steps, so the rigid
    // pin teleports engine rpm between levels (the audible "piano keys"). A
    // positive tau makes the pin CHASE the road-implied speed with a
    // critically-damped response. Default 150 ms = the owner-tuned road value;
    // the stable window is 60-1000 ms (see docs/architecture/pin-tau-compliance.md
    // in engine-sim-bridge). 0 (or negative) is EXACTLY the rigid pin,
    // bit-identical to the legacy behavior (the regression contract).
    double pinTauMs = 150.0;

    // Coupling MODEL (--coupling-model): how the live clutch pressure is derived.
    // "torque-converter" (default — fluid-coupling pump/turbine + TR/K curves, the
    // chosen approach), "clutch-map" (declarative smooth governor curve; never
    // opens the clutch, so no bang-bang oscillation — fallback for comparison), or
    // "legacy" (historical slip-lock + binary creep-drag relief, the path that
    // oscillated, kept for A/B comparison).
    std::string couplingModel = "torque-converter";

    // --effective-throttle (DEFAULT OFF): derive the twin's ENGINE-DRIVE throttle
    // from the commanded motor torque when Autopilot holds speed with the pedal
    // at rest (pedal=0.00 + torque>0 renders the engine silent today — 1,998
    // cruise rows at 95.6 kmh with +134 Nm in the 2026-08-30 capture). The blend
    // and hysteresis constants are pinned in the bridge's twin::EffectiveThrottleConfig.
    // Off must be byte-identical to today's output.
    bool effectiveThrottle = false;

    // --torque-informed-gearbox (DEFAULT OFF): feed the commanded motor torque
    // (sign + magnitude, UpstreamSignal::motorTorqueNm) into the gearbox shift
    // DECISION as a demand hint — pedal=0 AP pulls/brakes are misread as coasting
    // today. Decision input only, never physics. Off must be byte-identical.
    bool torqueInformedGearbox = false;
};

// Resolve the VehicleStartController crank delay (seconds) from the parsed
// start args. An EXPLICIT --cranking-delay converts ms->s verbatim (0 is a
// meaningful zero-delay combined start); only an ABSENT flag falls back to the
// controller default. Pure so the CLI conversion is unit-testable.
inline double resolveCrankDelayS(int crankingDelayMs, bool explicitMs, double defaultS) {
    return explicitMs
        ? static_cast<double>(crankingDelayMs) / 1000.0
        : defaultS;
}

// Presentation-layer knobs (--steering-style, --csv-out). Grouped so
// CommandLineArgs stays under the struct-field threshold (S1820).
struct PresentationArgs {
    // "arrows" (default — 8-way directional arrows, 45 deg sectors; owner
    // verdict 2026-09-02) or "braille" (12-position two-cell braille clock
    // face) when explicitly selected.
    std::string steeringStyle = "arrows";

    // Machine-parseable CSV output alongside the console line. Empty = no CSV.
    std::string csvOut;
};

struct CommandLineArgs {
    std::string engineConfig;
    std::string outputWav;
    double duration = 0.0;        // 0-sentinel, resolved by bridge/SimulationConfig
    double targetLoad = -1.0;     // -1 = no dyno, 0.0-1.0 = load torque fraction
    bool interactive = false;
    bool interactiveExplicit = false;  // true only when --interactive passed on CLI (vs defaulted because no --duration)
    bool playAudio = true;  // Audio playback is the default (only --deterministic suppresses it via null provider)
    bool connectDemo = false;      // Run VirtualICE twin demo with automatic gearbox
    bool sineMode = false;       // Generate sine wave test tone instead of engine audio
    bool syncPull = true;        // Use sync pull model by default
    bool deterministic = false;  // --deterministic: headless fixed-timestep replay (gate/diagnosis)
    float holdThrottle = -1.0f;  // -1 sentinel; 0..1 holds throttle for non-interactive driving/diagnostics

    ReplayArgs replay;
    GearboxArgs gearbox;
    AudioTimingArgs audio;

    // Output-shaping controls (--silent / --verbose). Grouped so
    // CommandLineArgs stays under the struct-field threshold (S1820),
    // mirroring StartArgs below.
    struct OutputArgs {
        bool silent = false;   // --silent: run full audio pipeline at zero volume
        bool verbose = false;  // --verbose: enable DEBUG-level logging (default output is INFO+)
    };
    OutputArgs output;

    // Start-control knobs (--start / --cranking-delay). Grouped so
    // CommandLineArgs stays under the struct-field threshold (S1820).
    struct StartArgs {
        bool autoStart = false;      // --start: auto-crank the engine (implicit with --replay-telemetry)
        int crankingDelayMs = 0;     // --cranking-delay <int-ms>: starter-then-ignition delay (McLaren mod). 0 = combined start
        // True only when --cranking-delay (or its --starter-delay alias)
        // appeared on the command line. The value 0 is MEANINGFUL (zero-delay
        // combined start) and must not fall back to the controller default —
        // only an ABSENT flag does.
        bool crankingDelayExplicit = false;
    };
    StartArgs start;

    // --live-telemetry + the twin coupling/torque knobs (see TwinArgs).
    TwinArgs twin;

    // Console presentation knobs (see PresentationArgs).
    PresentationArgs presentation;

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
// Returns -1.0 on invalid input. Shared bridge version — see common/TimeParser.h.
inline double parseReplayTimeToSeconds(const std::string& s) {
    return engine_sim_bridge::parseTimecodeToSeconds(s);
}

#endif // CLI_CONFIG_H
