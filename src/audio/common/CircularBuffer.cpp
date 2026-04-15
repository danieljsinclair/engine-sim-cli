// CircularBuffer.cpp - Pure ring buffer implementation

#include "CircularBuffer.h"

#include <cstring>
#include <algorithm>

// ============================================================================
// CircularBuffer Implementation
// ============================================================================

CircularBuffer::CircularBuffer()
    : buffer_(nullptr), bufferCapacity_(0), writePointer_(0), readPointer_(0), underrunCount_(0) {
}

CircularBuffer::~CircularBuffer() {
    cleanup();
}

bool CircularBuffer::initialize(size_t frameCapacity) {
    cleanup();
    
    bufferCapacity_ = frameCapacity;
    buffer_ = new float[frameCapacity * 2];  // Stereo = 2 channels
    if (!buffer_) {
        return false;
    }
    
    std::memset(buffer_, 0, frameCapacity * 2 * sizeof(float));
    writePointer_.store(0);
    readPointer_.store(0);
    
    return true;
}

void CircularBuffer::cleanup() {
    if (buffer_) {
        delete[] buffer_;
        buffer_ = nullptr;
    }
    bufferCapacity_ = 0;
    writePointer_.store(0);
    readPointer_.store(0);
}

size_t CircularBuffer::calculateDistance(int write, int read) const {
    if (write >= read) {
        return static_cast<size_t>(write - read);
    }
    return static_cast<size_t>(bufferCapacity_ - read + write);
}

size_t CircularBuffer::write(const float* samples, size_t frameCount) {
    if (!buffer_ || frameCount == 0) {
        return 0;
    }

    size_t available = freeSpace();
    size_t toWrite = std::min(frameCount, available);
    
    if (toWrite == 0) {
        return 0;
    }

    int writePtr = writePointer_.load();
    
    // Handle wrap-around
    if (writePtr + toWrite <= static_cast<int>(bufferCapacity_)) {
        // Simple case: no wrap-around
        for (size_t i = 0; i < toWrite; i++) {
            buffer_[(writePtr + i) * 2] = samples[i * 2];
            buffer_[(writePtr + i) * 2 + 1] = samples[i * 2 + 1];
        }
    } else {
        // Complex case: write spans buffer boundary
        size_t firstSegment = static_cast<size_t>(bufferCapacity_ - writePtr);
        
        // First segment: from current position to end of buffer
        for (size_t i = 0; i < firstSegment; i++) {
            buffer_[(writePtr + i) * 2] = samples[i * 2];
            buffer_[(writePtr + i) * 2 + 1] = samples[i * 2 + 1];
        }
        
        // Second segment: from beginning of buffer
        size_t secondSegment = toWrite - firstSegment;
        for (size_t i = 0; i < secondSegment; i++) {
            buffer_[i * 2] = samples[(firstSegment + i) * 2];
            buffer_[i * 2 + 1] = samples[(firstSegment + i) * 2 + 1];
        }
    }

    // Commit write with wrap-around
    int newWritePtr = static_cast<int>((writePtr + toWrite) % bufferCapacity_);
    writePointer_.store(newWritePtr);
    
    return toWrite;
}

size_t CircularBuffer::read(float* output, size_t frameCount) {
    if (!buffer_ || frameCount == 0) {
        return 0;
    }

    size_t availableFrames = available();
    size_t toRead = std::min(frameCount, availableFrames);
    
    if (toRead == 0) {
        return 0;
    }

    int readPtr = readPointer_.load();
    
    // Handle wrap-around
    if (readPtr + toRead <= static_cast<int>(bufferCapacity_)) {
        // Simple case: no wrap-around
        for (size_t i = 0; i < toRead; i++) {
            output[i * 2] = buffer_[(readPtr + i) * 2];
            output[i * 2 + 1] = buffer_[(readPtr + i) * 2 + 1];
        }
    } else {
        // Complex case: read spans buffer boundary
        size_t firstSegment = static_cast<size_t>(bufferCapacity_ - readPtr);
        
        // First segment: from current position to end of buffer
        for (size_t i = 0; i < firstSegment; i++) {
            output[i * 2] = buffer_[(readPtr + i) * 2];
            output[i * 2 + 1] = buffer_[(readPtr + i) * 2 + 1];
        }
        
        // Second segment: from beginning of buffer
        size_t secondSegment = toRead - firstSegment;
        for (size_t i = 0; i < secondSegment; i++) {
            output[(firstSegment + i) * 2] = buffer_[i * 2];
            output[(firstSegment + i) * 2 + 1] = buffer_[i * 2 + 1];
        }
    }

    // Update read pointer with wrap-around
    int newReadPtr = static_cast<int>((readPtr + toRead) % bufferCapacity_);
    readPointer_.store(newReadPtr);

    return toRead;
}

size_t CircularBuffer::readFromPosition(float* output, size_t frameCount, int position) const {
    if (!buffer_ || !output || frameCount == 0) {
        return 0;
    }

    size_t toRead = std::min(frameCount, static_cast<size_t>(bufferCapacity_));

    // Handle wrap-around read from specific position
    if (position + static_cast<int>(toRead) <= static_cast<int>(bufferCapacity_)) {
        // Simple case: no wrap-around
        for (size_t i = 0; i < toRead; i++) {
            output[i * 2] = buffer_[(position + static_cast<int>(i)) * 2];
            output[i * 2 + 1] = buffer_[(position + static_cast<int>(i)) * 2 + 1];
        }
    } else {
        // Complex case: read spans buffer boundary
        size_t firstSegment = static_cast<size_t>(bufferCapacity_ - position);

        // First segment: from current position to end of buffer
        for (size_t i = 0; i < firstSegment; i++) {
            output[i * 2] = buffer_[(position + static_cast<int>(i)) * 2];
            output[i * 2 + 1] = buffer_[(position + static_cast<int>(i)) * 2 + 1];
        }

        // Second segment: from beginning of buffer
        size_t secondSegment = toRead - firstSegment;
        for (size_t i = 0; i < secondSegment; i++) {
            output[(firstSegment + i) * 2] = buffer_[i * 2];
            output[(firstSegment + i) * 2 + 1] = buffer_[i * 2 + 1];
        }
    }

    return toRead;
}

size_t CircularBuffer::available() const {
    if (!buffer_) {
        return 0;
    }
    int writePtr = writePointer_.load();
    int readPtr = readPointer_.load();
    return calculateDistance(writePtr, readPtr);
}

size_t CircularBuffer::freeSpace() const {
    if (!buffer_) {
        return 0;
    }
    return bufferCapacity_ - available();
}

void CircularBuffer::reset() {
    writePointer_.store(0);
    readPointer_.store(0);
    underrunCount_.store(0);
}

void CircularBuffer::clear() {
    if (buffer_) {
        std::memset(buffer_, 0, bufferCapacity_ * 2 * sizeof(float));
    }
    reset();
}
