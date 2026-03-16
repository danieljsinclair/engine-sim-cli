// AudioPlayer.cpp - Audio playback implementation
// Refactored to use separate CircularBuffer and SyncPullAudio classes

#include "AudioPlayer.h"
#include "CircularBuffer.h"
#include "SyncPullAudio.h"

#include <iostream>
#include <cstring>
#include <cmath>
#include <thread>
#include <algorithm>
#include <iomanip>

// ============================================================================
// AudioPlayer Class Implementation
// ============================================================================

AudioPlayer::AudioPlayer() : audioUnit(nullptr), deviceID(0),
                isPlaying(false), sampleRate(0),
                context(nullptr) {
}

AudioPlayer::~AudioPlayer() {
    cleanup();
}

bool AudioPlayer::initialize(int sr, bool syncPull, EngineSimHandle handle, const EngineSimAPI* api, bool silent) {
    sampleRate = sr;

    // Create callback context
    context = new AudioUnitContext();
    context->sampleRate = sr;
    context->silent = silent;

    // Setup sync pull model if enabled
    if (syncPull && handle && api) {
        context->engineHandle = handle;
        // Use SyncPullAudio class for sync-pull rendering
        context->syncPullAudio = std::make_unique<SyncPullAudio>();
        if (!context->syncPullAudio->initialize(handle, api, sr, silent)) {
            std::cerr << "ERROR: Failed to initialize SyncPullAudio\n";
            delete context;
            context = nullptr;
            return false;
        }
        std::cout << "[Audio] Sync pull model enabled (using SyncPullAudio)\n";
    } else {
        std::cout << "[Audio] Cursor-chasing mode: 1s buffer with 100ms target lead\n";
    }

    // Set up audio format - PCM float32 stereo
    AudioStreamBasicDescription format = {};
    format.mSampleRate = sampleRate;
    format.mFormatID = kAudioFormatLinearPCM;
    format.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
    format.mBytesPerPacket = 2 * sizeof(float);  // Stereo
    format.mFramesPerPacket = 1;
    format.mBytesPerFrame = 2 * sizeof(float);   // Stereo float
    format.mChannelsPerFrame = 2;                // Stereo
    format.mBitsPerChannel = 8 * sizeof(float);

    // Create AudioUnit (AUHAL - Audio Unit Hardware Abstraction Layer)
    AudioComponentDescription desc = {};
    desc.componentType = kAudioUnitType_Output;
    desc.componentSubType = kAudioUnitSubType_DefaultOutput;
    desc.componentManufacturer = kAudioUnitManufacturer_Apple;
    desc.componentFlags = 0;
    desc.componentFlagsMask = 0;

    AudioComponent component = AudioComponentFindNext(nullptr, &desc);
    if (!component) {
        std::cerr << "ERROR: Failed to find AudioComponent\n";
        delete context;
        context = nullptr;
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
    if (status != noErr) {
        std::cerr << "ERROR: Failed to create AudioUnit: " << status << "\n";
        delete context;
        context = nullptr;
        return false;
    }

    // Set format for output
    status = AudioUnitSetProperty(
        audioUnit,
        kAudioUnitProperty_StreamFormat,
        kAudioUnitScope_Input,
        0,
        &format,
        sizeof(format)
    );

    if (status != noErr) {
        std::cerr << "ERROR: Failed to set AudioUnit format: " << status << "\n";
        cleanup();
        return false;
    }

    // Set up render callback
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
        std::cerr << "ERROR: Failed to set AudioUnit callback: " << status << "\n";
        cleanup();
        return false;
    }

    // Initialize AudioUnit
    status = AudioUnitInitialize(audioUnit);
    if (status != noErr) {
        std::cerr << "ERROR: Failed to initialize AudioUnit: " << status << "\n";
        cleanup();
        return false;
    }

    // Cursor-chasing buffer: 2+ seconds capacity (now uses CircularBuffer class)
    // 96000 samples = 2+ seconds at 44.1kHz
    context->circularBuffer = std::make_unique<CircularBuffer>();
    if (!context->circularBuffer->initialize(96000)) {
        std::cerr << "ERROR: Failed to initialize CircularBuffer\n";
        cleanup();
        return false;
    }

    // Initialize pointers - start with 100ms offset (cursor-chasing approach)
    // GUI starts writePointer at 4410 (100ms) for initial lead
    context->writePointer.store(static_cast<int>(sr * 0.1));  // 100ms ahead
    context->readPointer.store(0);
    context->totalFramesRead.store(0);

    // Get default output device for diagnostics
    AudioObjectPropertyAddress propertyAddress = {
        kAudioHardwarePropertyDefaultOutputDevice,
        kAudioObjectPropertyScopeGlobal,
        kAudioObjectPropertyElementMain
    };

    UInt32 deviceIDSize = sizeof(deviceID);
    status = AudioObjectGetPropertyData(
        kAudioObjectSystemObject,
        &propertyAddress,
        0,
        nullptr,
        &deviceIDSize,
        &deviceID
    );

    if (status != noErr) {
        std::cerr << "WARNING: Could not get audio device ID\n";
    }

    if (silent) {
        std::cout << "[Audio] Silent mode - output muted in callback\n";
    }

    std::cout << "[Audio] AudioUnit initialized at " << sampleRate << " Hz (stereo float32)\n";
    if (syncPull) {
        std::cout << "[Audio] Sync pull model - direct render on audio callback\n";
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
        // CircularBuffer and SyncPullAudio are cleaned up via unique_ptr
        context->circularBuffer.reset();
        context->syncPullAudio.reset();
        delete context;
        context = nullptr;
    }

    isPlaying = false;
}

