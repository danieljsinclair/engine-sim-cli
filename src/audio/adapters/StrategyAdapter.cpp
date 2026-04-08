// StrategyAdapter.cpp - Implementation of StrategyAdapter
// Bridges IAudioStrategy to IAudioRenderer interface for gradual migration

#include "StrategyAdapter.h"
#include "audio/common/CircularBuffer.h"
#include "AudioPlayer.h"
#include "AudioSource.h"  // For IAudioSource
#include "simulation/SimulationLoop.h"  // For SimulationConfig
#include "config/CLIconfig.h"  // For AudioLoopConfig
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"
#include "ILogging.h"
#include <cstring>  // For strcmp

// Forward declaration for AudioUnitContext
struct AudioUnitContext;

// Include strategy headers for dynamic_cast
#include "audio/strategies/SyncPullStrategy.h"
#include "audio/strategies/ThreadedStrategy.h"

StrategyAdapter::StrategyAdapter(
    std::unique_ptr<IAudioStrategy> strategy,
    std::unique_ptr<StrategyContext> context,
    ILogging* logger)
    : strategy_(std::move(strategy))
    , context_(std::move(context))
    , sampleRate_(0)
    , engineHandle_(nullptr)
    , engineAPI_(nullptr)
    , mockContext_(nullptr)
    , logger_(logger) {
}

std::string StrategyAdapter::getModeName() const {
    return std::string(strategy_->getName());
}

void StrategyAdapter::updateSimulation(EngineSimHandle handle, const EngineSimAPI& api,
                                   AudioPlayer* audioPlayer) {
    // Threaded mode: generates audio separately, but simulation still needs updates
    // Sync-pull mode: generates audio on-demand via RenderOnDemand() which updates simulation
    // Only call api.Update() for threaded mode to avoid race condition
    if (strategy_ && strcmp(strategy_->getName(), "Threaded") == 0) {
        api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);
    }
    (void)audioPlayer;
}

void StrategyAdapter::generateAudio(IAudioSource& audioSource, AudioPlayer* audioPlayer) {
    // Generate audio and write to circular buffer for threaded mode
    // Sync-pull mode generates audio on-demand in the render callback
    if (!audioPlayer || !audioPlayer->getContext()) {
        return;
    }

    // Only threaded strategy needs audio generation in main loop
    if (strcmp(strategy_->getName(), "Threaded") != 0) {
        return;  // Sync-pull mode renders on-demand, no buffer filling needed
    }

    // Calculate how many frames to write based on buffer level
    int framesToWrite = audioPlayer->calculateCursorChasingSamples(AudioLoopConfig::FRAMES_PER_UPDATE);
    if (framesToWrite <= 0) {
        return;
    }

    // Generate audio using the audio source
    std::vector<float> audioBuffer(framesToWrite * 2);  // Stereo
    if (audioSource.generateAudio(audioBuffer, framesToWrite)) {
        // Write to circular buffer via AddFrames (delegates to strategy)
        AddFrames(audioPlayer->getContext(), audioBuffer.data(), framesToWrite);
    }
}

std::string StrategyAdapter::getModeString() const {
    return strategy_->getModeString();
}

bool StrategyAdapter::startAudioThread(EngineSimHandle handle, const EngineSimAPI& api,
                                   AudioPlayer* audioPlayer) {
    // Only threaded strategy needs the engine's audio thread started
    // SyncPullStrategy renders on-demand and should NOT start the audio thread
    if (strcmp(strategy_->getName(), "Threaded") != 0) {
        // Sync-pull mode: no audio thread needed (renders on-demand)
        return true;
    }

    // Threaded mode: start the engine simulator's audio thread
    // This thread continuously fills audio buffers that ThreadedStrategy reads from
    EngineSimResult result = api.StartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        if (logger_) {
            logger_->error(LogMask::THREADED_AUDIO, "StrategyAdapter::startAudioThread: Failed to start audio thread");
        }
        return false;
    }

    if (logger_) {
        logger_->info(LogMask::THREADED_AUDIO, "StrategyAdapter::startAudioThread: Engine audio thread started for ThreadedStrategy");
    }

    (void)audioPlayer;  // Not needed for thread startup
    return true;
}

void StrategyAdapter::prepareBuffer(AudioPlayer* audioPlayer) {
    // For threaded strategy, prepare buffer and start audio playback
    // For sync-pull, this is a no-op since audio is rendered on-demand
    if (strategy_ && audioPlayer) {
        // Check if this is a threaded strategy that needs buffer preparation
        if (strcmp(strategy_->getName(), "Threaded") == 0) {
            // The strategy needs to initialize its buffer state
            context_->bufferState.writePointer.store(0);
            context_->bufferState.readPointer.store(0);
            context_->audioState.isPlaying.store(false);

            // Note: Actual AudioUnit start will happen in AudioPlayer's prepareBuffer
            // This method is called during strategy initialization
        }
    }
}

