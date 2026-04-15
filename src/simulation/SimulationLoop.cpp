// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Uses IAudioStrategy directly (no adapter, no IAudioRenderer)

#include "SimulationLoop.h"

#include "config/CLIconfig.h"
#include "AudioPlayer.h"
#include "AudioSource.h"
#include "audio/strategies/IAudioStrategy.h"
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
    , logger(logger ? logger : &defaultLogger())
    , telemetryWriter(nullptr)
{
}

// ============================================================================
// Private Helper Functions - SRP Compliance
// ============================================================================

namespace {

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

int getUnderrunCount(AudioPlayer* audioPlayer) {
    if (!audioPlayer) {
        return 0;
    }
    auto* context = audioPlayer->getContext();
    return context ? context->bufferState.underrunCount.load() : 0;
}

void performTimingControl(TimingOps::LoopTimer& timer) {
    timer.sleepToMaintain60Hz();
}

bool shouldContinueLoop(double currentTime, double duration, input::IInputProvider* inputProvider) {
    if (inputProvider) {
        return inputProvider->ShouldContinue();
    }
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

void updatePresentation(presentation::IPresentation* presentation, double currentTime,
                        const EngineSimStats& stats, double throttle,
                        int underrunCount, IAudioStrategy& audioStrategy,
                        input::IInputProvider* inputProvider) {
    if (presentation) {
        presentation::EngineState state;
        state.timestamp = currentTime;
        state.rpm = stats.currentRPM;
        state.throttle = throttle;
        state.load = stats.currentLoad;
        state.speed = 0;
        state.underrunCount = underrunCount;
        state.audioMode = audioStrategy.getModeString();
        state.ignition = inputProvider ? inputProvider->GetIgnition() : true;
        state.starterMotor = false;
        state.exhaustFlow = stats.exhaustFlow;

        presentation->ShowEngineState(state);
    }
}

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
        data.bufferHealthPct = 0.0;
        data.throttlePosition = throttle;
        data.ignitionOn = ignition;
        data.starterMotorEngaged = false;
        data.timestamp = currentTime;
        telemetryWriter->write(data);
    }
}

EngineSimConfig createDefaultEngineSimConfig(int sampleRate, int simulationFrequency = EngineConstants::DEFAULT_SIMULATION_FREQUENCY) {
    return EngineConfig::createDefault(sampleRate, simulationFrequency);
}

struct ScriptConfig {
    std::string scriptPath;
    std::string assetBasePath;
    bool valid = false;
    std::string errorMessage;
};

ScriptConfig prepareScriptConfig(const SimulationConfig& config) {
    ScriptConfig result;

    if (config.sineMode) {
        result.valid = true;
        return result;
    }

    if (config.useDefaultEngine) {
        result.scriptPath = "engine-sim-bridge/engine-sim/assets/main.mr";
    } else if (!config.configPath.empty()) {
        result.scriptPath = config.configPath;
    } else {
        result.errorMessage = "No engine configuration specified. Use --script <config.mr>, --default-engine, or --sine";
        return result;
    }

    result.valid = true;
    return result;
}

void runWarmupPhase(EngineSimHandle handle, EngineSimAPI& engineAPI,
                   AudioPlayer* audioPlayer, bool drainDuringWarmup) {
    WarmupOps::runWarmup(handle, engineAPI, audioPlayer, drainDuringWarmup);
}