void AudioPlayer::setEngineHandle(EngineSimHandle handle) {
    if (context) {
        context->engineHandle = handle;
    }
}

bool AudioPlayer::isSyncPullMode() const {
    return context && context->syncPullAudio && context->syncPullAudio->isEnabled();
}

bool AudioPlayer::start() {
    if (!audioUnit) return false;

    OSStatus status = AudioOutputUnitStart(audioUnit);
    if (status != noErr) {
        std::cerr << "ERROR: Failed to start AudioUnit: " << status << "\n";
        return false;
    }

    isPlaying = true;
    if (context) {
        context->isPlaying.store(true);
    }
    return true;
}

void AudioPlayer::stop() {
    if (audioUnit && isPlaying) {
        AudioOutputUnitStop(audioUnit);
        isPlaying = false;
        if (context) {
            context->isPlaying.store(false);
        }
    }
}

bool AudioPlayer::playBuffer(const float* data, int frames, int sampleRate) {
    // This method is kept for compatibility but does nothing in streaming mode
    // The AudioUnit callback handles all audio rendering
    if (!isPlaying) {
        start();
    }
    return true;
}

void AudioPlayer::waitForCompletion() {
    // In streaming mode, we just wait a bit for final samples to play
    if (isPlaying) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
    }
}

void AudioPlayer::addToCircularBuffer(const float* samples, int frameCount) {
    if (!context || !context->circularBuffer) return;

    // Use CircularBuffer class for writing
    context->circularBuffer->write(samples, frameCount);
    
    // Update write pointer to match CircularBuffer state
    context->writePointer.store(static_cast<int>(context->circularBuffer->getWritePointer()));
}

AudioUnitContext* AudioPlayer::getContext() { 
    return context; 
}

