// AudioPlayer.cpp - Audio playback implementation
// Refactored to use injected IAudioRenderer (SOLID: DI, OCP, SRP)
// OCP: Uses renderer strategy, no boolean flags

#include "AudioPlayer.h"
#include "AudioMode.h"
#include "CircularBuffer.h"
#include "SyncPullAudio.h"

#include <iostream>
#include <cstring>
#include <cmath>
#include <thread>
#include <algorithm>
#include <iomanip>
#include <stdexcept>

// ============================================================================
// Strategy Pattern: IAudioRenderer Implementations
// ============================================================================

// ----------------------------------------------------------------------------
// SyncPullRenderer Implementation
// Renders audio synchronously on-demand from the engine simulator
// ----------------------------------------------------------------------------
bool SyncPullRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);
    
    if (!context || !context->syncPullAudio) {
        return false;
    }
    
    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);

        // Clamp frames to buffer capacity
        UInt32 framesToRender = numberFrames;
        if (framesToRender * 2 * sizeof(float) > buffer.mDataByteSize) {
            framesToRender = buffer.mDataByteSize / (2 * sizeof(float));
        }

        // Request frames from the engine simulator synchronously
        int framesRead = context->syncPullAudio->renderOnDemand(data, static_cast<int>(framesToRender));

        // Update buffer size if partial read (end of audio)
        if (framesRead < static_cast<int>(framesToRender)) {
            for (UInt32 j = 0; j < ioData->mNumberBuffers; j++) {
                ioData->mBuffers[j].mDataByteSize = framesRead * 2 * sizeof(float);
            }
        }
    }
    
    return true;
}

bool SyncPullRenderer::isEnabled() const {
    // Will be checked via context in actual use
    return true;
}

bool SyncPullRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
    // Sync-pull renders on-demand in the callback, not via AddFrames
    // This method is a no-op - return true to indicate success
    (void)ctx;
    (void)buffer;
    (void)frameCount;
    return true;
}

// ----------------------------------------------------------------------------
// CircularBufferRenderer Implementation  
// Renders audio from cursor-chasing circular buffer using hardware feedback
// ----------------------------------------------------------------------------
bool CircularBufferRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);
    
    if (!context->circularBuffer || !context->circularBuffer->isInitialized()) {
        return false;
    }

    const int bufferSize = static_cast<int>(context->circularBuffer->capacity());

    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);

        // Clamp frames to buffer capacity
        UInt32 framesToWrite = numberFrames;
        if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
            framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
        }

        // Calculate available frames using cursor-chasing logic
        int readPtr = context->readPointer.load();
        int writePtr = context->writePointer.load();

        int available;
        if (writePtr >= readPtr) {
            available = writePtr - readPtr;
        } else {
            available = (bufferSize - readPtr) + writePtr;
        }

        // Determine how many frames we can actually read
        int framesToRead = std::min(static_cast<int>(framesToWrite), available);

        // Track underruns for diagnostics
        if (framesToRead < static_cast<int>(framesToWrite)) {
            context->underrunCount.fetch_add(1);
            if (context->underrunCount.load() % 10 == 0) {
                std::cout << "[Audio Diagnostics] Buffer underrun #" << context->underrunCount.load()
                          << " - requested: " << framesToWrite << ", available: " << available << "\n";
            }
        }

        // Read from circular buffer
        size_t framesRead = context->circularBuffer->read(data, framesToRead);
        (void)framesRead;

        // Handle underrun: fill remaining with silence
        if (framesToRead < static_cast<int>(framesToWrite)) {
            int silenceFrames = framesToWrite - framesToRead;
            std::memset(data + framesToRead * 2, 0, silenceFrames * 2 * sizeof(float));
            context->bufferStatus = (available < bufferSize / 8) ? 2 : 1;
        } else {
            context->bufferStatus = 0;
        }

        // Update read cursor (hardware position)
        int newReadPtr = (readPtr + framesToRead) % bufferSize;
        context->readPointer.store(newReadPtr);
        context->totalFramesRead.fetch_add(framesToRead);

        // Apply silent mode if requested
        if (context->silent) {
            std::memset(data, 0, framesToWrite * 2 * sizeof(float));
        }
    }
    
    return true;
}

bool CircularBufferRenderer::isEnabled() const {
    // Will be checked via context in actual use
    return true;
}

