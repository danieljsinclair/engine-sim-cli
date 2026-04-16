// SimulationLoop.cpp - Simulation loop implementation
// Extracted from engine_sim_cli.cpp for SOLID SRP compliance
// Phase E: Uses ISimulator* instead of EngineSimHandle/EngineSimAPI&

#include "SimulationLoop.h"

#include "ISimulator.h"
#include "BridgeSimulator.h"
#include "AudioLoopConfig.h"
#include "IAudioHardwareProvider.h"
#include "CoreAudioHardwareProvider.h"
#include "IAudioStrategy.h"
#include "Diagnostics.h"
#include "config/ANSIColors.h"
#include "input/IInputProvider.h"
#include "presentation/IPresentation.h"
#include "simulation/EngineConfig.h"
#include "ILogging.h"
#include "ITelemetryProvider.h"
#include "Verification.h"

#include <cstring>
#include <stdexcept>
#include <atomic>
#include <csignal>
#include <iostream>
#include <thread>
#include <chrono>

// ============================================================================
// SimulationConfig Implementation
// ============================================================================

SimulationConfig::SimulationConfig()
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
    , telemetryWriter(nullptr)
    , telemetryReader(nullptr)
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

// Create and initialize the audio hardware provider. Throws on failure.
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
        throw std::runtime_error("Failed to initialize audio hardware");
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

// Get input for non-interactive (timed) mode: ramp throttle from 0 to 1 over 0.5s
input::EngineInput getTimedInput(double currentTime) {
    input::EngineInput input;
    input.throttle = currentTime < 0.5 ? currentTime / 0.5 : 1.0;
    input.ignition = true;
    input.starterMotor = false;
    return input;
}

void updatePresentation(presentation::IPresentation* presentation, double currentTime,
                        const EngineSimStats& stats, double throttle,
                        int underrunCount, IAudioStrategy& audioStrategy,
                        input::IInputProvider* inputProvider,
                        telemetry::ITelemetryReader* telemetryReader) {
    if (!presentation) return;

    // Read audio timing diagnostics from telemetry (strategies push to telemetry after each render)
    telemetry::AudioTimingTelemetry timing;
    if (telemetryReader) {
        timing = telemetryReader->getAudioTiming();
    }

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
    state.renderMs = timing.renderMs;
    state.headroomMs = timing.headroomMs;
    state.budgetPct = timing.budgetPct;
    state.framesRequested = timing.framesRequested;
    state.framesRendered = timing.framesRendered;
    state.callbackRateHz = timing.callbackRateHz;
    state.generatingRateFps = timing.generatingRateFps;
    state.trendPct = timing.trendPct;
    state.sampleRate = 48000;

    presentation->ShowEngineState(state);
}