void AudioPlayer::getBufferDiagnostics(int& writePtr, int& readPtr, int& available, int& status) {
    if (context && context->circularBuffer) {
        writePtr = context->writePointer.load();
        readPtr = context->readPointer.load();

        // Use CircularBuffer class for available calculation
        available = static_cast<int>(context->circularBuffer->available());

        // Set status based on cursor-chasing target (100ms = 4410 samples)
        // Normal: within 50-200ms lead (2205-8820 samples)
        // Warning: 25-50ms or 200-400ms lead
        // Critical: <25ms lead (but still has data)
        // Underrun: no data
        const int targetLead = static_cast<int>(context->sampleRate * 0.1);  // 4410
        if (available >= targetLead / 2 && available <= targetLead * 2) {
            status = 0;  // Normal: 50-200ms lead
        } else if (available >= targetLead / 4 || available <= targetLead * 4) {
            status = 1;  // Warning: 25-50ms or 200-400ms lead
        } else if (available > 0) {
            status = 2;  // Critical: <25ms but not underrun yet
        } else {
            status = 3;  // Underrun
            context->underrunCount.fetch_add(1);
        }
    } else {
        writePtr = 0;
        readPtr = 0;
        available = 0;
        status = 3;
    }
}

void AudioPlayer::resetBufferDiagnostics() {
    if (context) {
        context->underrunCount.store(0);
        context->bufferStatus = 0;
    }
}

void AudioPlayer::resetCircularBuffer() {
    if (context && context->circularBuffer) {
        // Reset with 100ms offset (cursor-chasing initial state)
        context->writePointer.store(static_cast<int>(context->sampleRate * 0.1));
        context->readPointer.store(0);
        context->totalFramesRead.store(0);
        // Clear buffer contents
        context->circularBuffer->clear();
    }
}

int AudioPlayer::calculateCursorChasingSamples(int defaultFrames) {
    if (!context || !context->circularBuffer) return defaultFrames;

    const int bufferSize = static_cast<int>(context->circularBuffer->capacity());
    const int writePtr = context->writePointer.load();
    const int readPtr = context->readPointer.load();

    // Calculate current lead (distance ahead of playback cursor)
    int currentLead;
    if (writePtr >= readPtr) {
        currentLead = writePtr - readPtr;
    } else {
        currentLead = (bufferSize - readPtr) + writePtr;
    }

    // Target: 100ms ahead (matches GUI line 257)
    const int targetLead = static_cast<int>(context->sampleRate * 0.1);  // 4410 samples at 44.1kHz

    // Safety: if too far ahead (>500ms), snap back to 50ms (matches GUI lines 263-267)
    const int maxLead = static_cast<int>(context->sampleRate * 0.5);  // 22050 samples
    if (currentLead > maxLead) {
        // Reset write pointer to 50ms ahead of read pointer
        int newWritePtr = (readPtr + static_cast<int>(context->sampleRate * 0.05)) % bufferSize;
        context->writePointer.store(newWritePtr);
        currentLead = static_cast<int>(context->sampleRate * 0.05);
    }

    // Calculate target write position (100ms ahead of current read position)
    int targetWritePtr = (readPtr + targetLead) % bufferSize;

    // Calculate how many samples to write to reach target
    int maxWrite;
    if (targetWritePtr >= writePtr) {
        maxWrite = targetWritePtr - writePtr;
    } else {
        maxWrite = (bufferSize - writePtr) + targetWritePtr;
    }

    // Prevent underrun: don't write if it would make buffer smaller (matches GUI line 269-271)
    int newLead;
    if (targetWritePtr >= readPtr) {
        newLead = targetWritePtr - readPtr;
    } else {
        newLead = (bufferSize - readPtr) + targetWritePtr;
    }

    if (currentLead > newLead) {
        return 0;  // Would reduce buffer - skip this write
    }

    return std::min(maxWrite, defaultFrames);
}

// ============================================================================
// Static Callback for real-time audio rendering
// Uses separate SyncPullAudio and CircularBuffer classes
// ============================================================================

