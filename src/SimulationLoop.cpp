// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Refactored to use Strategy pattern for audio mode behavior

#include "SimulationLoop.h"

#include "AudioConfig.h"
#include "AudioPlayer.h"
#include "KeyboardInput.h"
#include "AudioSource.h"
#include "AudioMode.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <vector>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <algorithm>
#include <iomanip>
#include <filesystem>

// ============================================================================
// Private Helper Functions - SRP Compliance
// Each function does ONE thing
// ============================================================================

namespace {

// ----------------------------------------------------------------------------
// runUnifiedAudioLoop helpers - ONE thing each
// ----------------------------------------------------------------------------

KeyboardInput* initializeKeyboardInput(bool interactive) {
    if (!interactive) {
        return nullptr;
    }
    auto* keyboardInput = new KeyboardInput();
    std::cout << "\nInteractive mode enabled. Press Q to quit.\n";
    return keyboardInput;
}

void enableStarterMotor(EngineSimHandle handle, const EngineSimAPI& api) {
    api.SetStarterMotor(handle, 1);
}

bool checkStarterMotorRPM(EngineSimHandle handle, const EngineSimAPI& api, double minSustainedRPM) {
    EngineSimStats stats = {};
    api.GetStats(handle, &stats);
    if (stats.currentRPM > minSustainedRPM) {
        api.SetStarterMotor(handle, 0);
        return true;
    }
    return false;
}

void processKeyPress(EngineSimHandle handle, const EngineSimAPI& api, 
                     int key, int& lastKey, 
                     double& interactiveLoad, double& baselineLoad,
                     bool& wKeyPressed) {
    if (key < 0) {
        lastKey = -1;
        wKeyPressed = false;
        return;
    }
    
    if (key == lastKey) {
        return;
    }
    
    switch (key) {
        case 27: case 'q': case 'Q':
            g_running.store(false);
            break;
        case 'w': case 'W':
            wKeyPressed = true;
            interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
            baselineLoad = interactiveLoad;
            break;
        case ' ':
            interactiveLoad = 0.0;
            baselineLoad = 0.0;
            break;
        case 'r': case 'R':
            interactiveLoad = 0.2;
            baselineLoad = interactiveLoad;
            break;
        case 'a': {
            static bool ignitionState = true;
            ignitionState = !ignitionState;
            api.SetIgnition(handle, ignitionState ? 1 : 0);
            std::cout << "Ignition " << (ignitionState ? "enabled" : "disabled") << "\n";
            break;
        }
        case 's': {
            static bool starterState = false;
            starterState = !starterState;
            api.SetStarterMotor(handle, starterState ? 1 : 0);
            std::cout << "Starter motor " << (starterState ? "enabled" : "disabled") << "\n";
            break;
        }
        case 65:  // UP arrow (macOS)
            interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
            baselineLoad = interactiveLoad;
            break;
        case 66:  // DOWN arrow (macOS)
            interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
            baselineLoad = interactiveLoad;
            break;
        case 'k': case 'K':  // Alternative UP key
            interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
            baselineLoad = interactiveLoad;
            break;
        case 'j': case 'J':  // Alternative DOWN key
            interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
            baselineLoad = interactiveLoad;
            break;
    }
    lastKey = key;
}

void handleKeyboardInput(EngineSimHandle handle, const EngineSimAPI& api,
                         KeyboardInput* keyboardInput,
                         double& interactiveLoad, double& baselineLoad,
                         bool& wKeyPressed) {
    if (!keyboardInput) {
        return;
    }
    
    static int lastKey = -1;
    int key = keyboardInput->getKey();
    
    processKeyPress(handle, api, key, lastKey, interactiveLoad, baselineLoad, wKeyPressed);
    
    if (!wKeyPressed && interactiveLoad > baselineLoad) {
        interactiveLoad = std::max(baselineLoad, interactiveLoad * 0.5);
    }
}

double calculateThrottle(bool interactive, double currentTime, double interactiveLoad) {
    if (interactive) {
        return interactiveLoad;
    }
    return currentTime < 0.5 ? currentTime / 0.5 : 1.0;
}


int getUnderrunCount(AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return 0;
    }
    auto* context = audioPlayer->getContext();
    return context ? context->underrunCount.load() : 0;
}