void StrategyAdapter::resetBufferAfterWarmup(AudioPlayer* audioPlayer) {
    // For threaded strategy, reset buffer state after warmup
    // For sync-pull, this is a no-op
    (void)audioPlayer;

    if (strategy_ && context_) {
        if (strcmp(strategy_->getName(), "Threaded") == 0) {
            // Reset buffer pointers for threaded mode
            context_->bufferState.writePointer.store(0);
            context_->bufferState.readPointer.store(0);
        }
    }
}

void StrategyAdapter::startPlayback(AudioPlayer* audioPlayer) {
    // Start audio playback - this should trigger the AudioUnit
    if (audioPlayer) {
        // CRITICAL: Actually start the AudioUnit via AudioPlayer::start()
        // This is required for the audio callback to be invoked
        audioPlayer->start();

        // Update StrategyContext state
        context_->audioState.isPlaying.store(true);

        // CRITICAL: Also update mock context's isPlaying flag
        // Audio callback checks this flag, not StrategyContext's flag
        if (mockContext_) {
            mockContext_->isPlaying.store(true);
        }
    }
}

bool StrategyAdapter::shouldDrainDuringWarmup() const {
    return strategy_->shouldDrainDuringWarmup();
}

void StrategyAdapter::configure(const SimulationConfig& config) {
    // Note: sampleRate_ is already set in createContext()
    // We don't need to read sampleRate from config parameter
    (void)config;  // Suppress unused parameter warning

    // Create a minimal config for IAudioStrategy directly
    // The IAudioStrategy configure() expects sampleRate and channels
    AudioStrategyConfig strategyConfig;
    strategyConfig.sampleRate = sampleRate_;
    strategyConfig.channels = 2;  // Stereo

    // Configure the strategy with the config it expects
    strategy_->configure(strategyConfig);

    // Configure the context
    context_->audioState.sampleRate = sampleRate_;
    context_->audioState.isPlaying = false;
    context_->bufferState.capacity = sampleRate_ * 2;  // 2 seconds buffer
    context_->engineHandle = engineHandle_;
    context_->engineAPI = engineAPI_;

    // Set AudioUnitContext in strategy for metrics updates
    // This allows sync-pull diagnostics to display correctly
    // Simplified approach: use getName() to check strategy type
    const char* strategyName = strategy_->getName();
    if (std::strcmp(strategyName, "SyncPull") == 0) {
        auto* syncPullStrategy = static_cast<SyncPullStrategy*>(strategy_.get());
        if (mockContext_) {
            syncPullStrategy->setAudioUnitContext(mockContext_);
        }
    }

    // NOTE: Circular buffer is owned by AudioUnitContext (mockContext)
    // StrategyContext has a non-owning pointer to the same buffer
    // The buffer is initialized in createMockContext
}

std::unique_ptr<AudioUnitContext> StrategyAdapter::createContext(
    int sampleRate,
    EngineSimHandle engineHandle,
    const EngineSimAPI* engineAPI
) {
    sampleRate_ = sampleRate;
    engineHandle_ = engineHandle;
    engineAPI_ = engineAPI;

    // Create mock AudioUnitContext that bridges to StrategyContext
    return createMockContext(sampleRate, engineHandle, engineAPI);
}

bool StrategyAdapter::render(
    void* context,
    AudioBufferList* ioData,
    UInt32 numberFrames
) {
    // Use the StrategyContext stored in adapter (ignore passed context)
    return strategy_->render(context_.get(), ioData, numberFrames);
}

bool StrategyAdapter::isEnabled() const {
    return strategy_->isEnabled();
}

const char* StrategyAdapter::getName() const {
    return strategy_->getName();
}

bool StrategyAdapter::AddFrames(void* context, float* buffer, int frameCount) {
    // Use the StrategyContext stored in adapter (ignore passed context)
    return strategy_->AddFrames(context_.get(), buffer, frameCount);
}

std::unique_ptr<AudioUnitContext> StrategyAdapter::createMockContext(
    int sampleRate,
    EngineSimHandle engineHandle,
    const EngineSimAPI* engineAPI
) {
    // Create a mock AudioUnitContext that bridges to StrategyContext
    // This allows existing AudioPlayer code to work with new architecture

    auto mockContext = std::make_unique<AudioUnitContext>();
    mockContext->engineHandle = engineHandle;
    mockContext->engineAPI = engineAPI;
    mockContext->isPlaying = context_->audioState.isPlaying.load();
    mockContext->writePointer = context_->bufferState.writePointer.load();
    mockContext->readPointer = context_->bufferState.readPointer.load();
    mockContext->underrunCount = context_->bufferState.underrunCount.load();
    mockContext->bufferStatus = 0;  // Use diagnostics from StrategyContext
    mockContext->totalFramesRead = 0;
    mockContext->sampleRate = sampleRate;

    // Create and initialize circular buffer, owned by mock context (AudioUnitContext)
    mockContext->circularBuffer = std::make_unique<CircularBuffer>();
    mockContext->circularBuffer->initialize(sampleRate * 2);

    // Set non-owning pointer in StrategyContext for strategy access
    context_->circularBuffer = mockContext->circularBuffer.get();

    // Set this adapter as the audioRenderer for the mock context
    mockContext->audioRenderer = this;

    // Store non-owning pointer to mock context for isPlaying updates
    mockContext_ = mockContext.get();

    return mockContext;
}
