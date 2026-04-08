// AudioPlayer.cpp - Audio playback implementation
// Refactored to use injected IAudioRenderer (SOLID: DI, OCP, SRP)
// OCP: Uses renderer strategy, no boolean flags

#include "AudioPlayer.h"
#include "audio/renderers/IAudioRenderer.h"
#include "audio/common/CircularBuffer.h"
#include "SyncPullAudio.h"
#include "config/ANSIColors.h"

#include "audio/renderers/SyncPullRenderer.h"
#include "audio/renderers/ThreadedRenderer.h"

#include <cstring>
#include <cmath>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <stdexcept>

// ============================================================================
// AudioPlayer Class Implementation - Uses injected IAudioRenderer (DI)
// OCP: Uses Strategy pattern for rendering - no boolean flags
// SRP: Rendering logic is in IAudioRenderer, not AudioPlayer
// DI: Renderer is injected via constructor or obtained from context
// ============================================================================

// Constructor with optional injected IAudioRenderer and logger (DI pattern)
// If provided, can override the renderer; otherwise context's renderer is used
AudioPlayer::AudioPlayer(IAudioRenderer* renderer, ILogging* logger)
    : audioUnit(nullptr), deviceID(0),
      isPlaying(false), sampleRate(0),
      context(nullptr), renderer(renderer),
      defaultLogger_(logger ? nullptr : new ConsoleLogger()),
      logger_(logger ? logger : defaultLogger_.get()) {
}

AudioPlayer::~AudioPlayer() {
    cleanup();
}

bool AudioPlayer::initialize(IAudioRenderer& audioRenderer, int sr, EngineSimHandle handle, const EngineSimAPI* api) {
    sampleRate = sr;

    // Use IAudioRenderer to create the appropriate context
    // DI: IAudioRenderer injects itself into the context
    context = audioRenderer.createContext(sr, handle, api).release();

    if (!context) {
        logger_->error(LogMask::AUDIO, "Failed to create audio context");
        return false;
    }

    // OCP: No conditional branching - use injected renderer from constructor
    // if provided, otherwise use the one already injected by IAudioRenderer into context
    if (renderer) {
        // Constructor-provided renderer takes precedence
        context->setRenderer(renderer);
        logger_->info(LogMask::AUDIO, "Using constructor-injected renderer: %s", renderer->getName());
    } else if (!context->audioRenderer) {
        // Hard error: no renderer configured - must have a valid renderer
        throw std::runtime_error("FATAL: No audio renderer injected - IAudioRenderer is required");
    }

    if (!setupAudioUnit()) {
        delete context;
        context = nullptr;
        return false;
    }

    logger_->info(LogMask::AUDIO, "Initialized via factory: %s", audioRenderer.getModeName().c_str());
    return true;
}

bool AudioPlayer::setupAudioUnit() {
    AudioStreamBasicDescription format = {};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = 2 * sizeof(float);  // Stereo
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 2 * sizeof(float);   // Stereo float
    format.mChannelsPerFrame = 2;                 // Stereo
    format.mBitsPerChannel = 8 * sizeof(float);

    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        logger_->error(LogMask::AUDIO, "Failed to find AudioComponent");
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
    if (status != noErr) {
        logger_->error(LogMask::AUDIO, "Failed to create AudioUnit (OSStatus=%d) - system audio may be unavailable", status);
        return false;
    }

    status = AudioUnitSetProperty(
        audioUnit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0,
        &format,
        sizeof(format)
    );

    if (status != noErr) {
        logger_->error(LogMask::AUDIO, "Failed to set AudioUnit format (OSStatus=%d) - 44100 Hz stereo not supported", status);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = nullptr;
        return false;
    }

    // Set up callback with context
    AURenderCallbackStruct callbackStruct = {};
    callbackStruct.inputProc = audioUnitCallback;
    callbackStruct.inputProcRefCon = context;

    status = AudioUnitSetProperty(
        audioUnit,
        kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input,
        0,
        &callbackStruct,
        sizeof(callbackStruct)
    );

    if (status != noErr) {
        logger_->error(LogMask::AUDIO, "Failed to set callback (OSStatus=%d)", status);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = nullptr;
        return false;
    }

    status = AudioUnitInitialize(audioUnit);
    if (status != noErr) {
        const char* reason = "unavailable or misconfigured";
        if (status == kAudioUnitErr_FormatNotSupported) reason = "format not supported";
        else if (status == -66681) reason = "format changed during initialization";
        logger_->error(LogMask::AUDIO, "Failed to initialize AudioUnit (OSStatus=%d): %s", status, reason);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = nullptr;
        return false;
    }

    return true;
}

