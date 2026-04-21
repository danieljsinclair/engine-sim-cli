// CLIconfig.h - CLI argument parsing and global state
// Audio/simulation constants moved to bridge (AudioLoopConfig.h)

#ifndef CLI_CONFIG_H
#define CLI_CONFIG_H

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <string>
#include "strategy/AudioLoopConfig.h"

// ============================================================================
// Command Line Arguments
// ============================================================================

struct CommandLineArgs {
    std::string engineConfig;
    std::string outputWav;
    double duration = 3.0;
    double targetRPM = 0.0;
    double targetLoad = -1.0;  // -1 means auto (RPM control)
    bool interactive = false;
    bool playAudio = false;
    bool useDefaultEngine = false;
    bool sineMode = false;  // Generate sine wave test tone instead of engine audio
    bool syncPull = true;  // Use sync pull model by default
    bool silent = false;   // Run full audio pipeline but with zero volume
    float crankingVolume = 1.0f;  // Cranking volume boost (applied when ignition ON, RPM < threshold, no exhaust flow)
    int simulationFrequency = EngineConstants::DEFAULT_SIMULATION_FREQUENCY;  // Physics Hz - lower for faster sync-pull
    int preFillMs = 50;  // Pre-fill buffer duration in ms for sync-pull mode
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
void ShowConfigHeader(CommandLineArgs& config, const char* engineAPIVersion);

#endif // CLI_CONFIG_H