void writeTelemetry(telemetry::ITelemetryWriter* telemetryWriter,
                    double currentTime,
                    double throttle,
                    bool ignition) {
    if (!telemetryWriter) return;

    // Push vehicle inputs (loop owns throttle/ignition, simulator doesn't)
    telemetry::VehicleInputsTelemetry inputs;
    inputs.throttlePosition = throttle;
    inputs.ignitionOn = ignition;
    inputs.starterMotorEngaged = false;
    telemetryWriter->writeVehicleInputs(inputs);

    // Push simulator metrics
    telemetry::SimulatorMetricsTelemetry metrics;
    metrics.timestamp = currentTime;
    telemetryWriter->writeSimulatorMetrics(metrics);

    // Note: EngineStateTelemetry and FramePerformanceTelemetry are pushed
    // by BridgeSimulator::update() -- SRP/ISP compliance
    // Note: AudioDiagnostics and AudioTiming are pushed by strategies
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

// Initialize the simulator: create, set logging, load script or sine mode.
// Throws std::runtime_error on failure.
void initializeSimulator(
    ISimulator& simulator,
    const SimulationConfig& config,
    ILogging* logger,
    int sampleRate)
{
    ScriptConfig scriptConfig = prepareScriptConfig(config);
    if (!scriptConfig.valid) {
        throw std::runtime_error(scriptConfig.errorMessage);
    }

    logger->info(LogMask::BRIDGE, "Loading simulator: %s%s%s",
                        ANSIColors::CYAN.c_str(),
                        config.sineMode ? "[SINE]" : scriptConfig.scriptPath.c_str(),
                        ANSIColors::RESET.c_str());

    EngineSimConfig engineConfig = EngineConfig::createDefault(sampleRate, config.simulationFrequency);
    engineConfig.sineMode = config.sineMode ? 1 : 0;

    if (!simulator.create(engineConfig)) {
        throw std::runtime_error("Failed to create simulator: " + simulator.getLastError());
    }

    if (!simulator.setLogging(logger)) {
        logger->warning(LogMask::BRIDGE, "Failed to set logging");
    }

    if (!scriptConfig.scriptPath.empty()) {
        if (!simulator.loadScript(scriptConfig.scriptPath, scriptConfig.assetBasePath)) {
            throw std::runtime_error("Failed to load script: " + simulator.getLastError());
        }
    } else {
        // Sine mode or no script -- still need to initialize synthesizer
        if (!simulator.loadScript("", "")) {
            throw std::runtime_error("Failed to initialize synthesizer: " + simulator.getLastError());
        }
    }
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
    telemetry::ITelemetryReader* telemetryReader,
    ILogging* logger)
{
    double currentTime = 0.0;
    LoopTimer timer;

    const double minSustainedRPM = 550.0;

    double throttle = 0.1;
    bool ignition = true;

    logger->info(LogMask::BRIDGE, "runUnifiedAudioLoop starting simulation loop with %s mode", config.sineMode ? "SINE" : "ENGINE");

    while (true) {
        checkStarterMotorRPM(simulator, minSustainedRPM);

        // Poll input: interactive mode uses OnUpdateSimulation, timed mode uses duration check
        if (inputProvider) {
            auto engineInput = inputProvider->OnUpdateSimulation(AudioLoopConfig::UPDATE_INTERVAL);
            if (!engineInput) {
                break;  // Input provider signalled termination
            }
            throttle = engineInput->throttle;
            ignition = engineInput->ignition;
        } else {
            if (currentTime >= config.duration) {
                break;
            }
            auto timedInput = getTimedInput(currentTime);
            throttle = timedInput.throttle;
            ignition = timedInput.ignition;
        }

        simulator.setThrottle(throttle);
        simulator.setIgnition(ignition);

        // Update simulation via strategy (threaded mode updates here; sync-pull is no-op)
        audioStrategy.updateSimulation(&simulator, AudioLoopConfig::UPDATE_INTERVAL * 1000.0);

        EngineSimStats stats = simulator.getStats();

        // Generate audio: strategy decides whether to fill buffer (Threaded fills, SyncPull no-ops)
        audioStrategy.fillBufferFromEngine(&simulator, AudioLoopConfig::FRAMES_PER_UPDATE);

        writeTelemetry(telemetryWriter, currentTime, throttle, ignition);

        // Read underrun count from telemetry (pushed by ThreadedStrategy)
        int underrunCount = 0;
        if (telemetryReader) {
            auto audioDiag = telemetryReader->getAudioDiagnostics();
            underrunCount = audioDiag.underrunCount;
        }

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        // Display via presentation (ConsolePresentation formats the complete output line)
        updatePresentation(presentation, currentTime, stats, throttle, underrunCount, audioStrategy, inputProvider, telemetryReader);

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
    presentation::IPresentation* presentation,
    ILogging* logger)
{
    const int sampleRate = AudioLoopConfig::SAMPLE_RATE;

    ASSERT(audioStrategy, "audioStrategy must be provided");

    // Initialize simulator (throws on failure)
    initializeSimulator(simulator, config, logger, sampleRate);

    // Initialize strategy
    AudioStrategyConfig strategyConfig;
    strategyConfig.sampleRate = sampleRate;
    strategyConfig.channels = 2;

    if (!audioStrategy->initialize(strategyConfig)) {
        throw std::runtime_error("Failed to initialize audio strategy");
    }

    // Create and initialize audio hardware provider (throws on failure)
    auto callback = [audioStrategy](void* refCon, void* actionFlags,
                           const void* timeStamp, int busNumber, int numberFrames,
                           PlatformAudioBufferList* platformBufferList) -> int {
        (void)refCon;
        (void)actionFlags;
        (void)timeStamp;
        (void)busNumber;
        return audioRenderCallback(audioStrategy, numberFrames, platformBufferList);
    };

    auto hardwareProvider = createHardwareProvider(sampleRate, callback, logger);

    logger->info(LogMask::AUDIO, "Audio initialized: strategy=%s, sr=%d",
                         audioStrategy->getName(), sampleRate);

    // Start strategy playback
    if (!audioStrategy->startPlayback(&simulator)) {
        throw std::runtime_error("Failed to start audio playback");
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
        logger->error(LogMask::AUDIO, "Failed to start hardware playback");
    }

    int exitCode = runUnifiedAudioLoop(simulator, config, *audioStrategy, inputProvider, presentation, config.telemetryWriter, config.telemetryReader, logger);

    // Cleanup
    audioStrategy->stopPlayback(&simulator);
    cleanupSimulation(hardwareProvider.get(), simulator);

    warnWavExportNotSupported(config.outputWav, logger);

    return exitCode;
}
