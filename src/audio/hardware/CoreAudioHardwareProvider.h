// CoreAudioHardwareProvider.h - macOS CoreAudio implementation of IAudioHardwareProvider
// Wraps CoreAudio AudioUnit for macOS platform audio output
// OCP: macOS-specific implementation, abstracted behind IAudioHardwareProvider interface
// SRP: Single responsibility - manage CoreAudio AudioUnit lifecycle
// DIP: Depends on IAudioHardwareProvider abstraction, not exposed to clients

#ifndef CORE_AUDIO_HARDWARE_PROVIDER_H
#define CORE_AUDIO_HARDWARE_PROVIDER_H

#include "audio/hardware/IAudioHardwareProvider.h"
#include "ILogging.h"

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <memory>

/**
 * CoreAudioHardwareProvider - macOS CoreAudio implementation of IAudioHardwareProvider
 *
 * This class wraps all CoreAudio AudioUnit operations behind the platform-agnostic
 * IAudioHardwareProvider interface.
 *
 * Responsibilities:
 * - AudioUnit lifecycle (create, initialize, cleanup)
 * - Audio format configuration
 * - Playback control (start/stop)
 * - Volume control
 * - Callback registration and management
 * - Diagnostic state tracking
 *
 * Thread Safety:
 * - Callback must be thread-safe (real-time audio thread)
 * - State access uses appropriate synchronization
 */
class CoreAudioHardwareProvider : public IAudioHardwareProvider {
public:
    /**
     * Constructor with optional logger injection
     * @param logger Optional logger for diagnostics. If nullptr, uses default logger.
     */
    explicit CoreAudioHardwareProvider(ILogging* logger = nullptr);

    ~CoreAudioHardwareProvider() override;

    // ================================================================
    // IAudioHardwareProvider Implementation
    // ================================================================

    bool initialize(const AudioStreamFormat& format) override;
    void cleanup() override;

    bool startPlayback() override;
    void stopPlayback() override;

    void setVolume(double volume) override;
    double getVolume() const override;

    bool registerAudioCallback(const AudioCallback& callback) override;

    AudioHardwareState getHardwareState() const override;
    void resetDiagnostics() override;

private:
    // ================================================================
    // CoreAudio-specific Members
    // ================================================================

    AudioUnit audioUnit;              // CoreAudio AudioUnit instance
    AudioDeviceID deviceID;          // Default output device ID
    bool isPlaying;                  // Playback state
    double currentVolume;              // Current volume level (0.0 to 1.0)
    int underrunCount;               // Buffer underrun counter
    int overrunCount;                // Buffer overrun counter

    // ================================================================
    // Logging
    // ================================================================

    std::unique_ptr<ILogging> defaultLogger_;
    ILogging* logger_;                // Non-null, points to defaultLogger_ or injected logger

    // ================================================================
    // Callback Management
    // ================================================================

    AudioCallback audioCallback_;     // Registered audio callback

    // ================================================================
    // Private Helper Methods
    // ================================================================

    /**
     * Setup CoreAudio AudioUnit with default output device
     * @return true if setup succeeded, false otherwise
     *
     * This method:
     * - Finds default output device
     * - Creates AudioComponentDescription for output unit
     * - Creates AudioUnit from description
     * - Sets up audio stream format
     */
    bool setupAudioUnit();

    /**
     * Configure audio stream format for AudioUnit
     * @param format Audio format specification
     * @return true if configuration succeeded, false otherwise
     *
     * This method:
     * - Converts AudioStreamFormat to CoreAudio AudioStreamBasicDescription
     * - Sets format on AudioUnit using AudioUnitSetProperty
     * - Validates format support
     */
    bool configureAudioFormat(const AudioStreamFormat& format);

    /**
     * Register audio callback with AudioUnit
     * @return true if callback registered successfully, false otherwise
     *
     * This method:
     * - Wraps callback with AURenderCallback structure
     * - Registers callback using AudioUnitSetProperty
     * - Stores context reference for callback invocation
     */
    bool registerCallbackWithAudioUnit();

    /**
     * Static CoreAudio callback wrapper
     *
     * This static function is required by CoreAudio callback mechanism.
     * It extracts the context and invokes the user-provided callback.
     *
     * @param refCon Reference to connection (our context)
     * @param actionFlags Action flags from CoreAudio
     * @param timeStamp Timestamp of callback
     * @param busNumber Bus number (always 0 for simple output)
     * @param numberFrames Number of frames requested
     * @param ioData Audio buffer list to fill
     * @return OSStatus from callback (0 for success)
     */
    static OSStatus coreAudioCallbackWrapper(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    );

    /**
     * Helper to get OSStatus description for logging
     * @param status CoreAudio status code
     * @return Human-readable status description
     */
    static const char* getStatusDescription(OSStatus status);

    /**
     * Log CoreAudio error with detailed information
     * @param operation Name of operation that failed
     * @param status CoreAudio status code
     * @param additional Additional context for error
     */
    void logCoreAudioError(const char* operation, OSStatus status, const char* additional = nullptr);
};

#endif // CORE_AUDIO_HARDWARE_PROVIDER_H
