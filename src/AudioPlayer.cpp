// AudioPlayer.cpp - Audio playback facade
// DIP: Strategy and hardware provider are injected

#include "AudioPlayer.h"

#include <cstring>

// ============================================================================
// AudioPlayer Implementation
// ============================================================================

AudioPlayer::AudioPlayer(IAudioStrategy* strategy,
                         std::unique_ptr<IAudioHardwareProvider> hardwareProvider,
                         ILogging* logger)
    : hardwareProvider_(std::move(hardwareProvider))
    , strategy_(strategy)
    , defaultLogger_(logger ? nullptr : new ConsoleLogger())
    , logger_(logger ? logger : defaultLogger_.get())
{
    context_.circularBuffer = &circularBuffer_;
}

AudioPlayer::~AudioPlayer() {
    cleanup();
}

bool AudioPlayer::initialize(int sampleRate, EngineSimHandle handle, const EngineSimAPI* api) {
    sampleRate_ = sampleRate;

    if (!strategy_) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::initialize: No strategy injected");
        return false;
    }

    if (!hardwareProvider_) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::initialize: No hardware provider injected");
        return false;
    }

    // Initialize the strategy with context and config
    AudioStrategyConfig config;
    config.sampleRate = sampleRate;
    config.channels = 2;
    config.engineHandle = handle;
    config.engineAPI = api;

    if (!strategy_->initialize(&context_, config)) {
        logger_->error(LogMask::AUDIO, "AudioPlayer::initialize: Strategy initialization failed");
        return false;
    }

    // Register audio callback that bridges platform buffers -> strategy->render()
    auto callback = [this](void* refCon, void* actionFlags,
                           const void* timeStamp, int busNumber, int numberFrames,
                           PlatformAudioBufferList* platformBufferList) -> int {
        (void)refCon;
        (void)actionFlags;
        (void)timeStamp;
        (void)busNumber;

        if (!context_.audioState.isPlaying.load()) {
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

        if (platformBufferList && platformBufferList->buffers && strategy_) {
            AudioBuffer* audioBuffers = static_cast<AudioBuffer*>(platformBufferList->buffers);
            AudioBufferList bufferList;
            bufferList.mNumberBuffers = static_cast<UInt32>(platformBufferList->numberBuffers);
            for (int i = 0; i < platformBufferList->numberBuffers; i++) {
                bufferList.mBuffers[i] = audioBuffers[i];
            }
            strategy_->render(&context_, &bufferList, numberFrames);
        }

        return 0;
    };

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

BufferContext* AudioPlayer::getContext() {
    return &context_;
}

const BufferContext* AudioPlayer::getContext() const {
    return &context_;
}

void AudioPlayer::cleanup() {
    stop();

    if (hardwareProvider_) {
        hardwareProvider_->cleanup();
        hardwareProvider_.reset();
    }
}
