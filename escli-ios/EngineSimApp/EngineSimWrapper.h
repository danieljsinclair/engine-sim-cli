/**
 * @brief Objective-C++ thin bridge between C++ engine-sim-bridge and Swift UI.
 *
 * Zero simulation logic -- delegates entirely to C++ via ISimulator interface.
 *
 * Usage from Swift:
 *   let wrapper = EngineSimWrapper()
 *   wrapper.loadScript(...)
 *   wrapper.startAudioThread()
 *   let rpm = wrapper.currentRPM
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C++ wrapper for ISimulator interface
@interface EngineSimWrapper : NSObject

/// Initialize the simulator with default configuration
- (instancetype)init;

/// Load an engine script (.mr file)
/// @param scriptPath Absolute path to .mr file
/// @param assetBasePath Base path for engine assets (WAV files)
/// @return YES if loaded successfully
- (BOOL)loadScript:(NSString *)scriptPath assetBase:(NSString *)assetBasePath;

/// Start the audio processing thread
- (BOOL)startAudioThread;

/// Stop and destroy the simulator
- (void)stop;

/// Advance simulation by one tick
/// @param deltaTime Time step in seconds (e.g. 1/60)
- (void)update:(double)deltaTime;

// Controls
- (void)setThrottle:(double)position;
- (void)setIgnition:(BOOL)enabled;
- (void)setStarter:(BOOL)enabled;

// Telemetry (read-only)
@property (nonatomic, readonly) double currentRPM;
@property (nonatomic, readonly) double currentLoad;
@property (nonatomic, readonly) double exhaustFlow;
@property (nonatomic, readonly) double manifoldPressure;
@property (nonatomic, readonly) BOOL isRunning;

@end

NS_ASSUME_NONNULL_END
