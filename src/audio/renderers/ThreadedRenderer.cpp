// ThreadedRenderer.cpp - Threaded renderer implementation
// Renders audio from cursor-chasing circular buffer using hardware feedback

#include "audio/renderers/ThreadedRenderer.h"
#include "AudioPlayer.h"
#include "../common/CircularBuffer.h"

#include <iostream>
#include <cstring>
#include <algorithm>

bool ThreadedRenderer::render(void* ctx, AudioBufferList* ioData, UInt32 numberFrames) {
    AudioUnitContext* context = static_cast<AudioUnitContext*>(ctx);

    if (!context || !context->circularBuffer || !context->circularBuffer->isInitialized()) {
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
                std::cout << "[SYNC-PULL] UNDERFLOW (x" << context->underrunCount.load()
                          << "): requested " << framesToWrite << ", got " << framesToRead << "\n";
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
    }
    
    return true;
}

bool ThreadedRenderer::isEnabled() const {
    // Will be checked via context in actual use
    return true;
}

bool ThreadedRenderer::AddFrames(void* ctx, float* buffer, int frameCount) {
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
