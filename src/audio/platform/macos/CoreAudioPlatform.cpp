// CoreAudioPlatform.cpp - macOS audio platform implementation

#include "CoreAudioPlatform.h"

#include <iostream>
#include <algorithm>
#include <cstring>

namespace audio {
namespace platform {
namespace macos {

CoreAudioPlatform::CoreAudioPlatform()
    : audioUnit_(nullptr)
    , deviceID_(0)
    , audioSource_(nullptr)
    , sampleRate_(0)
    , framesPerBuffer_(0)
    , isPlaying_(false)
    , isInitialized_(false)
{
}

CoreAudioPlatform::~CoreAudioPlatform() {
    cleanup();
}

bool CoreAudioPlatform::initialize(int sampleRate, int framesPerBuffer) {
    sampleRate_ = sampleRate;
    framesPerBuffer_ = framesPerBuffer;

    if (!setupAudioUnit()) {
        return false;
    }

    isInitialized_ = true;
    return true;
}

bool CoreAudioPlatform::setupAudioUnit() {
    AudioStreamBasicDescription format = {};
    format.mSampleRate = sampleRate_;
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
        std::cerr << "[CoreAudioPlatform] ERROR: Failed to find AudioComponent\n";
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audioUnit_);
    if (status != noErr) {
        std::cerr << "[CoreAudioPlatform] ERROR: Failed to create AudioUnit (OSStatus=" << status << ")\n";
        return false;
    }

    status = AudioUnitSetProperty(
        audioUnit_,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0,
        &format,
        sizeof(format)
    );

    if (status != noErr) {
        std::cerr << "[CoreAudioPlatform] ERROR: Failed to set AudioUnit format (OSStatus=" << status << ")\n";
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }

    // Set up callback with this platform instance
    AURenderCallbackStruct callbackStruct = {};
    callbackStruct.inputProc = audioCallback;
    callbackStruct.inputProcRefCon = this;

    status = AudioUnitSetProperty(
        audioUnit_,
        kAudioUnitProperty_SetRenderCallback,
        kAudioUnitScope_Input,
        0,
        &callbackStruct,
        sizeof(callbackStruct)
    );

    if (status != noErr) {
        std::cerr << "[CoreAudioPlatform] ERROR: Failed to set callback (OSStatus=" << status << ")\n";
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }

    status = AudioUnitInitialize(audioUnit_);
    if (status != noErr) {
        std::cerr << "[CoreAudioPlatform] ERROR: Failed to initialize AudioUnit (OSStatus=" << status << ")\n";
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
        return false;
    }

    return true;
}

bool CoreAudioPlatform::start() {
    if (!audioUnit_ || isPlaying_) {
        return false;
    }

    OSStatus status = AudioOutputUnitStart(audioUnit_);
    if (status != noErr) {
        std::cerr << "[CoreAudioPlatform] ERROR: Failed to start AudioUnit (OSStatus=" << status << ")\n";
        return false;
    }

    isPlaying_ = true;
    return true;
}

void CoreAudioPlatform::stop() {
    if (!isPlaying_) {
        return;
    }

    if (audioUnit_) {
        AudioOutputUnitStop(audioUnit_);
    }

    isPlaying_ = false;
}

void CoreAudioPlatform::setVolume(float volume) {
    if (!audioUnit_) {
        return;
    }
    // Clamp to valid range
    volume = std::max(0.0f, std::min(1.0f, volume));
    OSStatus status = AudioUnitSetParameter(
        audioUnit_,
        kHALOutputParam_Volume,
        kAudioUnitScope_Global,
        0,      // element (0 = main output)
        volume,
        0       // bufferOffset (ignored for global param)
    );
    if (status != noErr) {
        std::cerr << "[CoreAudioPlatform] WARNING: Failed to set volume: " << status << "\n";
    }
}

void CoreAudioPlatform::setAudioSource(IAudioSource* source) {
    audioSource_ = source;
}

void CoreAudioPlatform::cleanup() {
    if (audioUnit_) {
        AudioOutputUnitStop(audioUnit_);
        AudioUnitUninitialize(audioUnit_);
        AudioComponentInstanceDispose(audioUnit_);
        audioUnit_ = nullptr;
    }

    isPlaying_ = false;
    isInitialized_ = false;
}

OSStatus CoreAudioPlatform::audioCallback(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 numberFrames,
    AudioBufferList* ioData
) {
    CoreAudioPlatform* platform = static_cast<CoreAudioPlatform*>(refCon);

    // Suppress unused parameter warnings
    (void)actionFlags;
    (void)timeStamp;
    (void)busNumber;

    return platform->renderAudio(ioData, numberFrames);
}

OSStatus CoreAudioPlatform::renderAudio(AudioBufferList* ioData, UInt32 numberFrames) {
    if (!audioSource_) {
        // No audio source - render silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            float* data = static_cast<float*>(buffer.mData);
            std::memset(data, 0, buffer.mDataByteSize);
        }
        return noErr;
    }

    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);

        // Generate audio using the source
        int framesGenerated = audioSource_->generateAudio(data, static_cast<int>(numberFrames));

        // Fill remaining frames with silence if needed
        if (framesGenerated < static_cast<int>(numberFrames)) {
            int remainingFrames = static_cast<int>(numberFrames) - framesGenerated;
            std::memset(data + framesGenerated * 2, 0, remainingFrames * 2 * sizeof(float));
        }
    }

    return noErr;
}

} // namespace macos
} // namespace platform
} // namespace audio
