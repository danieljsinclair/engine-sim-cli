// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Uses IAudioStrategy directly (no adapter, no IAudioRenderer)

#include "SimulationLoop.h"

#include "config/CLIconfig.h"
#include "audio/hardware/IAudioHardwareProvider.h"
#include "audio/hardware/CoreAudioHardwareProvider.h"
#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/BufferContext.h"
#include "audio/common/CircularBuffer.h"
#include "input/IInputProvider.h"
#include "presentation/IPresentation.h"
#include "simulation/EngineConfig.h"
#include "config/ANSIColors.h"
#include "bridge/engine_sim_loader.h"
#include "ILogging.h"
#include "ITelemetryProvider.h" 
#include "Verification.h"

#include <cstring>
#include <atomic>
#include <csignal>
#include <iostream>
#include <iomanip>
#include <thread>
#include <chrono>

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

// Timing control for 60Hz loop pacing (moved from AudioSource)
struct LoopTimer {
    std::chrono::steady_clock::time_point absoluteStartTime;
    int iterationCount;

    LoopTimer() : absoluteStartTime(std::chrono::steady_clock::now()), iterationCount(0) {}

    void sleepToMaintain60Hz() {
        iterationCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(now - absoluteStartTime).count();
        auto targetUs = static_cast<long long>(iterationCount * AudioLoopConfig::UPDATE_INTERVAL * 1000000);
        auto sleepUs = targetUs - elapsedUs;

        if (sleepUs > 0) {
            std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
        }
    }
};

// User data for the audio callback
struct AudioCallbackContext {
    BufferContext* bufferContext;
    IAudioStrategy* strategy;
};

// Named audio render callback -- bridges platform audio buffers to strategy->render()
// Takes AudioCallbackContext directly (not via refCon, which belongs to the hardware provider).
int audioRenderCallback(const AudioCallbackContext& ctx, int numberFrames,
                        PlatformAudioBufferList* platformBufferList) {
    if (!ctx.bufferContext->audioState.isPlaying.load()) {
        if (platformBufferList && platformBufferList->bufferData) {
            for (int i = 0; i < platformBufferList->numberBuffers; i++) {
                if (platformBufferList->bufferData[i]) {
                    AudioBuffer* buffers = static_cast<AudioBuffer*>(platformBufferList->buffers);
                    std::memset(buffers[i].mData, 0, buffers[i].mDataByteSize);
                }
            }
        }
        return 0;
    }

    if (platformBufferList && platformBufferList->buffers) {
        AudioBuffer* audioBuffers = static_cast<AudioBuffer*>(platformBufferList->buffers);
        AudioBufferList bufferList;
        bufferList.mNumberBuffers = static_cast<UInt32>(platformBufferList->numberBuffers);
        for (int i = 0; i < platformBufferList->numberBuffers; i++) {
            bufferList.mBuffers[i] = audioBuffers[i];
        }
        ctx.strategy->render(ctx.bufferContext, &bufferList, numberFrames);
    }

    return 0;
}

// Create and initialize the audio hardware provider
std::unique_ptr<IAudioHardwareProvider> createHardwareProvider(
    int sampleRate,
    const IAudioHardwareProvider::AudioCallback& callback,
    ILogging* logger)
{
    auto provider = std::make_unique<CoreAudioHardwareProvider>(logger);
    provider->registerAudioCallback(callback);

    AudioStreamFormat format;
    format.sampleRate = sampleRate;
    format.channels = 2;
    format.bitsPerSample = 32;
    format.isFloat = true;
    format.isInterleaved = true;

    if (!provider->initialize(format)) {
        return nullptr;
    }

    return provider;
}