void AudioPlayer::cleanup() {
    if (audioUnit) {
        AudioOutputUnitStop(audioUnit);
        AudioUnitUninitialize(audioUnit);
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = nullptr;
    }
    
    if (context) {
        delete context;
        context = nullptr;
    }
}

void AudioPlayer::setEngineHandle(EngineSimHandle handle) {
    if (context) {
        context->engineHandle = handle;
    }
}

bool AudioPlayer::start() {
    if (!audioUnit || isPlaying) {
        return false;
    }
    
    if (context) {
        context->isPlaying.store(true);
    }
    
    OSStatus status = AudioOutputUnitStart(audioUnit);
    if (status != noErr) {
        const char* reason = "unknown error";
        switch (status) {
            case kAudioUnitErr_Uninitialized:
                reason = "not initialized properly";
                break;
            case kAudioUnitErr_InvalidParameter:
                reason = "invalid parameter or configuration";
                break;
            case kAudioHardwareNotRunningError:
                reason = "audio hardware stopped (try: sudo killall coreaudiod)";
                break;
            case kAudioHardwareBadDeviceError:
                reason = "device unavailable (unplugged/changed during startup?)";
                break;
            case -66671:
                reason = "another app has exclusive access (close other audio apps)";
                break;
            case -66681:
                reason = "device format changed (sample rate mismatch?)";
                break;
            case kAudioHardwareUnspecifiedError:
                reason = "unspecified hardware error (device busy/unavailable)";
                break;
        }
        logger_->error(LogMask::AUDIO, "Failed to start AudioUnit (OSStatus=%d): %s", status, reason);

        if (context) {
            context->isPlaying.store(false);
        }
        return false;
    }
    
    isPlaying = true;
    return true;
}

void AudioPlayer::stop() {
    if (!isPlaying) {
        return;
    }
    
    if (audioUnit) {
        AudioOutputUnitStop(audioUnit);
    }
    
    if (context) {
        context->isPlaying.store(false);
    }
    
    isPlaying = false;
}

void AudioPlayer::setVolume(float volume) {
    if (!audioUnit) {
        return;
    }
    // Clamp to valid range
    volume = std::max(0.0f, std::min(1.0f, volume));
    OSStatus status = AudioUnitSetParameter(
        audioUnit,
        kHALOutputParam_Volume,
        kAudioUnitScope_Global,
        0,      // element (0 = main output)
        volume,
        0       // bufferOffset (ignored for global param)
    );
    if (status != noErr) {
        logger_->warning(LogMask::AUDIO, "Failed to set volume: %d", status);
    }
}

bool AudioPlayer::playBuffer(const float* data, int frames, int sr) {
    // SOLID: No branching - delegate to renderer via AddFrames()
    // OCP: New renderers can be added without changing this code
    if (!context || !context->audioRenderer) {
        logger_->error(LogMask::AUDIO, "No audio context or renderer available");
        return false;
    }
    
    // Delegate to the injected renderer
    return context->audioRenderer->AddFrames(context, const_cast<float*>(data), frames);
}

void AudioPlayer::waitForCompletion() {
    // For sync-pull mode, there's no background thread
    // For threaded mode, would need additional synchronization
}