std::unique_ptr<IAudioSource> createAudioSource(EngineSimHandle handle,
                                                EngineSimAPI& engineAPI) {
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

/**
 * Generate audio frames via engine API and write to the player's circular buffer.
 * Used for threaded mode where the main loop generates audio proactively.
 */
void generateAudioForThreadedMode(EngineSimHandle handle, const EngineSimAPI& api,
                                  IAudioSource& audioSource, AudioPlayer* audioPlayer) {
    if (!audioPlayer) return;

    int defaultFrames = AudioLoopConfig::FRAMES_PER_UPDATE;
    int framesToGenerate = audioPlayer->calculateCursorChasingSamples(defaultFrames);

    std::vector<float> buffer(framesToGenerate * 2);
    int totalRead = 0;
    api.ReadAudioBuffer(handle, buffer.data(), framesToGenerate, &totalRead);

    if (totalRead > 0) {
        audioPlayer->addToCircularBuffer(buffer.data(), totalRead);
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
    const SimulationConfig& config,
    AudioPlayer* audioPlayer,
    IAudioStrategy& audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter)
{
    double currentTime = 0.0;
    TimingOps::LoopTimer timer;

    const double minSustainedRPM = 550.0;

    double throttle = getThrottle(inputProvider);

    config.logger->info(LogMask::BRIDGE, "runUnifiedAudioLoop starting simulation loop with %s mode", config.sineMode ? "SINE" : "ENGINE");

    while (shouldContinueLoop(currentTime, config.duration, inputProvider)) {
        checkStarterMotorRPM(handle, api, minSustainedRPM);

        throttle = updateInputAndGetThrottle(inputProvider, currentTime);
        api.SetThrottle(handle, throttle);

        bool ignition = inputProvider ? inputProvider->GetIgnition() : true;
        api.SetIgnition(handle, ignition ? 1 : 0);

        // Update simulation via strategy (threaded mode updates here; sync-pull is no-op)
        BufferContext* ctx = audioPlayer ? audioPlayer->getContext() : nullptr;
        if (ctx) {
            audioStrategy.updateSimulation(ctx, handle, api, AudioLoopConfig::UPDATE_INTERVAL * 1000.0);
        }

        EngineSimStats stats = {};
        api.GetStats(handle, &stats);

        audioSource.updateStats(stats);

        // For threaded mode: generate audio proactively and add to buffer
        // For sync-pull mode: no generation needed (render callback handles it)
        if (audioPlayer) {
            generateAudioForThreadedMode(handle, api, audioSource, audioPlayer);
        }

        int underrunCount = getUnderrunCount(audioPlayer);

        writeTelemetry(telemetryWriter, stats, currentTime, throttle, ignition, underrunCount);

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        audioSource.displayProgress(currentTime, config.duration, config.interactive, stats, throttle, underrunCount, audioPlayer);

        updatePresentation(presentation, currentTime, stats, throttle, underrunCount, audioStrategy, inputProvider);

        performTimingControl(timer);
    }

    return 0;
}

AudioPlayer* InitAudioPlayback(IAudioStrategy* strategy, int sampleRate, EngineSimHandle handle, EngineSimAPI& engineAPI, ILogging* logger) {
    auto* audioPlayer = new AudioPlayer(strategy, logger);

    bool initSuccess = audioPlayer->initialize(sampleRate, handle, &engineAPI);

    if (!initSuccess) {
        logger->error(LogMask::AUDIO, "Audio init failed");
        delete audioPlayer;
        return nullptr;
    }
    return audioPlayer;
}

void StartAudioMode(IAudioStrategy* strategy, EngineSimHandle handle, EngineSimAPI& engineAPI, AudioPlayer* audioPlayer) {
    if (!strategy) {
        if (audioPlayer) {
            delete audioPlayer;
        }
        engineAPI.Destroy(handle);
        throw std::runtime_error(std::string(ANSIColors::RED) + "ERROR: audioStrategy must be injected" + ANSIColors::RESET + "\n");
    }

    BufferContext* ctx = audioPlayer ? audioPlayer->getContext() : nullptr;
    if (!strategy->startPlayback(ctx, handle, &engineAPI)) {
        if (audioPlayer) {
            delete audioPlayer;
        }
        engineAPI.Destroy(handle);
        throw std::runtime_error("ERROR: Failed to start audio playback\n");
    }
}

// ============================================================================
// Main Simulation Entry Point
// ============================================================================

int runSimulation(
    const SimulationConfig& config,
    EngineSimAPI& engineAPI,
    IAudioStrategy* audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation) {
    const int sampleRate = AudioLoopConfig::SAMPLE_RATE;

    ScriptConfig scriptConfig = prepareScriptConfig(config);
    if (!scriptConfig.valid) {
        config.logger->error(LogMask::BRIDGE, "%s", scriptConfig.errorMessage.c_str());
        return 1;
    }

    config.logger->info(LogMask::BRIDGE, "Loading simulator: %s%s%s", ANSIColors::CYAN.c_str(), config.sineMode ? "[SINE]" : scriptConfig.scriptPath.c_str(), ANSIColors::RESET.c_str());

    EngineSimConfig engineConfig = EngineConfig::createDefault(sampleRate, config.simulationFrequency);
    engineConfig.sineMode = config.sineMode ? 1 : 0;

    EngineSimHandle handle = nullptr;

    EngineSimResult result = engineAPI.Create(&engineConfig, &handle);
    if (result != ESIM_SUCCESS || !handle) {
        const char* err = handle ? engineAPI.GetLastError(handle) : "Unknown error";
        config.logger->error(LogMask::BRIDGE, "Failed to create simulator: %s", err);
        return 1;
    }

    const char* scriptPathPtr = scriptConfig.scriptPath.empty() ? nullptr : scriptConfig.scriptPath.c_str();
    const char* assetBasePtr = scriptConfig.assetBasePath.empty() ? nullptr : scriptConfig.assetBasePath.c_str();

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

    AudioPlayer* audioPlayer = InitAudioPlayback(audioStrategy, sampleRate, handle, engineAPI, config.logger);
    if (!audioPlayer) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize audio player");
        return 1;
    }
    audioPlayer->setVolume(config.volume);
    StartAudioMode(audioStrategy, handle, engineAPI, audioPlayer);

    AudioStrategyConfig strategyConfig;
    strategyConfig.sampleRate = sampleRate;
    strategyConfig.channels = 2;
    audioStrategy->configure(strategyConfig);

    enableStarterMotor(handle, engineAPI);

    bool drainDuringWarmup = config.playAudio && audioPlayer && audioStrategy->shouldDrainDuringWarmup();
    runWarmupPhase(handle, engineAPI, audioPlayer, drainDuringWarmup);

    // Prepare buffer via strategy (threaded: pre-fills, sync-pull: no-op)
    BufferContext* ctx = audioPlayer->getContext();
    audioStrategy->prepareBuffer(ctx);

    // Reset buffer after warmup via strategy
    audioStrategy->resetBufferAfterWarmup(ctx);

    // Start AudioUnit playback via AudioPlayer
    audioPlayer->start();

    std::unique_ptr<IAudioSource> audioSource = createAudioSource(handle, engineAPI);

    int exitCode = runUnifiedAudioLoop(handle, engineAPI, *audioSource, config, audioPlayer, *audioStrategy, inputProvider, presentation, config.telemetryWriter);

    cleanupSimulation(audioPlayer, handle, engineAPI, exitCode);

    warnWavExportNotSupported(config.outputWav, config.logger);

    return exitCode;
}
