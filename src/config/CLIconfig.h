// CLIconfig.h - CLI argument parsing and global state
// Audio/simulation constants moved to bridge (AudioLoopConfig.h)

#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include "simulator/EngineSimTypes.h"

// Forward declarations
class SimulationConfig;

// ============================================================================
// Command Line Arguments
// ============================================================================

struct CommandLineArgs {
    std::string engineConfig;
    std::string outputWav;
    double duration = 0.0;        // 0-sentinel, resolved by bridge/SimulationConfig
    double targetRPM = 0.0;
    double targetLoad = -1.0;     // -1 means auto (RPM control)
    bool interactive = false;
    bool playAudio = false;
    bool useDefaultEngine = false;
    bool sineMode = false;       // Generate sine wave test tone instead of engine audio
    bool syncPull = true;        // Use sync pull model by default
    bool silent = false;         // Run full audio pipeline but with zero volume
    float crankingVolume = 0.0f; // 0-sentinel, resolved by bridge/SimulationConfig
    int simulationFrequency = 0; // Physics Hz — 0 means use EngineSimDefaults
    double synthLatency = 0.0;   // Synth latency seconds — 0 means use EngineSimDefaults
    int preFillMs = 0;           // Pre-fill buffer duration — 0 means use bridge default
};

// ============================================================================
// Global State (required for signal handling)
// ============================================================================

extern std::atomic<bool> g_running;
extern std::atomic<bool> g_interactiveMode;

// ============================================================================
// Function Declarations
// ============================================================================

void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], CommandLineArgs& args);
void ShowConfigHeader(const SimulationConfig& config, const char* engineAPIVersion);

#endif // CLI_CONFIG_H