bool shouldDisplayDiagnostics(std::chrono::steady_clock::time_point& lastDiagTime) {
    auto diagNow = std::chrono::steady_clock::now();
    if (std::chrono::duration<double>(diagNow - lastDiagTime).count() > 1.0) {
        lastDiagTime = diagNow;
        return true;
    }
    return false;
}

// Strategy pattern: Display diagnostics with mode from strategy
void displayDiagnostics(const EngineSimStats& stats, int underrunCount, 
                        AudioPlayer* audioPlayer, IAudioMode& audioMode) {
    std::cout << "\n[Diag] RPM=" << static_cast<int>(stats.currentRPM)
              << " underruns=" << underrunCount
              << " mode=" << audioMode.getModeString()
              << "\n";
}

void performTimingControl(TimingOps::LoopTimer& timer) {
    timer.sleepToMaintain60Hz();
}

bool shouldContinueLoop(bool interactive, double currentTime, double duration) {
    if (interactive) {
        return g_running.load();
    }
    return currentTime < duration;
}

// Update sine frequency in writer thread (SRP - moved from audio source)
// This ensures thread safety - frequency is updated in main loop, not audio thread
void updateSineFrequency(EngineSimHandle handle, const EngineSimAPI& api,
                          const EngineSimStats& stats, bool sineMode) {
    if (!sineMode) {
        return;
    }
    // Calculate frequency from RPM: (RPM / 600.0) * 100.0
    double frequency = (stats.currentRPM / 600.0) * 100.0;
    if (api.SetSineFrequency) {
        api.SetSineFrequency(handle, frequency);
    }
}

// ----------------------------------------------------------------------------
// runSimulation helpers - ONE thing each
// ----------------------------------------------------------------------------

EngineSimHandle createSimulator(const EngineSimConfig& config, EngineSimAPI& engineAPI) {
    EngineSimHandle handle = nullptr;
    EngineSimResult result = engineAPI.Create(&config, &handle);
    if (result != ESIM_SUCCESS || !handle) {
        std::cerr << "ERROR: Failed to create simulator\n";
        return nullptr;
    }
    return handle;
}

EngineSimConfig createDefaultConfig(int sampleRate) {
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;
    return config;
}

std::string determineConfigPath(const CommandLineArgs& args) {
    if (args.sineMode) {
        return "engine-sim-bridge/engine-sim/assets/main.mr";
    }
    if (args.useDefaultEngine) {
        return "engine-sim-bridge/engine-sim/assets/main.mr";
    }
    if (args.engineConfig) {
        return args.engineConfig;
    }
    return "";
}

bool validateConfigPath(const std::string& configPath, EngineSimHandle handle, 
                        EngineSimAPI& engineAPI) {
    if (configPath.empty()) {
        std::cerr << "ERROR: No engine configuration specified\n";
        std::cerr << "Use --script <config.mr> or --default-engine\n";
        engineAPI.Destroy(handle);
        return false;
    }
    return true;
}

bool resolveConfigPath(const std::string& configPath, 
                       std::string& resolvedPath, 
                       std::string& assetBasePath) {
    try {
        std::filesystem::path scriptPath(configPath);
        if (scriptPath.is_relative()) {
            scriptPath = std::filesystem::absolute(scriptPath);
        }
        scriptPath = scriptPath.lexically_normal();
        resolvedPath = scriptPath.string();
        
        assetBasePath = "engine-sim-bridge/engine-sim";
        
        if (scriptPath.has_parent_path()) {
            std::filesystem::path parentPath = scriptPath.parent_path();
            if (parentPath.filename() == "assets") {
                assetBasePath = parentPath.parent_path().string();
            } else {
                assetBasePath = parentPath.string();
            }
        }
        
        std::filesystem::path assetPath(assetBasePath);
        if (assetPath.is_relative()) {
            assetPath = std::filesystem::absolute(assetPath);
        }
        assetPath = assetPath.lexically_normal();
        assetBasePath = assetPath.string();
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ERROR: Failed to resolve path: " << e.what() << "\n";
        return false;
    }
    return true;
}

