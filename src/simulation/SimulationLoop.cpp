// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Refactored to use Strategy pattern for audio mode behavior

#include "SimulationLoop.h"

#include "config/CLIconfig.h"
#include "AudioPlayer.h"
#include "AudioSource.h"
#include "audio/renderers/IAudioRenderer.h"
#include "input/IInputProvider.h"
#include "presentation/IPresentation.h"
#include "simulation/EngineConfig.h"
#include "config/ANSIColors.h"
#include "bridge/engine_sim_loader.h"
#include "ILogging.h"
#include "ITelemetryProvider.h"

#include <cstring>
#include <atomic>
#include <csignal>

// ============================================================================
// SimulationConfig Implementation
// ============================================================================

ConsoleLogger& SimulationConfig::defaultLogger() {
    static ConsoleLogger instance;
    return instance;
}

SimulationConfig::SimulationConfig(ILogging* logger)
    : configPath()
    , assetBasePath()
    , duration(3.0)
    , interactive(false)
    , playAudio(false)
    , volume(1.0f)
    , sineMode(false)
    , syncPull(true)
    , targetRPM(0.0)
    , targetLoad(-1.0)
    , useDefaultEngine(false)
    , outputWav(nullptr)
    , simulationFrequency(10000)
    , preFillMs(50)
    , audioMode()
    , logger(logger ? logger : &defaultLogger())
    , telemetryWriter(nullptr)
{
    // Logger is guaranteed non-null by constructor
}

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
    // For non-interactive (no inputProvider), check duration
    return currentTime < duration;
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

