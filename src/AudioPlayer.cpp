// AudioPlayer.cpp - Audio playback implementation
// SRP: Orchestrates audio playback using injected strategy and hardware provider
// OCP: Strategy pattern - no boolean flags, no mode switching
// DI: Strategy and hardware provider are injected

#include "AudioPlayer.h"
#include "audio/hardware/CoreAudioHardwareProvider.h"

#include <cstring>
#include <algorithm>

// ============================================================================
// AudioPlayer Implementation
// ============================================================================

AudioPlayer::AudioPlayer(IAudioStrategy* strategy, ILogging* logger)
    : strategy_(strategy)
    , defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
    , isPlaying_(false)
    , sampleRate_(0)
{
    // Wire context's circular buffer pointer to our owned buffer
    context_.circularBuffer = &circularBuffer_;
    context_.strategy = strategy_;
}

AudioPlayer::~AudioPlayer() {
    cleanup();
}

bool AudioPlayer::initialize(int sampleRate, EngineSimHandle handle, const EngineSimAPI* api) {
    sampleRate_ = sampleRate;

    // Set engine handles on context
    context_.engineHandle = handle;
    context_.engineAPI = api;

    if (!strategy_) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::initialize: No strategy injected");
        return false;
    }

    // Initialize the strategy with context and config
    AudioStrategyConfig config;
    config.sampleRate = sampleRate;
    config.channels = 2;

    if (!strategy_->initialize(&context_, config)) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::initialize: Strategy initialization failed");
        return false;
    }

    // Create and initialize hardware provider
    hardwareProvider_ = std::make_unique<CoreAudioHardwareProvider>(logger_);

    // Register our audio callback with the hardware provider BEFORE initialize()
    // The callback bridges from PlatformAudioBufferList to AudioBufferList for the strategy
    auto callback = [this](void* refCon, void* actionFlags,
                           const void* timeStamp, int busNumber, int numberFrames,
                           PlatformAudioBufferList* platformBufferList) -> int {
        (void)refCon;
        (void)actionFlags;
        (void)timeStamp;
        (void)busNumber;

        if (!context_.audioState.isPlaying.load()) {
            // Output silence when not playing
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

        // Reconstruct AudioBufferList from PlatformAudioBufferList
        // The CoreAudioHardwareProvider stores the original AudioBuffer array in buffers field
        if (platformBufferList && platformBufferList->buffers && context_.strategy) {
            AudioBuffer* audioBuffers = static_cast<AudioBuffer*>(platformBufferList->buffers);
            AudioBufferList bufferList;
            bufferList.mNumberBuffers = static_cast<UInt32>(platformBufferList->numberBuffers);
            for (int i = 0; i < platformBufferList->numberBuffers; i++) {
                bufferList.mBuffers[i] = audioBuffers[i];
            }
            context_.strategy->render(&context_, &bufferList, numberFrames);
        }

        return 0;
    };

    // Register callback BEFORE initialize (initialize calls registerCallbackWithAudioUnit)
    hardwareProvider_->registerAudioCallback(callback);

    AudioStreamFormat format;
    format.sampleRate = sampleRate;
    format.channels = 2;
    format.bitsPerSample = 32;
    format.isFloat = true;
    format.isInterleaved = true;

    if (!hardwareProvider_->initialize(format)) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::initialize: Hardware provider initialization failed");
        return false;
    }

    logger_->info(LogMask::AUDIO, "AudioPlayer initialized: strategy=%s, sr=%d",
                  strategy_->getName(), sampleRate);

    return true;
}

bool AudioPlayer::start() {
    if (isPlaying_) {
        return false;
    }

    context_.audioState.isPlaying.store(true);

    if (!hardwareProvider_->startPlayback()) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::start: Hardware startPlayback failed");
        context_.audioState.isPlaying.store(false);
        return false;
    }

    isPlaying_ = true;
    return true;
}

void AudioPlayer::stop() {
    if (!isPlaying_) {
        return;
    }

    if (hardwareProvider_) {
        hardwareProvider_->stopPlayback();
    }

    context_.audioState.isPlaying.store(false);
    isPlaying_ = false;
}

void AudioPlayer::setVolume(float volume) {
    if (hardwareProvider_) {
        hardwareProvider_->setVolume(volume);
    }
}

void AudioPlayer::setEngineHandle(EngineSimHandle handle) {
    context_.engineHandle = handle;
}

void AudioPlayer::addToCircularBuffer(const float* samples, int frameCount) {
    if (!samples || frameCount <= 0) {
        return;
    }
    strategy_->AddFrames(&context_, const_cast<float*>(samples), frameCount);
}

void AudioPlayer::resetCircularBuffer() {
    circularBuffer_.reset();
    context_.bufferState.readPointer.store(0);

    // Pre-fill with 100ms of silence for cursor-chasing warmup
    int prefillFrames = static_cast<int>(sampleRate_ * 0.1);
    context_.bufferState.writePointer.store(prefillFrames);
}

int AudioPlayer::calculateCursorChasingSamples(int defaultFrames) {
    size_t available = circularBuffer_.available();
    int bufferSize = static_cast<int>(circularBuffer_.capacity());

    int targetLead = static_cast<int>(sampleRate_ * 0.1);

    int framesToWrite;
    if (static_cast<int>(available) < targetLead) {
        framesToWrite = defaultFrames + (targetLead - static_cast<int>(available));
    } else if (static_cast<int>(available) > targetLead * 2) {
        framesToWrite = std::max(defaultFrames - (static_cast<int>(available) - targetLead), 0);
    } else {
        framesToWrite = defaultFrames;
    }

    constexpr int MAX_FRAMES_PER_READ = 4096;
    framesToWrite = std::min(framesToWrite, MAX_FRAMES_PER_READ);

    return std::min(framesToWrite, bufferSize);
}

BufferContext* AudioPlayer::getContext() {
    return &context_;
}

const BufferContext* AudioPlayer::getContext() const {
    return &context_;
}

void AudioPlayer::getBufferDiagnostics(int& writePtr, int& readPtr, int& available, int& status) {
    writePtr = context_.bufferState.writePointer.load();
    readPtr = context_.bufferState.readPointer.load();

    int bufferSize = static_cast<int>(circularBuffer_.capacity());
    if (writePtr >= readPtr) {
        available = writePtr - readPtr;
    } else {
        available = (bufferSize - readPtr) + writePtr;
    }

    status = 0;
}

void AudioPlayer::resetBufferDiagnostics() {
    context_.bufferState.underrunCount.store(0);
}

void AudioPlayer::waitForCompletion() {
    // For sync-pull mode, there's no background thread
    // For threaded mode, would need additional synchronization
}

void AudioPlayer::cleanup() {
    stop();

    if (hardwareProvider_) {
        hardwareProvider_->cleanup();
        hardwareProvider_.reset();
    }
}

// ============================================================================
// Static Callback for real-time audio rendering
// Delegates to the injected IAudioStrategy
// ============================================================================

OSStatus AudioPlayer::audioUnitCallback(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 numberFrames,
    AudioBufferList* ioData
) {
    auto* ctx = static_cast<BufferContext*>(refCon);

    (void)actionFlags;
    (void)timeStamp;
    (void)busNumber;

    if (!ctx || !ctx->audioState.isPlaying.load()) {
        // Output silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            std::memset(buffer.mData, 0, buffer.mDataByteSize);
        }
        return noErr;
    }

    // Delegate to the strategy for rendering
    if (ctx->strategy) {
        ctx->strategy->render(ctx, ioData, numberFrames);
    }

    return noErr;
}