bool loadEngineScript(EngineSimHandle handle, EngineSimAPI& engineAPI,
                     const std::string& configPath, const std::string& assetBasePath) {
    EngineSimResult result = engineAPI.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load config: " << engineAPI.GetLastError(handle) << "\n";
        engineAPI.Destroy(handle);
        return false;
    }
    std::cout << "[Configuration loaded: " << configPath << "]\n";
    return true;
}

AudioPlayer* initializeAudioPlayer(int sampleRate, bool syncPull, bool silent,
                                   EngineSimHandle handle, EngineSimAPI* engineAPI) {
    auto* audioPlayer = new AudioPlayer();
    if (!audioPlayer->initialize(sampleRate, syncPull, handle, engineAPI, silent)) {
        std::cerr << "ERROR: Audio init failed\n";
        delete audioPlayer;
        return nullptr;
    }
    return audioPlayer;
}


void runWarmupPhase(EngineSimHandle handle, EngineSimAPI& engineAPI,
                   AudioPlayer* audioPlayer, bool drainDuringWarmup) {
    WarmupOps::runWarmup(handle, engineAPI, audioPlayer, drainDuringWarmup);
}

std::unique_ptr<IAudioSource> createAudioSource(EngineSimHandle handle, 
                                                EngineSimAPI& engineAPI, 
                                                bool sineMode, bool syncPull) {
    if (sineMode) {
        std::cout << "Mode: SINE TEST\n";
        return std::make_unique<SineAudioSource>(handle, engineAPI);
    }
    std::cout << "Mode: REAL ENGINE\n";
    return std::make_unique<EngineAudioSource>(handle, engineAPI);
}

void cleanupSimulation(AudioPlayer* audioPlayer, EngineSimHandle handle, 
                       EngineSimAPI& engineAPI, int exitCode) {
    if (audioPlayer) {
        audioPlayer->stop();
        audioPlayer->waitForCompletion();
        delete audioPlayer;
    }
    engineAPI.Destroy(handle);
    (void)exitCode;
}

void warnWavExportNotSupported(bool outputWavRequested) {
    if (outputWavRequested) {
        std::cout << "\nWARNING: WAV export not supported in unified mode\n";
        std::cout << "Use the old engine mode code path for WAV export.\n";
    }
}

} // anonymous namespace

// ============================================================================
// Unified Main Loop Implementation
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const CommandLineArgs& args,
    AudioPlayer* audioPlayer,
    IAudioMode& audioMode)
{
    double currentTime = 0.0;
    TimingOps::LoopTimer timer;
    
    const double minSustainedRPM = 550.0;
    double interactiveLoad = 0.1;
    double baselineLoad = interactiveLoad;
    bool wKeyPressed = false;
    
    // ONE thing: Initialize keyboard input
    KeyboardInput* keyboardInput = initializeKeyboardInput(args.interactive);
    
    // ONE thing: Enable starter motor
    enableStarterMotor(handle, api);
    
    std::cout << "\nStarting main loop...\n";
    
    auto lastDiagTime = std::chrono::steady_clock::now();
    
    // Main loop
    while (shouldContinueLoop(args.interactive, currentTime, args.duration)) {
        // ONE thing: Check starter motor RPM and disable when running
        checkStarterMotorRPM(handle, api, minSustainedRPM);
        
        // ONE thing: Handle keyboard input
        handleKeyboardInput(handle, api, keyboardInput, interactiveLoad, baselineLoad, wKeyPressed);
        
        // ONE thing: Calculate throttle
        double throttle = calculateThrottle(args.interactive, currentTime, interactiveLoad);
        api.SetThrottle(handle, throttle);
        
        // Strategy pattern: Update simulation based on audio mode
        audioMode.updateSimulation(handle, api, audioPlayer);
        
        // ONE thing: Get current stats
        EngineSimStats stats = {};
        api.GetStats(handle, &stats);
        
        // ONE thing: Update sine frequency (SRP - moved from audio source)
        updateSineFrequency(handle, api, stats, args.sineMode);
        
        // Strategy pattern: Generate audio based on audio mode
        audioMode.generateAudio(audioSource, audioPlayer);
        
        // ONE thing: Get underrun count
        int underrunCount = getUnderrunCount(audioPlayer);
        
        currentTime += AudioLoopConfig::UPDATE_INTERVAL;
        
        // ONE thing: Display diagnostics if needed
        if (shouldDisplayDiagnostics(lastDiagTime)) {
            displayDiagnostics(stats, underrunCount, audioPlayer, audioMode);
        }
        
        // ONE thing: Display progress
        audioSource.displayProgress(currentTime, args.duration, args.interactive, stats, throttle, underrunCount);
        
        // ONE thing: Maintain 60Hz timing
        performTimingControl(timer);
    }
    
    // ONE thing: Cleanup keyboard input
    if (keyboardInput) {
        delete keyboardInput;
    }
    
    return 0;
}