bool CircularBufferRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);
    
    if (!context || !context->circularBuffer || !context->circularBuffer->isInitialized()) {
        return false;
    }
    
    // Write frames to circular buffer
    size_t framesWritten = context->circularBuffer->write(buffer, frameCount);
    
    // Update write pointer
    int writePtr = context->writePointer.load();
    int bufferSize = static_cast<int>(context->circularBuffer->capacity());
    int newWritePtr = (writePtr + static_cast<int>(framesWritten)) % bufferSize;
    context->writePointer.store(newWritePtr);
    
    return framesWritten > 0;
}

// ----------------------------------------------------------------------------
// SilentRenderer Implementation
// Renders silence - used as fallback when no valid mode is configured
// ----------------------------------------------------------------------------
bool SilentRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    (void)ctx;
    (void)numberFrames;
    
    for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
        AudioBuffer& buffer = ioData->mBuffers[i];
        float* data = static_cast<float*>(buffer.mData);
        std::memset(data, 0, buffer.mDataByteSize);
    }
    return true;
}

bool SilentRenderer::isEnabled() const {
    return true;
}

bool SilentRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
    // SilentRenderer discards all frames - return true to indicate success
    (void)ctx;
    (void)buffer;
    (void)frameCount;
    return true;
}

// ============================================================================
// AudioPlayer Class Implementation - Uses injected IAudioRenderer (DI)
// OCP: Uses Strategy pattern for rendering - no boolean flags
// SRP: Rendering logic is in IAudioRenderer, not AudioPlayer
// DI: Renderer is injected via constructor or obtained from context
// ============================================================================

// Constructor with optional injected IAudioRenderer (DI pattern)
// If provided, can override the mode's renderer; otherwise context's renderer is used
AudioPlayer::AudioPlayer(IAudioRenderer* renderer) 
    : audioUnit(nullptr), deviceID(0),
      isPlaying(false), sampleRate(0),
      context(nullptr), renderer(renderer) {
}

AudioPlayer::~AudioPlayer() {
    cleanup();
}

bool AudioPlayer::initialize(IAudioMode& audioMode, int sr, EngineSimHandle handle, 
                              const EngineSimAPI* api, bool silent) {
    sampleRate = sr;

    // Use IAudioMode to create the appropriate context
    // DI: IAudioMode injects its own renderer into the context
    context = audioMode.createContext(sr, handle, api, silent).release();
    
    if (!context) {
        std::cerr << "ERROR: Failed to create audio context\n";
        return false;
    }

    // OCP: No conditional branching - use injected renderer from constructor
    // if provided, otherwise use the one already injected by IAudioMode into context
    if (renderer) {
        // Constructor-provided renderer takes precedence
        context->setRenderer(renderer);
        std::cout << "[Audio] Using constructor-injected renderer: " << renderer->getName() << "\n";
    } else if (!context->audioRenderer) {
        // Hard error: no renderer configured - must have a valid renderer
        throw std::runtime_error("FATAL: No audio renderer injected - IAudioRenderer is required");
    }
    
    if (!setupAudioUnit()) {
        delete context;
        context = nullptr;
        return false;
    }
    
    std::cout << "[Audio] Initialized via factory: " << audioMode.getModeName() << "\n";
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
        std::cerr << "ERROR: Failed to find AudioComponent\n";
        return false;
    }

    OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
    if (status != noErr) {
        std::cerr << "ERROR: Failed to create AudioUnit: " << status << "\n";
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
        std::cerr << "ERROR: Failed to set AudioUnit format: " << status << "\n";
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
        std::cerr << "ERROR: Failed to set callback: " << status << "\n";
        AudioComponentInstanceDispose(audioUnit);
        audioUnit = nullptr;
        return false;
    }

    status = AudioUnitInitialize(audioUnit);
    if (status != noErr) {
        std::cerr << "ERROR: Failed to initialize AudioUnit: " << status << "\n";
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
        std::cerr << "ERROR: Failed to start AudioUnit: " << status << "\n";
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

bool AudioPlayer::playBuffer(const float* data, int frames, int sr) {
    // SOLID: No branching - delegate to renderer via AddFrames()
    // OCP: New renderers can be added without changing this code
    if (!context || !context->audioRenderer) {
        std::cerr << "ERROR: No audio context or renderer available\n";
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
