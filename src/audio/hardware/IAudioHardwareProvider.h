// IAudioHardwareProvider.h - Platform-agnostic audio hardware abstraction
// Strategy pattern - abstracts platform-specific audio hardware operations
// OCP: New platforms can be added without modifying AudioPlayer
// ISP: Interface Segregation - focused, minimal interfaces
// DIP: High-level modules depend on abstraction, not concrete implementations

#ifndef IAUDIO_HARDWARE_PROVIDER_H
#define IAUDIO_HARDWARE_PROVIDER_H

#include <functional>
#include <memory>
#include <cstdint>

// Forward declarations
struct AudioStreamFormat;
struct PlatformAudioBufferList;

/**
 * Audio format specification (platform-agnostic)
 * Used to configure audio hardware with sample rate and channel configuration
 */
struct AudioStreamFormat {
    int sampleRate;           // Sample rate in Hz (e.g., 44100, 48000)
    int channels;              // Number of audio channels (typically 2 for stereo)
    int bitsPerSample;         // Bits per sample (typically 32 for float)
    bool isFloat;             // True if floating-point samples (false for integer)
    bool isInterleaved;       // True if channels are interleaved (LRLR...)

    AudioStreamFormat()
        : sampleRate(44100), channels(2), bitsPerSample(32),
          isFloat(true), isInterleaved(true) {}
};

/**
 * Audio buffer list wrapper (platform-agnostic)
 * Used for audio rendering callbacks
 */
struct PlatformAudioBufferList {
    int numberBuffers;                    // Number of buffers
    void* buffers;                       // Platform-specific buffer array
    int* bufferSizes;                  // Array of buffer sizes in bytes
    int* bufferChannels;                // Array of channel counts per buffer
    void** bufferData;                   // Array of data pointers

    PlatformAudioBufferList()
        : numberBuffers(0), buffers(nullptr),
          bufferSizes(nullptr), bufferChannels(nullptr),
          bufferData(nullptr) {}
};

/**
 * Audio hardware state information
 * Provides diagnostic information about hardware state
 */
struct AudioHardwareState {
    bool isInitialized;      // Hardware has been initialized
    bool isPlaying;          // Hardware is currently playing
    bool isCallbackActive;   // Audio callback is currently active
    double currentVolume;     // Current volume level (0.0 to 1.0)
    int underrunCount;       // Number of buffer underruns
    int overrunCount;        // Number of buffer overruns

    AudioHardwareState()
        : isInitialized(false), isPlaying(false), isCallbackActive(false),
          currentVolume(1.0), underrunCount(0), overrunCount(0) {}
};

/**
 * IAudioHardwareProvider - Interface for platform-specific audio hardware
 *
 * This interface abstracts all platform-specific audio hardware operations.
 * Each platform (macOS, iOS, ESP32, etc.) implements this interface.
 *
 * Responsibilities (SRP):
 * - AudioUnit/AudioEngine lifecycle management
 * - Audio format configuration
 * - Playback control (start/stop)
 * - Volume control
 * - Callback registration for real-time rendering
 * - Hardware state diagnostics
 *
 * OCP: New platforms can be added without modifying AudioPlayer
 * DIP: AudioPlayer depends on this abstraction, not concrete implementations
 * ISP: Focused interfaces - each method has single responsibility
 */
class IAudioHardwareProvider {
public:
    virtual ~IAudioHardwareProvider() = default;

    // ================================================================
    // Lifecycle Methods
    // ================================================================

    /**
     * Initialize audio hardware with specified format
     * @param format Audio format configuration
     * @return true if initialization succeeded, false otherwise
     *
     * This method must:
     * - Allocate and initialize platform-specific audio resources
     * - Configure audio format (sample rate, channels, etc.)
     * - Prepare hardware for playback but NOT start playback yet
     */
    virtual bool initialize(const AudioStreamFormat& format) = 0;

    /**
     * Cleanup audio hardware resources
     *
     * This method must:
     * - Stop any active playback
     * - Release all allocated resources
     * - Reset hardware to initial state
     */
    virtual void cleanup() = 0;

    // ================================================================
    // Playback Control Methods
    // ================================================================

    /**
     * Start audio playback
     * @return true if playback started successfully, false otherwise
     *
     * This method must:
     * - Begin audio streaming
     * - Activate audio callback
     * - Set isPlaying state to true
     */
    virtual bool startPlayback() = 0;

    /**
     * Stop audio playback
     *
     * This method must:
     * - Stop audio streaming
     * - Deactivate audio callback
     * - Set isPlaying state to false
     */
    virtual void stopPlayback() = 0;

    // ================================================================
    // Volume Control Methods
    // ================================================================

    /**
     * Set output volume level
     * @param volume Volume level (0.0 to 1.0)
     *
     * This method must:
     * - Apply volume to output
     * - Validate volume is within valid range
     * - Clamp invalid values to valid range
     */
    virtual void setVolume(double volume) = 0;

    /**
     * Get current output volume level
     * @return Current volume level (0.0 to 1.0)
     */
    virtual double getVolume() const = 0;

    // ================================================================
    // Callback Registration Methods
    // ================================================================

    /**
     * Audio rendering callback signature
     *
     * Platform implementations call this callback when audio hardware needs samples.
     *
     * @param refCon Reference to connection (platform-specific context)
     * @param actionFlags Action flags (platform-specific)
     * @param timeStamp Timestamp of callback
     * @param busNumber Bus number (for multi-bus scenarios)
     * @param numberFrames Number of audio frames requested
     * @param ioData Audio buffer list to fill with samples
     *
     * Return value: Platform-specific status code (0 for success)
     */
    using AudioCallback = std::function<int(void* refCon, void* actionFlags,
                                           const void* timeStamp,
                                           int busNumber, int numberFrames,
                                           PlatformAudioBufferList* ioData)>;

    /**
     * Register audio rendering callback
     * @param callback Function to call when hardware needs samples
     * @return true if callback registered successfully, false otherwise
     *
     * This method must:
     * - Store callback for later invocation by platform
     * - Configure hardware to use callback for rendering
     */
    virtual bool registerAudioCallback(const AudioCallback& callback) = 0;

    // ================================================================
    // Diagnostic Methods
    // ================================================================

    /**
     * Get current hardware state
     * @return Current hardware state information
     *
     * This method must:
     * - Return snapshot of current hardware state
     * - Include all diagnostic counters (underruns, overruns, etc.)
     */
    virtual AudioHardwareState getHardwareState() const = 0;

    /**
     * Reset diagnostic counters
     *
     * This method must:
     * - Reset all counters (underruns, overruns, etc.)
     * - Preserve current playback state if playing
     */
    virtual void resetDiagnostics() = 0;
};

/**
 * AudioHardwareProviderFactory - Factory for creating platform-specific providers
 *
 * This factory creates appropriate IAudioHardwareProvider implementation
 * based on the current platform or configuration.
 *
 * OCP: New platforms can be added by extending factory
 * DIP: Client code depends on IAudioHardwareProvider, not concrete implementations
 */
class AudioHardwareProviderFactory {
public:
    /**
     * Create audio hardware provider for current platform
     * @param logger Optional logger for diagnostics
     * @return Unique pointer to created provider, or nullptr if unsupported platform
     *
     * Factory automatically detects current platform and creates
     * appropriate provider implementation.
     */
    static std::unique_ptr<IAudioHardwareProvider> createProvider(class ILogging* logger = nullptr);
};

#endif // IAUDIO_HARDWARE_PROVIDER_H
