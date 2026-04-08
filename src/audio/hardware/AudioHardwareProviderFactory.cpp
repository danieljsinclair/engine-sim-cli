// AudioHardwareProviderFactory.cpp - Factory for creating platform-specific audio hardware providers
// Implements factory pattern for creating IAudioHardwareProvider implementations
// OCP: New platforms can be added by extending this factory
// DIP: Client code depends on IAudioHardwareProvider, not concrete implementations

#include "audio/hardware/IAudioHardwareProvider.h"
#include "audio/hardware/CoreAudioHardwareProvider.h"
#include "ILogging.h"

#include <memory>

// ================================================================
// Platform Detection
// ================================================================

/**
 * Detect current platform and return appropriate hardware provider
 *
 * This function uses platform-specific macros to determine which
 * IAudioHardwareProvider implementation to create.
 *
 * @param logger Optional logger for diagnostics
 * @return Unique pointer to created provider, or nullptr if unsupported platform
 */
std::unique_ptr<IAudioHardwareProvider> AudioHardwareProviderFactory::createProvider(ILogging* logger) {
    // Platform detection logic
    // For now, only macOS is supported
    // Future platforms (iOS, ESP32, etc.) can be added here

#if defined(__APPLE__) && defined(__MACH__)
    #if TARGET_OS_MAC
        // macOS platform - use CoreAudio implementation
        return std::make_unique<CoreAudioHardwareProvider>(logger);
    #elif TARGET_OS_IPHONE
        // iOS platform - would use AVAudioEngine implementation (TODO)
        // For now, return nullptr as iOS is not yet implemented
        return nullptr;
    #endif
#else
    // Other platforms not yet supported
    // Future: ESP32, Windows, Linux, etc.
    return nullptr;
#endif
}