void outputProgress(bool interactive, const std::string& prefix,
    double currentTime, double duration, int progress,
    const EngineSimStats& stats, double throttle, int underrunCount) {
    (void)currentTime;
    (void)duration;
    if (interactive) {
        std::cout << prefix << "\n" << std::flush;
    } else {
        static int lastProgress = 0;
        if (progress != lastProgress && progress % 10 == 0) {
            std::cout << prefix << "  Progress: " << progress << "% | RPM: " << static_cast<int>(stats.currentRPM)
                        << " | Throttle: " << static_cast<int>(throttle * 100) << "%"
                        << " | Underruns: " << underrunCount << "\r" << std::flush;
            lastProgress = progress;
        }
    }
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

void performTimingControl(LoopTimer& timer) {
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
                    bool ignition) {
    if (!telemetryWriter) return;

    // Push engine state
    telemetry::EngineStateTelemetry engine;
    engine.currentRPM = stats.currentRPM;
    engine.currentLoad = stats.currentLoad;
    engine.exhaustFlow = stats.exhaustFlow;
    engine.manifoldPressure = stats.manifoldPressure;
    engine.activeChannels = stats.activeChannels;
    telemetryWriter->writeEngineState(engine);

    // Push frame performance
    telemetry::FramePerformanceTelemetry perf;
    perf.processingTimeMs = stats.processingTimeMs;
    telemetryWriter->writeFramePerformance(perf);

    // Push vehicle inputs
    telemetry::VehicleInputsTelemetry inputs;
    inputs.throttlePosition = throttle;
    inputs.ignitionOn = ignition;
    inputs.starterMotorEngaged = false;
    telemetryWriter->writeVehicleInputs(inputs);

    // Push simulator metrics
    telemetry::SimulatorMetricsTelemetry metrics;
    metrics.timestamp = currentTime;
    telemetryWriter->writeSimulatorMetrics(metrics);

    // Note: AudioDiagnostics (underrunCount, bufferHealthPct) are pushed
    // by ThreadedStrategy, not here -- SRP/ISP compliance
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
                   bool drainDuringWarmup) {
    double smoothedThrottle = 0.6;
    double currentTime = 0.0;

    for (int i = 0; i < AudioLoopConfig::WARMUP_ITERATIONS; i++) {
        EngineSimStats stats = {};
        engineAPI.GetStats(handle, &stats);

        engineAPI.SetThrottle(handle, smoothedThrottle);
        engineAPI.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        if (drainDuringWarmup) {
            std::vector<float> discardBuffer(AudioLoopConfig::FRAMES_PER_UPDATE * 2);
            int warmupRead = 0;

            for (int retry = 0; retry <= 3 && warmupRead < AudioLoopConfig::FRAMES_PER_UPDATE; retry++) {
                int readThisTime = 0;
                engineAPI.RenderOnDemand(handle,
                    discardBuffer.data() + warmupRead * 2,
                    AudioLoopConfig::FRAMES_PER_UPDATE - warmupRead,
                    &readThisTime);

                if (readThisTime > 0) warmupRead += readThisTime;
            }
        }
    }
}

void cleanupSimulation(IAudioHardwareProvider* hardwareProvider, EngineSimHandle handle,
                       EngineSimAPI& engineAPI) {
    if (hardwareProvider) {
        hardwareProvider->stopPlayback();
        hardwareProvider->cleanup();
    }
    engineAPI.Destroy(handle);
}

void warnWavExportNotSupported(bool outputWavRequested, ILogging* logger) {
    if (outputWavRequested) {
        logger->warning(LogMask::AUDIO, "WAV export not supported in unified mode - use the old engine mode code path");
    }
}

} // anonymous namespace

