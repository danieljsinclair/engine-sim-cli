// CoreAudioHardwareProvider.cpp - macOS CoreAudio implementation of IAudioHardwareProvider
// Wraps CoreAudio AudioUnit for macOS platform audio output
// Thread-safe callback handling with proper resource management

#include "audio/hardware/CoreAudioHardwareProvider.h"

#include <cstring>
#include <AudioUnit/AudioComponent.h>
#include <AudioUnit/AudioUnit.h>

// ================================================================
// CoreAudioHardwareProvider Implementation
// ================================================================

CoreAudioHardwareProvider::CoreAudioHardwareProvider(ILogging* logger)
    : audioUnit(nullptr),
      deviceID(0),
      isPlaying(false),
      currentVolume(1.0),
      underrunCount(0),
      overrunCount(0),
      defaultLogger_(logger ? nullptr : new ConsoleLogger()),
      logger_(logger ? logger : defaultLogger_.get()),
      audioCallback_(nullptr),
      context_(nullptr) {
}

CoreAudioHardwareProvider::~CoreAudioHardwareProvider() {
    cleanup();
}

bool CoreAudioHardwareProvider::initialize(const AudioStreamFormat& format) {
    logger_->info(LogMask::AUDIO, "CoreAudioHardwareProvider::initialize() - sr=%d, ch=%d",
                 format.sampleRate, format.channels);

    // Setup AudioUnit
    if (!setupAudioUnit()) {
        logger_->error(LogMask::AUDIO, "Failed to setup AudioUnit");
        return false;
    }

    // Configure audio format
    if (!configureAudioFormat(format)) {
        logger_->error(LogMask::AUDIO, "Failed to configure audio format");
        return false;
    }

    // Register callback
    if (!registerCallbackWithAudioUnit()) {
        logger_->error(LogMask::AUDIO, "Failed to register audio callback");
        return false;
    }

    // Initialize AudioUnit (required before AudioOutputUnitStart)
    OSStatus initStatus = AudioUnitInitialize(audioUnit);
    if (initStatus != noErr) {
        logCoreAudioError("AudioUnitInitialize", initStatus, nullptr);
        return false;
    }

    logger_->info(LogMask::AUDIO, "CoreAudioHardwareProvider initialized successfully");
    return true;
}

void CoreAudioHardwareProvider::cleanup() {
    if (audioUnit) {
        stopPlayback();

        // Uninitialize AudioUnit
        OSStatus status = AudioUnitUninitialize(audioUnit);
        if (status != noErr) {
            logger_->warning(LogMask::AUDIO, "AudioUnitUninitialize failed: %s",
                          getStatusDescription(status));
        }

        // Dispose AudioUnit
        status = AudioComponentInstanceDispose(audioUnit);
        if (status != noErr) {
            logger_->warning(LogMask::AUDIO, "AudioComponentInstanceDispose failed: %s",
                          getStatusDescription(status));
        }

        audioUnit = nullptr;
    }

    isPlaying = false;
    logger_->info(LogMask::AUDIO, "CoreAudioHardwareProvider cleaned up");
}

bool CoreAudioHardwareProvider::startPlayback() {
    if (!audioUnit) {
        logger_->error(LogMask::AUDIO, "Cannot start playback - AudioUnit not initialized");
        return false;
    }

    logger_->debug(LogMask::AUDIO, "Starting AudioUnit playback");

    OSStatus status = AudioOutputUnitStart(audioUnit);
    if (status != noErr) {
        logCoreAudioError("AudioOutputUnitStart", status, nullptr);
        return false;
    }

    isPlaying = true;
    logger_->info(LogMask::AUDIO, "AudioUnit playback started");
    return true;
}

void CoreAudioHardwareProvider::stopPlayback() {
    if (!audioUnit) {
        logger_->warning(LogMask::AUDIO, "Cannot stop playback - AudioUnit not initialized");
        return;
    }

    if (isPlaying) {
        logger_->debug(LogMask::AUDIO, "Stopping AudioUnit playback");

        OSStatus status = AudioOutputUnitStop(audioUnit);
        if (status != noErr) {
            logCoreAudioError("AudioOutputUnitStop", status, nullptr);
        }

        isPlaying = false;
        logger_->info(LogMask::AUDIO, "AudioUnit playback stopped");
    }
}

void CoreAudioHardwareProvider::setVolume(double volume) {
    if (!audioUnit) {
        logger_->warning(LogMask::AUDIO, "Cannot set volume - AudioUnit not initialized");
        return;
    }

    // Clamp volume to valid range
    double clampedVolume = std::max(0.0, std::min(1.0, volume));

    logger_->debug(LogMask::AUDIO, "Setting volume to %.2f", clampedVolume);

    // Set volume parameter on global scope
    OSStatus status = AudioUnitSetParameter(
        audioUnit,
        kHALOutputParam_Volume,
        kAudioUnitScope_Global,
        0,  // kAudioUnitElement_Output
        clampedVolume,
        0   // AudioUnitElement inBufferOffset
    );

    if (status != noErr) {
        logCoreAudioError("AudioUnitSetParameter (volume)", status, nullptr);
    } else {
        currentVolume = clampedVolume;
    }
}

double CoreAudioHardwareProvider::getVolume() const {
    return currentVolume;
}

bool CoreAudioHardwareProvider::registerAudioCallback(const AudioCallback& callback) {
    if (!callback) {
        logger_->error(LogMask::AUDIO, "Cannot register null callback");
        return false;
    }

    audioCallback_ = callback;
    logger_->info(LogMask::AUDIO, "Audio callback registered");

    // Callback registration happens in registerCallbackWithAudioUnit()
    return true;
}