/// ============================================================================
/// Presentation Update
/// ============================================================================
void updatePresentation(presentation::IPresentation* presentation, double currentTime,
                        const EngineSimStats& stats, double throttle,
                        int underrunCount, IAudioRenderer& audioMode,
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

/// ============================================================================
/// Telemetry Write
/// ============================================================================
void writeTelemetry(telemetry::ITelemetryWriter* telemetryWriter,
                    const EngineSimStats& stats,
                    double currentTime,
                    double throttle,
                    bool ignition,
                    int underrunCount) {
    if (telemetryWriter) {
        telemetry::TelemetryData data;
        data.currentRPM = stats.currentRPM;
        data.currentLoad = stats.currentLoad;
        data.exhaustFlow = stats.exhaustFlow;
        data.manifoldPressure = stats.manifoldPressure;
        data.activeChannels = stats.activeChannels;
        data.processingTimeMs = stats.processingTimeMs;
        data.underrunCount = underrunCount;
        data.bufferHealthPct = 0.0;  // TODO: Calculate from audio player if available
        data.throttlePosition = throttle;
        data.ignitionOn = ignition;
        data.starterMotorEngaged = false;  // TODO: Track starter motor state
        data.timestamp = currentTime;
        telemetryWriter->write(data);
    }
}

// ----------------------------------------------------------------------------
// runSimulation helpers - ONE thing each
// ----------------------------------------------------------------------------

EngineSimConfig createDefaultEngineSimConfig(int sampleRate, int simulationFrequency = EngineConstants::DEFAULT_SIMULATION_FREQUENCY) {
    return EngineConfig::createDefault(sampleRate, simulationFrequency);
}

/**
 * Script configuration result from path resolution.
 * For sine mode: scriptPath and assetBasePath are empty (no script needed).
 * For other modes: paths are validated and converted to absolute.
 */
struct ScriptConfig {
    std::string scriptPath;
    std::string assetBasePath;
    bool valid = false;
    std::string errorMessage;
};

/**
 * Prepare script configuration based on mode priority.
 * PRIORITY ORDER: --sine > --default-engine > --script
 *
 * This is the ONLY place that checks mode and determines which script to load.
 * Path resolution and validation is handled by the bridge (SRP compliance).
 *
 * @param config Simulation configuration with mode flags
 * @return ScriptConfig with paths for bridge to process
 */
ScriptConfig prepareScriptConfig(const SimulationConfig& config) {
    ScriptConfig result;

    // Sine mode: no script needed, SineWaveSimulator creates dummy engine
    if (config.sineMode) {
        result.valid = true;
        return result;
    }

    // Determine which path to use
    if (config.useDefaultEngine) {
        result.scriptPath = "engine-sim-bridge/engine-sim/assets/main.mr";
    } else if (!config.configPath.empty()) {
        result.scriptPath = config.configPath;
    } else {
        result.errorMessage = "No engine configuration specified. Use --script <config.mr>, --default-engine, or --sine";
        return result;
    }

    // Path resolution, validation, and asset base path derivation handled by bridge
    // (SRP: CLI provides input, bridge processes it)
    result.valid = true;

    return result;
}

/// @brief Run the warmup phase of the simulation
/// @param handle Engine simulation handle
/// @param engineAPI Engine simulation API
/// @param audioPlayer Audio player instance
/// @param drainDuringWarmup Whether to drain audio during warmup
void runWarmupPhase(EngineSimHandle handle, EngineSimAPI& engineAPI,
                   AudioPlayer* audioPlayer, bool drainDuringWarmup) {
    WarmupOps::runWarmup(handle, engineAPI, audioPlayer, drainDuringWarmup);
}

/**
 * Create audio source for simulation.
 * Always uses EngineAudioSource - the bridge handles whether it's using
 * PistonEngineSimulator or SineWaveSimulator based on config.sineMode (DI pattern).
 * 
 * @param handle Engine simulation handle
 * @param engineAPI Engine simulation API
 * @return Audio source instance (always EngineAudioSource)
 */
std::unique_ptr<IAudioSource> createAudioSource(EngineSimHandle handle,
                                                EngineSimAPI& engineAPI) {
    // Bridge created the right simulator type during Create() based on config.sineMode
    // No need to check mode here - just use the handle polymorphically
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

void warnWavExportNotSupported(bool outputWavRequested, ILogging* logger) {
    if (outputWavRequested) {
        logger->warning(LogMask::AUDIO, "WAV export not supported in unified mode - use the old engine mode code path");
    }
}
} // anonymous namespace

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
    IAudioRenderer& audioMode,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter)
{
    double currentTime = 0.0;
    TimingOps::LoopTimer timer;

    const double minSustainedRPM = 550.0;
    double interactiveLoad = 0.1;
    double baselineLoad = interactiveLoad;
    bool wKeyPressed = false;

    // Note: Starter motor is already enabled in runSimulation() before warmup phase
    // This ensures the engine is running before warmup audio draining
    double throttle = getThrottle(inputProvider);

    config.logger->info(LogMask::BRIDGE, "runUnifiedAudioLoop starting simulation loop with %s mode", config.sineMode ? "SINE" : "ENGINE");

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

        // Update audio source with latest stats (needed for threaded sine mode)
        audioSource.updateStats(stats);
        audioMode.generateAudio(audioSource, audioPlayer);

        int underrunCount = getUnderrunCount(audioPlayer);

        // Write telemetry data
        writeTelemetry(telemetryWriter, stats, currentTime, throttle, ignition, underrunCount);

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        // Show audio source specific output (Frequency for sine, Flow for engine)
        audioSource.displayProgress(currentTime, config.duration, config.interactive, stats, throttle, underrunCount, audioPlayer);

        // Maintain 60hz timing with sleeps :(
        performTimingControl(timer);
    }

    return 0;
}

AudioPlayer *InitAudioPlayback(IAudioRenderer* audioMode, int sampleRate, EngineSimHandle handle, EngineSimAPI& engineAPI, ILogging* logger) {
    // This must happen AFTER the simulator is created and configured
    auto* audioPlayer = new AudioPlayer(nullptr, logger);

    // Use the provided audioMode (DI - don't create a new one!)
    bool initSuccess = audioPlayer->initialize(*audioMode, sampleRate, handle, &engineAPI);

    if (!initSuccess) {
        logger->error(LogMask::AUDIO, "Audio init failed");
        delete audioPlayer;
        return nullptr;
    }
    return audioPlayer;
}