// ============================================================================
// Main Simulation Entry Point - UNIFIED for both modes
// ============================================================================

int runSimulation(const CommandLineArgs& args, EngineSimAPI& engineAPI) {
    const int sampleRate = AudioLoopConfig::SAMPLE_RATE;
    
    // Strategy pattern: Create audio mode based on syncPull flag
    std::unique_ptr<IAudioMode> audioMode = createAudioMode(args.syncPull);
    
    // ONE thing: Create default configuration
    EngineSimConfig config = createDefaultConfig(sampleRate);
    
    // ONE thing: Create simulator
    EngineSimHandle handle = createSimulator(config, engineAPI);
    if (!handle) {
        return 1;
    }
    
    // ONE thing: Determine config path
    std::string configPath = determineConfigPath(args);
    
    // ONE thing: Validate config path
    if (!validateConfigPath(configPath, handle, engineAPI)) {
        return 1;
    }
    
    // ONE thing: Resolve config path
    std::string assetBasePath;
    if (!resolveConfigPath(configPath, configPath, assetBasePath)) {
        engineAPI.Destroy(handle);
        return 1;
    }
    
    // ONE thing: Load engine script
    if (!loadEngineScript(handle, engineAPI, configPath, assetBasePath)) {
        return 1;
    }
    
    // ONE thing: Enable ignition
    engineAPI.SetIgnition(handle, 1);
    std::cout << "[Ignition enabled]\n";
    
    // ONE thing: Initialize audio player
    AudioPlayer* audioPlayer = nullptr;
    if (args.playAudio) {
        audioPlayer = initializeAudioPlayer(sampleRate, args.syncPull, args.silent,
                                            handle, &engineAPI);
        if (!audioPlayer) {
            engineAPI.Destroy(handle);
            return 1;
        }
    }
    
    // Strategy pattern: Start audio thread based on audio mode
    if (!audioMode->startAudioThread(handle, engineAPI, audioPlayer)) {
        delete audioPlayer;
        engineAPI.Destroy(handle);
        return 1;
    }
    
    // Strategy pattern: Prepare audio buffer based on audio mode
    audioMode->prepareBuffer(audioPlayer);
    
    // Strategy pattern: Check if drain is needed during warmup
    bool drainDuringWarmup = args.playAudio && audioPlayer && audioMode->shouldDrainDuringWarmup();
    runWarmupPhase(handle, engineAPI, audioPlayer, drainDuringWarmup);
    
    // Strategy pattern: Reset buffer after warmup based on audio mode
    audioMode->resetBufferAfterWarmup(audioPlayer);
    
    // Strategy pattern: Start playback based on audio mode
    audioMode->startPlayback(audioPlayer);
    
    // Enable sine mode in mock synth if in sine mode
    if (args.sineMode && engineAPI.SetSineMode) {
        engineAPI.SetSineMode(handle, 1);
    }
    
    // ONE thing: Create audio source
    std::unique_ptr<IAudioSource> audioSource = createAudioSource(handle, engineAPI, args.sineMode, args.syncPull);
    
    // Run unified loop - SAME CODE FOR BOTH MODES (uses strategy internally)
    int exitCode = runUnifiedAudioLoop(handle, engineAPI, *audioSource, args, audioPlayer, *audioMode);
    
    // ONE thing: Cleanup simulation resources
    cleanupSimulation(audioPlayer, handle, engineAPI, exitCode);
    
    // ONE thing: Warn about WAV export
    warnWavExportNotSupported(args.outputWav);
    
    return exitCode;
}