// ============================================================================
// Unified Main Loop Implementation
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    const SimulationConfig& config,
    BufferContext* audioContext,
    IAudioStrategy& audioStrategy,
    input::IInputProvider* inputProvider,
    presentation::IPresentation* presentation,
    telemetry::ITelemetryWriter* telemetryWriter,
    telemetry::ITelemetryReader* telemetryReader)
{
    double currentTime = 0.0;
    LoopTimer timer;

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
        audioStrategy.updateSimulation(audioContext, handle, api, AudioLoopConfig::UPDATE_INTERVAL * 1000.0);

        EngineSimStats stats = {};
        api.GetStats(handle, &stats);

        // Generate audio: strategy decides whether to fill buffer (Threaded fills, SyncPull no-ops)
        audioStrategy.fillBufferFromEngine(audioContext, handle, api, AudioLoopConfig::FRAMES_PER_UPDATE);

        writeTelemetry(telemetryWriter, stats, currentTime, throttle, ignition);

        // Read underrun count from telemetry (pushed by ThreadedStrategy)
        int underrunCount = 0;
        if (telemetryReader) {
            auto audioDiag = telemetryReader->getAudioDiagnostics();
            underrunCount = audioDiag.underrunCount;
        }

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        // Display progress (inlined from EngineAudioSource)
        {
            int progress = static_cast<int>(currentTime * 100 / config.duration);
            std::ostringstream prefix;
            int rpm = static_cast<int>(stats.currentRPM);
            if (rpm < 10 && stats.currentRPM > 0) rpm = 0;
            prefix << "[" << std::setw(5) << rpm << " RPM] ";
            prefix << "[Throttle: " << std::setw(4) << static_cast<int>(throttle * 100) << "%] ";
            prefix << "[Underruns: " << underrunCount << "] ";
            prefix << ANSIColors::INFO << "[Flow: " << std::fixed << std::showpos << std::setw(8) << std::setprecision(5) << stats.exhaustFlow << std::noshowpos << " m3/s]" << ANSIColors::RESET << " ";

            if (audioContext) {
                double renderMs = audioContext->diagnostics.lastRenderMs.load();
                double headroomMs = audioContext->diagnostics.lastHeadroomMs.load();
                double budgetPct = audioContext->diagnostics.lastBudgetPct.load();

                if (renderMs > 0.0) {
                    std::string budgetColor = ANSIColors::getDispositionColour(budgetPct < 80, budgetPct < 100);
                    prefix << "[SYNC-PULL] rendered=" << std::setw(5) << std::fixed << std::setprecision(1) << renderMs << "ms"
                           << " headroom=" << std::setw(5) << std::showpos << std::setprecision(1) << headroomMs << std::noshowpos << "ms"
                           << " (" << budgetColor << std::setw(3) << std::setprecision(0) << budgetPct << "% of budget" << ANSIColors::RESET << ")";
                }
            }

            outputProgress(config.interactive, prefix.str(), currentTime, config.duration, progress, stats, throttle, underrunCount);
        }

        updatePresentation(presentation, currentTime, stats, throttle, underrunCount, audioStrategy, inputProvider);

        performTimingControl(timer);
    }

    return 0;
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

    ASSERT(audioStrategy, "audioStrategy must be provided", [&config]() { config.logger->error(LogMask::AUDIO, "ERROR: audioStrategy must be injected"); });

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

    // Create and initialize buffer context
    CircularBuffer circularBuffer;
    if (!circularBuffer.initialize(sampleRate * 2)) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize circular buffer");
        engineAPI.Destroy(handle);
        return 1;
    }

    BufferContext audioContext;
    audioContext.circularBuffer = &circularBuffer;
    audioContext.audioState.sampleRate = sampleRate;
    audioContext.audioState.isPlaying = false;

    // Initialize strategy
    AudioStrategyConfig strategyConfig;
    strategyConfig.sampleRate = sampleRate;
    strategyConfig.channels = 2;
    strategyConfig.engineHandle = handle;
    strategyConfig.engineAPI = &engineAPI;

    if (!audioStrategy->initialize(&audioContext, strategyConfig)) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize audio strategy");
        engineAPI.Destroy(handle);
        return 1;
    }

    // Create and initialize audio hardware provider
    AudioCallbackContext callbackCtx{&audioContext, audioStrategy};
    auto callback = [callbackCtx](void* refCon, void* actionFlags,
                           const void* timeStamp, int busNumber, int numberFrames,
                           PlatformAudioBufferList* platformBufferList) -> int {
        (void)refCon;
        (void)actionFlags;
        (void)timeStamp;
        (void)busNumber;
        return audioRenderCallback(callbackCtx, numberFrames, platformBufferList);
    };

    auto hardwareProvider = createHardwareProvider(sampleRate, callback, config.logger);
    if (!hardwareProvider) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize audio hardware");
        engineAPI.Destroy(handle);
        return 1;
    }

    config.logger->info(LogMask::AUDIO, "Audio initialized: strategy=%s, sr=%d",
                         audioStrategy->getName(), sampleRate);

    // Start strategy playback
    if (!audioStrategy->startPlayback(&audioContext, handle, &engineAPI)) {
        config.logger->error(LogMask::AUDIO, "Failed to start audio playback");
        hardwareProvider->cleanup();
        engineAPI.Destroy(handle);
        return 1;
    }

    // Set volume
    hardwareProvider->setVolume(config.volume);

    enableStarterMotor(handle, engineAPI);

    bool drainDuringWarmup = config.playAudio && audioStrategy->shouldDrainDuringWarmup();
    runWarmupPhase(handle, engineAPI, drainDuringWarmup);

    // Prepare buffer via strategy (threaded: pre-fills, sync-pull: no-op)
    audioStrategy->prepareBuffer(&audioContext);

    // Reset buffer after warmup via strategy
    audioStrategy->resetBufferAfterWarmup(&audioContext);

    // Start audio hardware playback
    audioContext.audioState.isPlaying.store(true);
    if (!hardwareProvider->startPlayback()) {
        config.logger->error(LogMask::AUDIO, "Failed to start hardware playback");
        audioContext.audioState.isPlaying.store(false);
    }

    int exitCode = runUnifiedAudioLoop(handle, engineAPI, config, &audioContext, *audioStrategy, inputProvider, presentation, config.telemetryWriter, config.telemetryReader);

    // Cleanup
    audioContext.audioState.isPlaying.store(false);
    cleanupSimulation(hardwareProvider.get(), handle, engineAPI);

    warnWavExportNotSupported(config.outputWav, config.logger);

    return exitCode;
}