void StartAudioMode(IAudioRenderer* audioMode, EngineSimHandle handle, EngineSimAPI& engineAPI, AudioPlayer* audioPlayer) {
    if (!audioMode) {
        if (audioPlayer) {
            delete audioPlayer;
        }
        engineAPI.Destroy(handle);
        throw std::runtime_error(std::string(ANSIColors::RED) + "ERROR: audioMode must be injected" + ANSIColors::RESET + "\n");
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
    IAudioRenderer* audioMode,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation) {
    const int sampleRate = AudioLoopConfig::SAMPLE_RATE;

    // Prepare script configuration (handles all mode priority and path validation)
    ScriptConfig scriptConfig = prepareScriptConfig(config);
    if (!scriptConfig.valid) {
        config.logger->error(LogMask::BRIDGE, "%s", scriptConfig.errorMessage.c_str());
        return 1;
    }
    
    // Show what we're loading (after validation)
    config.logger->info(LogMask::BRIDGE, "Loading simulator: %s%s%s", ANSIColors::CYAN.c_str(), config.sineMode ? "[SINE]" : scriptConfig.scriptPath.c_str(), ANSIColors::RESET.c_str());

    // Create simulator
    EngineSimConfig engineConfig = EngineConfig::createDefault(sampleRate, config.simulationFrequency);
    
    // Set sineMode in config so bridge creates correct simulator type (DI at creation)
    engineConfig.sineMode = config.sineMode ? 1 : 0;
    
    EngineSimHandle handle = nullptr;

    EngineSimResult result = engineAPI.Create(&engineConfig, &handle);
    if (result != ESIM_SUCCESS || !handle) {
        const char* err = handle ? engineAPI.GetLastError(handle) : "Unknown error";
        config.logger->error(LogMask::BRIDGE, "Failed to create simulator: %s", err);
        return 1;
    }

    // Load script (nullptr for sine mode since scriptPath will be empty)
    const char* scriptPathPtr = scriptConfig.scriptPath.empty() ? nullptr : scriptConfig.scriptPath.c_str();
    const char* assetBasePtr = scriptConfig.assetBasePath.empty() ? nullptr : scriptConfig.assetBasePath.c_str();

    // DI: Inject logging interface into bridge
    EngineSimResult logResult = engineAPI.SetLogging(handle, config.logger);
    if (logResult != ESIM_SUCCESS) {
        config.logger->warning(LogMask::BRIDGE, "Failed to set logging: result=%d", logResult);
    }

    result = engineAPI.LoadScript(handle, scriptPathPtr, assetBasePtr);
    if (result != ESIM_SUCCESS) {
        config.logger->error(LogMask::SCRIPT, "Failed to load script: %s", engineAPI.GetLastError(handle));
        engineAPI.Destroy(handle);
        return 1;
    }

    // Initialize Audio framework and playback if requested
    AudioPlayer* audioPlayer = InitAudioPlayback(audioMode, sampleRate, handle, engineAPI, config.logger);
    if (!audioPlayer) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize audio player");
        return 1;
    }
    audioPlayer->setVolume(config.volume);
    StartAudioMode(audioMode, handle, engineAPI, audioPlayer);
    audioMode->configure(config);

    // CRITICAL: Enable starter motor BEFORE warmup phase
    // Otherwise, warmup will hang because RenderOnDemand returns 0 frames (engine not running)
    enableStarterMotor(handle, engineAPI);

    // REORDER: Warmup FIRST, then pre-fill buffer
    // This ensures engine is warm when RenderOnDemand is called during pre-fill
    // Check if drain is needed during warmup
    bool drainDuringWarmup = config.playAudio && audioPlayer && audioMode->shouldDrainDuringWarmup();
    runWarmupPhase(handle, engineAPI, audioPlayer, drainDuringWarmup);

    // Now pre-fill buffer with WARM engine
    audioMode->prepareBuffer(audioPlayer);

    // Reset buffer after warmup based on audio mode
    audioMode->resetBufferAfterWarmup(audioPlayer);
    
    // Start playback based on audio mode
    audioMode->startPlayback(audioPlayer);
    std::unique_ptr<IAudioSource> audioSource = createAudioSource(handle, engineAPI);
    
    // Run MAIN loop - now uses injectable input provider and presentation
    // Some input providers enable ignition by default which will allow the engine to fire during the cranking phase otherwise it will crank endlessly huffing and puffing
    int exitCode = runUnifiedAudioLoop(handle, engineAPI, *audioSource, config, audioPlayer, *audioMode, inputProvider, presentation, config.telemetryWriter);
    
    // EXIT
    cleanupSimulation(audioPlayer, handle, engineAPI, exitCode);

    // If WAV export was requested, warn that it's not supported in unified mode (since it requires a different audio thread setup)
    warnWavExportNotSupported(config.outputWav, config.logger);

    return exitCode;
}
