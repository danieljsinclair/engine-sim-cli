// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Phase E: Uses ISimulator* instead of EngineSimHandle/EngineSimAPI&

#include "SimulationLoop.h"

#include "simulation/ISimulator.h"
#include "simulation/BridgeSimulator.h"
#include "config/CLIconfig.h"
#include "audio/hardware/IAudioHardwareProvider.h"
#include "audio/hardware/CoreAudioHardwareProvider.h"
#include "audio/strategies/IAudioStrategy.h"
#include "audio/state/Diagnostics.h"
#include "config/ANSIColors.h"
#include "input/IInputProvider.h"
#include "presentation/IPresentation.h"
#include "simulation/EngineConfig.h"
#include "ILogging.h"
#include "ITelemetryProvider.h"
#include "Verification.h"

#include <cstring>
#include <atomic>
#include <csignal>
#include <iostream>
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

// Timing control for 60Hz loop pacing using sleep_until for accuracy
struct LoopTimer {
    std::chrono::steady_clock::time_point nextWakeTime;
    std::chrono::microseconds intervalUs;

    LoopTimer()
        : nextWakeTime(std::chrono::steady_clock::now())
        , intervalUs(static_cast<long long>(AudioLoopConfig::UPDATE_INTERVAL * 1000000.0))
    {}

    void waitUntilNextTick() {
        nextWakeTime += intervalUs;
        std::this_thread::sleep_until(nextWakeTime);
    }
};