AudioHardwareState CoreAudioHardwareProvider::getHardwareState() const {
    AudioHardwareState state;
    state.isInitialized = (audioUnit != nullptr);
    state.isPlaying = isPlaying;
    state.isCallbackActive = isPlaying;  // Callback is active when playing
    state.currentVolume = currentVolume;
    state.underrunCount = underrunCount;
    state.overrunCount = overrunCount;
    return state;
}

void CoreAudioHardwareProvider::resetDiagnostics() {
    underrunCount = 0;
    overrunCount = 0;
    logger_->debug(LogMask::AUDIO, "Hardware diagnostics reset");
}

// ================================================================
// Private Helper Methods
// ================================================================

bool CoreAudioHardwareProvider::setupAudioUnit() {
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    // Find default output AudioComponent
    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        logger_->error(LogMask::AUDIO, "Failed to find AudioComponent");
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audioUnit);

    if (status != noErr || !audioUnit) {
        logCoreAudioError("AudioComponentInstanceNew", status,
                         "Failed to create AudioUnit - system audio may be unavailable");
        return false;
    }

    return true;
}

bool CoreAudioHardwareProvider::configureAudioFormat(const AudioStreamFormat& format) {
    if (!audioUnit) {
        return false;
    }

    // Build AudioStreamBasicDescription
    AudioStreamBasicDescription streamFormat = {};
    streamFormat.mSampleRate = static_cast<Float64>(format.sampleRate);
    streamFormat.mFormatID = kAudioFormatLinearPCM;

    if (format.isFloat) {
        streamFormat.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
        streamFormat.mBitsPerChannel = format.bitsPerSample;
        streamFormat.mFramesPerPacket = 1;
        streamFormat.mBytesPerPacket = format.channels * sizeof(float);
        streamFormat.mBytesPerFrame = format.channels * sizeof(float);
    } else {
        // Integer format support (not currently used but provided for completeness)
        streamFormat.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
        streamFormat.mBitsPerChannel = format.bitsPerSample;
        streamFormat.mFramesPerPacket = 1;
        streamFormat.mBytesPerPacket = (format.bitsPerSample / 8) * format.channels;
        streamFormat.mBytesPerFrame = (format.bitsPerSample / 8) * format.channels;
    }

    streamFormat.mChannelsPerFrame = format.channels;

    // Set format on AudioUnit
    OSStatus status = AudioUnitSetProperty(
        audioUnit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0,  // kAudioUnitElement_Output
        &streamFormat,
        sizeof(streamFormat)
    );

    if (status != noErr) {
        logCoreAudioError("AudioUnitSetProperty (format)", status,
                         "44100Hz stereo float not supported");
        return false;
    }

    return true;
}

bool CoreAudioHardwareProvider::registerCallbackWithAudioUnit() {
    if (!audioUnit || !audioCallback_) {
        return false;
    }

    // Set up callback structure
    AURenderCallbackStruct callbackStruct;
    callbackStruct.inputProc = &coreAudioCallbackWrapper;
    callbackStruct.inputProcRefCon = this;  // Pass 'this' to static callback wrapper

    OSStatus status = AudioUnitSetProperty(
        audioUnit,
        kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input,
        0,  // kAudioUnitElement_Output
        &callbackStruct,
        sizeof(callbackStruct)
    );

    if (status != noErr) {
        logCoreAudioError("AudioUnitSetProperty (callback)", status);
        return false;
    }

    return true;
}

OSStatus CoreAudioHardwareProvider::coreAudioCallbackWrapper(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 numberFrames,
    AudioBufferList* ioData
) {
    // Extract the CoreAudioHardwareProvider instance from refCon
    CoreAudioHardwareProvider* provider = static_cast<CoreAudioHardwareProvider*>(refCon);

    if (!provider || !provider->audioCallback_) {
        return noErr;  // Should not happen if properly initialized
    }

    // Convert CoreAudio buffer structure to our platform-agnostic structure
    PlatformAudioBufferList bufferList;
    bufferList.numberBuffers = ioData->mNumberBuffers;
    bufferList.buffers = ioData->mBuffers;  // This is the array pointer
    bufferList.bufferSizes = nullptr;  // Not needed for our use case
    bufferList.bufferChannels = nullptr;  // Not needed for our use case
    bufferList.bufferData = reinterpret_cast<void**>(ioData->mBuffers);  // Get pointer to buffer data

    // Suppress CoreAudio parameters we don't use
    (void)actionFlags;
    (void)timeStamp;
    (void)busNumber;

    // Invoke the user-provided callback
    return static_cast<OSStatus>(
        provider->audioCallback_(refCon, actionFlags, timeStamp, busNumber, numberFrames, &bufferList)
    );
}

const char* CoreAudioHardwareProvider::getStatusDescription(OSStatus status) {
    switch (status) {
        case noErr: return "no error";
        case kAudioUnitErr_FormatNotSupported: return "format not supported";
        case kAudioUnitErr_Initialized: return "already initialized";
        case kAudioUnitErr_InvalidParameter: return "invalid parameter";
        case kAudioUnitErr_InvalidProperty: return "invalid property";
        case kAudioUnitErr_InvalidElement: return "invalid element"; return "invalid element";
        case kAudioUnitErr_NoConnection: return "no connection";
        case kAudioUnitErr_Uninitialized: return "hardware uninitialized";
        default:
            return "unknown error";
    }
}

void CoreAudioHardwareProvider::logCoreAudioError(const char* operation, OSStatus status, const char* additional) {
    const char* description = getStatusDescription(status);

    if (additional) {
        logger_->error(LogMask::AUDIO, "CoreAudio error in %s: %s (%d) - %s",
                      operation, description, status, additional);
    } else {
        logger_->error(LogMask::AUDIO, "CoreAudio error in %s: %s (%d)",
                      operation, description, status);
    }
}
