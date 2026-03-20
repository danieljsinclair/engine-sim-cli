// AudioConfig.h - Audio loop configuration structs
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance

#ifndef AUDIO_CONFIG_H
#define AUDIO_CONFIG_H

#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

#include <atomic>
#include <cstdint>
#include <cstddef>

// ============================================================================
// Shared Audio Loop Configuration
// ============================================================================

struct AudioLoopConfig {
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr double UPDATE_INTERVAL = 1.0 / 60.0;  // 60Hz
    static constexpr int FRAMES_PER_UPDATE = SAMPLE_RATE / 60;  // 735 frames
    static constexpr int WARMUP_ITERATIONS = 3;  // Minimal warmup
    static constexpr int PRE_FILL_ITERATIONS = 40;  // 0.67s initial buffer (matches working commit)
    static constexpr int RE_PRE_FILL_ITERATIONS = 0;  // No re-pre-fill (matches working commit)
};

struct EngineConstants {
    static constexpr int DEFAULT_SIMULATION_FREQUENCY = 10000;
    static constexpr int MIN_SIMULATION_FREQUENCY = 1000;
    static constexpr int MAX_SIMULATION_FREQUENCY = 100000;
};

// ============================================================================
// Command Line Arguments
// ============================================================================

struct CommandLineArgs {
    const char* engineConfig = nullptr;
    const char* outputWav = nullptr;
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
void displayHUD(double rpm, double throttle, double targetRPM, const EngineSimStats& stats, int underrunCount);
void ShowConfigHeader(CommandLineArgs& config, const char* engineAPIVersion);

#endif // AUDIO_CONFIG_H