OSStatus AudioPlayer::audioUnitCallback(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 numberFrames,
    AudioBufferList* ioData
) {
    AudioUnitContext* ctx = static_cast<AudioUnitContext*>(refCon);

    // Check if we should be playing
    if (!ctx || !ctx->isPlaying.load()) {
        // Output silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            float* data = static_cast<float*>(buffer.mData);
            std::memset(data, 0, buffer.mDataByteSize);
        }
        return noErr;
    }

    // SYNC PULL MODEL: Use SyncPullAudio class for rendering
    // This bypasses the circular buffer entirely for lower latency
    if (ctx->syncPullAudio && ctx->syncPullAudio->isEnabled()) {
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            float* data = static_cast<float*>(buffer.mData);

            UInt32 framesToRender = numberFrames;
            if (framesToRender * 2 * sizeof(float) > buffer.mDataByteSize) {
                framesToRender = buffer.mDataByteSize / (2 * sizeof(float));
            }

            // Use SyncPullAudio class for rendering
            int framesRead = ctx->syncPullAudio->renderOnDemand(data, static_cast<int>(framesToRender));

            // Tell AudioUnit actual buffer size to avoid playing zeros
            if (framesRead < static_cast<int>(framesToRender)) {
                for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
                    ioData->mBuffers[i].mDataByteSize = framesRead * 2 * sizeof(float);
                }
            }
        }
        return noErr;
    }

    // CIRCULAR BUFFER MODEL: Use CircularBuffer class for rendering
    // This buffer is filled by main loop calling addToCircularBuffer()
    if (!ctx->circularBuffer || !ctx->circularBuffer->isInitialized()) {
        // No buffer - output silence
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            float* data = static_cast<float*>(buffer.mData);
            std::memset(data, 0, buffer.mDataByteSize);
        }
        return noErr;
    }

    const int bufferSize = static_cast<int>(ctx->circularBuffer->capacity());

    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);

        // Calculate how many frames we can write
        UInt32 framesToWrite = numberFrames;
        if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
            framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
        }

        // Use CircularBuffer class for reading
        int readPtr = ctx->readPointer.load();
        int writePtr = ctx->writePointer.load();

        // Calculate how much data is available in circular buffer
        int available;
        if (writePtr >= readPtr) {
            available = writePtr - readPtr;
        } else {
            available = (bufferSize - readPtr) + writePtr;
        }

        int framesToRead = std::min(static_cast<int>(framesToWrite), available);

        // Diagnostic: Track buffer status
        if (framesToRead < static_cast<int>(framesToWrite)) {
            // Underrun detected - increment counter
            ctx->underrunCount.fetch_add(1);
            // Log underrun periodically (every 10th underrun to avoid spam)
            if (ctx->underrunCount.load() % 10 == 0) {
                std::cout << "[Audio Diagnostics] Buffer underrun #" << ctx->underrunCount.load()
                          << " - requested: " << framesToWrite << ", available: " << available << "\n";
            }
        }

        // Use CircularBuffer class for reading
        size_t framesRead = ctx->circularBuffer->read(data, framesToRead);
        (void)framesRead;  // Suppress unused warning

        // Fill rest with silence if underrun
        if (framesToRead < static_cast<int>(framesToWrite)) {
            int silenceFrames = framesToWrite - framesToRead;
            std::memset(data + framesToRead * 2, 0, silenceFrames * 2 * sizeof(float));

            // Set buffer status for diagnostics
            ctx->bufferStatus = (available < bufferSize / 8) ? 2 : 1;
        } else {
            ctx->bufferStatus = 0;  // Normal
        }

        // Update read pointer (hardware playback cursor position in circular buffer)
        int newReadPtr = (readPtr + framesToRead) % bufferSize;
        ctx->readPointer.store(newReadPtr);

        // Track total frames read for cursor tracking (used by main loop)
        ctx->totalFramesRead.fetch_add(framesToRead);

        // Silent mode: zero output after processing so no sound reaches speakers
        if (ctx->silent) {
            std::memset(data, 0, framesToWrite * 2 * sizeof(float));
        }
    }

    return noErr;
}
