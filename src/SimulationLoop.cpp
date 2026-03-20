// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Refactored to use Strategy pattern for audio mode behavior

#include "SimulationLoop.h"

#include "CLIConfig.h"
#include "AudioPlayer.h"
#include "AudioSource.h"
#include "audio/modes/IAudioMode.h"
#include "interfaces/IInputProvider.h"
#include "interfaces/IPresentation.h"
#include "EngineConfig.h"
#include "ConsoleColors.h"

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

void performTimingControl(TimingOps::LoopTimer& timer) {
    timer.sleepToMaintain60Hz();
}

bool shouldContinueLoop(double currentTime, double duration, input::IInputProvider* inputProvider) {
    // Check input provider first (knows about keyboard quit, upstream disconnect)
    if (inputProvider) {
        return inputProvider->ShouldContinue();
    }
    // For non-interactive, check duration
    if (inputProvider != nullptr) {
        return currentTime < duration;
    }
    return g_running.load();
}

double getThrottle(input::IInputProvider* inputProvider) {
    if (inputProvider) {
        inputProvider->Update(AudioLoopConfig::UPDATE_INTERVAL);
        return inputProvider->GetThrottle();
    }
    return 0.1;
}

double updateInputAndGetThrottle(input::IInputProvider* inputProvider, double currentTime) {
    if (inputProvider) {
        return getThrottle(inputProvider);
    }
    return currentTime < 0.5 ? currentTime / 0.5 : 1.0;
}



void updatePresentation(presentation::IPresentation* presentation, double currentTime,
                        const EngineSimStats& stats, double throttle, 
                        int underrunCount, IAudioMode& audioMode,
                        input::IInputProvider* inputProvider) {
    if (presentation) {
        presentation::EngineState state;
        state.timestamp = currentTime;
        state.rpm = stats.currentRPM;
        state.throttle = throttle;
        state.load = stats.currentLoad;
        state.speed = 0;
        state.underrunCount = underrunCount;
        state.audioMode = audioMode.getModeString();
        state.ignition = inputProvider ? inputProvider->GetIgnition() : true;
        state.starterMotor = false;
        state.exhaustFlow = stats.exhaustFlow;
        
        presentation->ShowEngineState(state);
    }
}

// Update sine frequency in writer thread
void updateSineFrequency(EngineSimHandle handle, const EngineSimAPI& api,
                          const EngineSimStats& stats, bool sineMode) {
    if (!sineMode) {
        return;
    }
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
        throw std::runtime_error("ERROR: Failed to create simulator\n");
    }
    return handle;
}

EngineSimConfig createDefaultConfig(int sampleRate, int simulationFrequency = EngineConstants::DEFAULT_SIMULATION_FREQUENCY) {
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = simulationFrequency;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;
    return config;
}

std::string determineConfigPath(const SimulationConfig& config) {
    if (config.sineMode || config.useDefaultEngine) {
        return "engine-sim-bridge/engine-sim/assets/main.mr";
    }
    return config.configPath;
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

bool loadEngineScriptInternal(EngineSimHandle handle, EngineSimAPI& engineAPI,
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


void runWarmupPhase(EngineSimHandle handle, EngineSimAPI& engineAPI,
                   AudioPlayer* audioPlayer, bool drainDuringWarmup) {
    WarmupOps::runWarmup(handle, engineAPI, audioPlayer, drainDuringWarmup);
}

std::unique_ptr<IAudioSource> createAudioSource(EngineSimHandle handle, 
                                                EngineSimAPI& engineAPI, 
                                                bool sineMode) {
    if (sineMode) {
        std::cout << "Engine: " << ANSIColors::colorEngineType("SINE") << " (simple bypass)\n";
        return std::make_unique<SineAudioSource>(handle, engineAPI);
    }
    std::cout << "Engine: " << ANSIColors::colorEngineType("REAL ENGINE") << "\n";
    auto source = std::make_unique<EngineAudioSource>(handle, engineAPI);
    source->setSyncPullMode(syncPull);
    return source;
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
        std::cout << "\n" << ANSIColors::colorWarning("WARNING:") << " WAV export not supported in unified mode\n";
        std::cout << "Use the old engine mode code path for WAV export.\n";
    }
}

// Initialize AudioPlayer with the simulator handle using Factory + DI pattern
// This is done in runSimulation to ensure the simulator is properly set up first
AudioPlayer* createAndInitializeAudioPlayer(int sampleRate, EngineSimHandle handle, const EngineSimAPI* engineAPI, bool syncPull) {
    auto* audioPlayer = new AudioPlayer(nullptr);
    
    // Use factory to create the appropriate audio mode (DI pattern)
    auto factory = createAudioModeFactory(engineAPI, syncPull);
    bool initSuccess = audioPlayer->initialize(*factory, sampleRate, handle, engineAPI);
    factory.release();  // AudioPlayer doesn't own the factory, but we created it here
    
    if (!initSuccess) {
        std::cerr << "ERROR: Audio init failed\n";
        delete audioPlayer;
        return nullptr;
    }
    return audioPlayer;
}

} // anonymous namespace

// ============================================================================
// Config Path Loading - Single function (Feedback #3)
// ============================================================================

EngineLoaderResult loadEngineScript(const SimulationConfig& config) {
    EngineLoaderResult result;
    
    result.configPath = determineConfigPath(config);
    
    if (result.configPath.empty()) {
        std::cerr << "ERROR: No engine configuration specified\n";
        std::cerr << "Use --script <config.mr> or --default-engine\n";
        return result;
    }
    
    if (!resolveConfigPath(result.configPath, result.configPath, result.assetBasePath)) {
        return result;
    }
    
    result.success = true;
    return result;
}