void AudioPlayer::addToCircularBuffer(const float* samples, int frameCount) {
    if (!context || !context->circularBuffer) {
        return;
    }
    
    size_t framesWritten = context->circularBuffer->write(samples, frameCount);
    
    int writePtr = context->writePointer.load();
    int newWritePtr = (writePtr + static_cast<int>(framesWritten)) % 
                      static_cast<int>(context->circularBuffer->capacity());
    context->writePointer.store(newWritePtr);
}

AudioUnitContext* AudioPlayer::getContext() {
    return context;
}

const AudioUnitContext* AudioPlayer::getContext() const {
    return context;
}

void AudioPlayer::getBufferDiagnostics(int& writePtr, int& readPtr, int& available, int& status) {
    if (!context) {
        writePtr = readPtr = available = status = 0;
        return;
    }
    
    writePtr = context->writePointer.load();
    readPtr = context->readPointer.load();
    
    int bufferSize = static_cast<int>(context->circularBuffer->capacity());
    if (writePtr >= readPtr) {
        available = writePtr - readPtr;
    } else {
        available = (bufferSize - readPtr) + writePtr;
    }
    
    status = context->bufferStatus;
}

void AudioPlayer::resetBufferDiagnostics() {
    if (context) {
        context->underrunCount.store(0);
        context->bufferStatus = 0;
    }
}

void AudioPlayer::resetCircularBuffer() {
    if (context && context->circularBuffer) {
        context->circularBuffer->reset();
        context->readPointer.store(0);
        
        // Pre-fill with 100ms of silence for cursor-chasing warmup
        int prefillFrames = static_cast<int>(context->sampleRate * 0.1);
        context->writePointer.store(prefillFrames);
    }
}

int AudioPlayer::calculateCursorChasingSamples(int defaultFrames) {
    if (!context || !context->circularBuffer) {
        return defaultFrames;
    }
    
    int bufferSize = static_cast<int>(context->circularBuffer->capacity());
    int writePtr = context->writePointer.load();
    int readPtr = context->readPointer.load();
    
    int available;
    if (writePtr >= readPtr) {
        available = writePtr - readPtr;
    } else {
        available = (bufferSize - readPtr) + writePtr;
    }
    
    // Target: maintain ~100ms lead
    int targetLead = static_cast<int>(context->sampleRate * 0.1);
    
    if (available < targetLead) {
        // Behind target - request more frames
        return std::min(defaultFrames + (targetLead - available), bufferSize);
    } else if (available > targetLead * 2) {
        // Too far ahead - reduce frames
        return std::max(defaultFrames - (available - targetLead), 0);
    }
    
    return defaultFrames;
}

// ============================================================================
// Helper function - DRY
// ============================================================================
static OSStatus renderSilence(AudioBufferList* ioData) {
    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);
        std::memset(data, 0, buffer.mDataByteSize);
    }
    return noErr;
}

// ============================================================================
// Static Callback for real-time audio rendering
// OCP: New rendering modes can be added without modifying this function
// SRP: Delegates to injected IAudioRenderer strategy
// ============================================================================

OSStatus AudioPlayer::audioUnitCallback(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 numberFrames,
    AudioBufferList* ioData
) {
    // Retrieve the audio context from the referencecon
    AudioUnitContext* ctx = static_cast<AudioUnitContext*>(refCon);

    // Suppress unused parameter warnings (required by AudioUnit API)
    (void)actionFlags;
    (void)timeStamp;
    (void)busNumber;

    // Early exit: if context is null or playback is stopped, output silence
    // This prevents audio glitches when the player is stopped
    if (!ctx || !ctx->isPlaying.load()) {
        return renderSilence(ioData);
    }

    // strategy pattern - initialize() throws if null
    ctx->audioRenderer->render(ctx, ioData, numberFrames);

    return noErr;
}
