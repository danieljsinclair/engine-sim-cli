// CircularBuffer.h - Pure ring buffer for audio data
// Separated from AudioPlayer for SRP compliance

#ifndef CIRCULAR_BUFFER_H
#define CIRCULAR_BUFFER_H

#include <atomic>
#include <cstddef>
#include <cstdint>

// ============================================================================
// CircularBuffer - Thread-safe ring buffer for audio streaming
// Handles only buffer storage, no audio rendering logic
// ============================================================================

class CircularBuffer {
public:
    CircularBuffer();
    ~CircularBuffer();

    // Initialize buffer with given capacity (in frames, stereo = 2 channels)
    bool initialize(size_t frameCapacity);

    // Clean up buffer resources
    void cleanup();

    // Write frames to buffer (stereo interleaved)
    // Returns number of frames actually written
    size_t write(const float* samples, size_t frameCount);

    // Read frames from buffer (stereo interleaved)
    // Returns number of frames actually read
    size_t read(float* output, size_t frameCount);

    // Read frames from buffer starting at specific position (for cursor-chasing)
    // This allows reading from arbitrary positions without updating internal read pointer
    // Returns number of frames actually read
    size_t readFromPosition(float* output, size_t frameCount, int position) const;

    // Get available frames in buffer (data waiting to be read)
    size_t available() const;

    // Get free space in buffer (space available for writing)
    size_t freeSpace() const;

    // Reset buffer state (keep data, reset pointers)
    void reset();

    // Clear buffer content (zero out)
    void clear();

    // Get buffer capacity
    size_t capacity() const { return bufferCapacity_; }

    // Get current write position
    int getWritePointer() const { return writePointer_.load(); }

    // Get current read position
    int getReadPointer() const { return readPointer_.load(); }

    // Get raw buffer pointer for direct access
    float* getBuffer() { return buffer_; }

    // Check if buffer is initialized
    bool isInitialized() const { return buffer_ != nullptr; }

private:
    float* buffer_;                    // Stereo interleaved buffer
    size_t bufferCapacity_;            // Capacity in frames
    std::atomic<int> writePointer_;    // Write position
    std::atomic<int> readPointer_;     // Read position

    // Calculate circular distance from read to write
    size_t calculateDistance(int write, int read) const;
};

#endif // CIRCULAR_BUFFER_H