// Named audio render callback -- bridges platform audio buffers to strategy->render()
int audioRenderCallback(IAudioStrategy* strategy, int numberFrames,
                        PlatformAudioBufferList* platformBufferList) {
    if (!strategy->isPlaying()) {
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
        strategy->render(&bufferList, numberFrames);
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

void enableStarterMotor(ISimulator& simulator) {
    simulator.setStarterMotor(true);
}

bool checkStarterMotorRPM(ISimulator& simulator, double minSustainedRPM) {
    EngineSimStats stats = simulator.getStats();
    if (stats.currentRPM > minSustainedRPM) {
        simulator.setStarterMotor(false);
        return true;
    }
    return false;
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
    if (!presentation) return;

    auto snap = audioStrategy.getDiagnosticsSnapshot();

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
    state.renderMs = snap.lastRenderMs;
    state.headroomMs = snap.lastHeadroomMs;
    state.budgetPct = snap.lastBudgetPct;

    presentation->ShowEngineState(state);
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

void runWarmupPhase(ISimulator& simulator,
                   bool drainDuringWarmup) {
    double smoothedThrottle = 0.6;
    double currentTime = 0.0;

    for (int i = 0; i < AudioLoopConfig::WARMUP_ITERATIONS; i++) {
        EngineSimStats stats = simulator.getStats();

        simulator.setThrottle(smoothedThrottle);
        simulator.update(AudioLoopConfig::UPDATE_INTERVAL);

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        if (drainDuringWarmup) {
            std::vector<float> discardBuffer(AudioLoopConfig::FRAMES_PER_UPDATE * 2);
            int warmupRead = 0;

            for (int retry = 0; retry <= 3 && warmupRead < AudioLoopConfig::FRAMES_PER_UPDATE; retry++) {
                int readThisTime = 0;
                simulator.renderOnDemand(
                    discardBuffer.data() + warmupRead * 2,
                    AudioLoopConfig::FRAMES_PER_UPDATE - warmupRead,
                    &readThisTime);

                if (readThisTime > 0) warmupRead += readThisTime;
            }
        }
    }
}

void cleanupSimulation(IAudioHardwareProvider* hardwareProvider, ISimulator& simulator) {
    if (hardwareProvider) {
        hardwareProvider->stopPlayback();
        hardwareProvider->cleanup();
    }
    simulator.destroy();
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
    ISimulator& simulator,
    const SimulationConfig& config,
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
        checkStarterMotorRPM(simulator, minSustainedRPM);

        throttle = updateInputAndGetThrottle(inputProvider, currentTime);
        simulator.setThrottle(throttle);

        bool ignition = inputProvider ? inputProvider->GetIgnition() : true;
        simulator.setIgnition(ignition);

        // Update simulation via strategy (threaded mode updates here; sync-pull is no-op)
        audioStrategy.updateSimulation(&simulator, AudioLoopConfig::UPDATE_INTERVAL * 1000.0);

        EngineSimStats stats = simulator.getStats();

        // Generate audio: strategy decides whether to fill buffer (Threaded fills, SyncPull no-ops)
        audioStrategy.fillBufferFromEngine(&simulator, AudioLoopConfig::FRAMES_PER_UPDATE);

        writeTelemetry(telemetryWriter, stats, currentTime, throttle, ignition);

        // Read underrun count from telemetry (pushed by ThreadedStrategy)
        int underrunCount = 0;
        if (telemetryReader) {
            auto audioDiag = telemetryReader->getAudioDiagnostics();
            underrunCount = audioDiag.underrunCount;
        }

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        // Display via presentation (ConsolePresentation formats the complete output line)
        updatePresentation(presentation, currentTime, stats, throttle, underrunCount, audioStrategy, inputProvider);

        // Pace to 60Hz using sleep_until for accuracy
        timer.waitUntilNextTick();
    }

    return 0;
}

// ============================================================================
// Main Simulation Entry Point
// ============================================================================

int runSimulation(
    const SimulationConfig& config,
    ISimulator& simulator,
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

    if (!simulator.create(engineConfig)) {
        config.logger->error(LogMask::BRIDGE, "Failed to create simulator: %s", simulator.getLastError().c_str());
        return 1;
    }

    if (!simulator.setLogging(config.logger)) {
        config.logger->warning(LogMask::BRIDGE, "Failed to set logging");
    }

    if (!scriptConfig.scriptPath.empty()) {
        if (!simulator.loadScript(scriptConfig.scriptPath, scriptConfig.assetBasePath)) {
            config.logger->error(LogMask::SCRIPT, "Failed to load script: %s", simulator.getLastError().c_str());
            simulator.destroy();
            return 1;
        }
    } else {
        // Sine mode or no script -- still need to initialize synthesizer
        if (!simulator.loadScript("", "")) {
            config.logger->error(LogMask::SCRIPT, "Failed to initialize synthesizer: %s", simulator.getLastError().c_str());
            simulator.destroy();
            return 1;
        }
    }

    // Initialize strategy
    AudioStrategyConfig strategyConfig;
    strategyConfig.sampleRate = sampleRate;
    strategyConfig.channels = 2;

    if (!audioStrategy->initialize(strategyConfig)) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize audio strategy");
        simulator.destroy();
        return 1;
    }

    // Create and initialize audio hardware provider
    auto callback = [audioStrategy](void* refCon, void* actionFlags,
                           const void* timeStamp, int busNumber, int numberFrames,
                           PlatformAudioBufferList* platformBufferList) -> int {
        (void)refCon;
        (void)actionFlags;
        (void)timeStamp;
        (void)busNumber;
        return audioRenderCallback(audioStrategy, numberFrames, platformBufferList);
    };

    auto hardwareProvider = createHardwareProvider(sampleRate, callback, config.logger);
    if (!hardwareProvider) {
        config.logger->error(LogMask::AUDIO, "Failed to initialize audio hardware");
        simulator.destroy();
        return 1;
    }

    config.logger->info(LogMask::AUDIO, "Audio initialized: strategy=%s, sr=%d",
                         audioStrategy->getName(), sampleRate);

    // Start strategy playback
    if (!audioStrategy->startPlayback(&simulator)) {
        config.logger->error(LogMask::AUDIO, "Failed to start audio playback");
        hardwareProvider->cleanup();
        simulator.destroy();
        return 1;
    }

    // Set volume
    hardwareProvider->setVolume(config.volume);

    enableStarterMotor(simulator);

    bool drainDuringWarmup = config.playAudio && audioStrategy->shouldDrainDuringWarmup();
    runWarmupPhase(simulator, drainDuringWarmup);

    // Prepare buffer via strategy (threaded: pre-fills, sync-pull: no-op)
    audioStrategy->prepareBuffer();

    // Reset buffer after warmup via strategy
    audioStrategy->resetBufferAfterWarmup();

    // Start audio hardware playback
    if (!hardwareProvider->startPlayback()) {
        config.logger->error(LogMask::AUDIO, "Failed to start hardware playback");
    }

    int exitCode = runUnifiedAudioLoop(simulator, config, *audioStrategy, inputProvider, presentation, config.telemetryWriter, config.telemetryReader);

    // Cleanup
    audioStrategy->stopPlayback(&simulator);
    cleanupSimulation(hardwareProvider.get(), simulator);

    warnWavExportNotSupported(config.outputWav, config.logger);

    return exitCode;
}
