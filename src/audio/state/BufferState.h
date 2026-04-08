// BufferState.h - Circular buffer state management
// SRP: Single responsibility - manages only buffer pointers and counters
// OCP: New buffer types can be added without modifying existing code
// DIP: High-level modules depend on this abstraction

#ifndef BUFFER_STATE_H
#define BUFFER_STATE_H

#include <atomic>

/**
 * BufferState - Circular buffer state management
 *
 * Responsibilities:
 * - Track read/write pointers for cursor-chasing
 * - Maintain buffer capacity for calculations
 * - Track underrun events for diagnostics
 * - Thread-safe state management
 *
 * SRP: Only manages buffer state, not audio or diagnostics
 */
struct BufferState {
    /**
     * Initialize with default values
     */
    BufferState()
        : writePointer(0)
        , readPointer(0)
        , underrunCount(0)
        , fillLevel(0)
        , capacity(0)
    {}

    /**
     * Write pointer in circular buffer
     * Points to next position for writing frames
     * Wraps around at buffer capacity
     */
    std::atomic<int> writePointer;

    /**
     * Read pointer in circular buffer
     * Points to next position for reading frames
     * Used for cursor-chasing with hardware feedback
     * Wraps around at buffer capacity
     */
    std::atomic<int> readPointer;

    /**
     * Count of buffer underrun events
     * Incremented when hardware requests frames that aren't available
     * Used for diagnostics and health monitoring
     */
    std::atomic<int> underrunCount;

    /**
     * Current fill level of buffer
     * - 0: Empty buffer
     * - capacity: Full buffer
     * Used for buffer health monitoring
     */
    int fillLevel;

    /**
     * Buffer capacity in frames
     * Total number of frames buffer can hold
     * Used for calculating available space and wrap-around
     */
    int capacity;

    /**
     * Calculate available frames in buffer
     * @return Number of frames that can be read
     *
     * Handles wrap-around correctly
     */
    int availableFrames() const {
        if (writePointer.load() >= readPointer.load()) {
            return writePointer.load() - readPointer.load();
        } else {
            return capacity - readPointer.load() + writePointer.load();
        }
    }

    /**
     * Calculate free space in buffer
     * @return Number of frames that can be written
     *
     * Handles wrap-around correctly
     */
    int freeSpace() const {
        return capacity - availableFrames() - 1;
    }

    /**
     * Reset state to initial values
     * Useful for cleanup or re-initialization scenarios
     */
    void reset() {
        writePointer.store(0);
        readPointer.store(0);
        underrunCount.store(0);
        fillLevel = 0;
    }
};

#endif // BUFFER_STATE_H