// ============================================================================
// Unified Main Loop Implementation
// Now uses IInputProvider and IPresentation for SRP compliance
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const SimulationConfig& config,
    AudioPlayer* audioPlayer,
    IAudioMode& audioMode,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation)
{
    double currentTime = 0.0;
    TimingOps::LoopTimer timer;
    
    const double minSustainedRPM = 550.0;
    double interactiveLoad = 0.1;
    double baselineLoad = interactiveLoad;
    bool wKeyPressed = false;
    
    // Initialize - enable starter motor (simulator logic)
    enableStarterMotor(handle, api);
    double throttle = getThrottle(inputProvider);
    
    std::cout << "\nStarting main loop...\n";
    
    // Main loop
    while (shouldContinueLoop(currentTime, config.duration, inputProvider)) {
        checkStarterMotorRPM(handle, api, minSustainedRPM);
        
        throttle = updateInputAndGetThrottle(inputProvider, currentTime);
        api.SetThrottle(handle, throttle);

        bool ignition = inputProvider ? inputProvider->GetIgnition() : true;
        api.SetIgnition(handle, ignition ? 1 : 0);
        
        audioMode.updateSimulation(handle, api, audioPlayer);
        

        EngineSimStats stats = {};
        api.GetStats(handle, &stats);
        updateSineFrequency(handle, api, stats, config.sineMode);
        
        // Update audio source with latest stats (needed for threaded sine mode)
        audioSource.updateStats(stats);
        audioMode.generateAudio(audioSource, audioPlayer);
        
        int underrunCount = getUnderrunCount(audioPlayer);
        
        currentTime += AudioLoopConfig::UPDATE_INTERVAL;
        
        // Show audio source specific output (Frequency for sine, Flow for engine)
        audioSource.displayProgress(currentTime, config.duration, config.interactive, stats, throttle, underrunCount);
        
        // Maintain 60hz timing with sleeps :(
        performTimingControl(timer);
    }
    
    return 0;
}

AudioPlayer *InitAudioPlayback(int sampleRate, EngineSimHandle handle, EngineSimAPI& engineAPI, bool syncPull) {
    // This must happen AFTER the simulator is created and configured
    AudioPlayer *audioPlayer = createAndInitializeAudioPlayer(sampleRate, handle, &engineAPI, syncPull);
    if (!audioPlayer) {
        throw std::runtime_error("Failed to initialize audio player");
    }
    return audioPlayer;
}

void StartAudioMode(IAudioMode* audioMode, EngineSimHandle handle, EngineSimAPI& engineAPI, AudioPlayer* audioPlayer) {
    if (!audioMode) {
        if (audioPlayer) {
            delete audioPlayer;
        }
        engineAPI.Destroy(handle);
        throw std::runtime_error("ERROR: audioMode must be injected\n");
    }

    // Strategy pattern: Start audio thread based on audio mode
    if (!audioMode->startAudioThread(handle, engineAPI, audioPlayer)) {
        if (audioPlayer) {
            delete audioPlayer;
        }
        engineAPI.Destroy(handle);
        throw std::runtime_error("ERROR: Failed to start audio thread\n");
    }
}

// ============================================================================
// Main Simulation Entry Point - UNIFIED for both modes
// Now uses injectable IInputProvider and IPresentation
// ============================================================================

int runSimulation(
    const SimulationConfig& config,
    EngineSimAPI& engineAPI,
    IAudioMode* audioMode,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation) {
    const int sampleRate = AudioLoopConfig::SAMPLE_RATE;
    
    // Create simulator - single simulator for both audio and main simulation
    EngineSimConfig engineConfig = createDefaultConfig(sampleRate, config.simulationFrequency);
    EngineSimHandle handle = createSimulator(engineConfig, engineAPI);
    if (!loadEngineScriptInternal(handle, engineAPI, config.configPath, config.assetBasePath)) {
        return 1;
    }
    
    // Initialize Audio framework and playback if requested
    AudioPlayer* audioPlayer = InitAudioPlayback(audioMode, sampleRate, handle, engineAPI);
    audioPlayer->setVolume(config.volume);
    StartAudioMode(audioMode, handle, engineAPI, audioPlayer);
    audioMode->prepareBuffer(audioPlayer);
    
    // Check if drain is needed during warmup
    bool drainDuringWarmup = config.playAudio && audioPlayer && audioMode->shouldDrainDuringWarmup();
    runWarmupPhase(handle, engineAPI, audioPlayer, drainDuringWarmup);
    
    // Reset buffer after warmup based on audio mode
    audioMode->resetBufferAfterWarmup(audioPlayer);
    
    // Start playback based on audio mode
    audioMode->startPlayback(audioPlayer);
    std::unique_ptr<IAudioSource> audioSource = createAudioSource(handle, engineAPI, config.sineMode);
    
    // Run MAIN loop - now uses injectable input provider and presentation
    // Some input providers enable ignition by default which will allow the engine to fire during the cranking phase otherwise it will crank endlessly huffing and puffing
    int exitCode = runUnifiedAudioLoop(handle, engineAPI, *audioSource, config, audioPlayer, *audioMode, inputProvider, presentation);
    
    // EXIT
    cleanupSimulation(audioPlayer, handle, engineAPI, exitCode);
    
    // If WAV export was requested, warn that it's not supported in unified mode (since it requires a different audio thread setup)
    warnWavExportNotSupported(config.outputWav);
    
    return exitCode;
}
