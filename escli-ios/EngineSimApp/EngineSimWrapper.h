/**
 * @brief Objective-C++ thin bridge between C++ engine-sim-bridge and Swift UI.
 *
 * Zero simulation logic -- delegates entirely to C++ via the C API in engine_sim_bridge.h.
 *
 * Usage from Swift:
 *   let wrapper = EngineSimWrapper()
 *   wrapper.loadPreset("honda_trx520")
 *   wrapper.startAudioThread()
 *   let rpm = wrapper.currentRPM
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C++ wrapper for engine-sim-bridge C API
@interface EngineSimWrapper : NSObject

/// Initialize the simulator with default configuration
- (instancetype)init;

/// Load an engine script (.mr file) - requires Piranha, not available on iOS
/// @param scriptPath Absolute path to .mr file
/// @param assetBasePath Base path for engine assets (WAV files)
/// @return YES if loaded successfully
- (BOOL)loadScript:(NSString *)scriptPath assetBase:(NSString *)assetBasePath;

/// Load a hardcoded engine preset by ID (no file needed, works on iOS)
/// Available IDs: "honda_trx520", "subaru_ej25", "gm_ls"
/// @param presetId Preset identifier string
/// @return YES if loaded successfully
- (BOOL)loadPreset:(NSString *)presetId;

/// Get the number of available engine presets
+ (NSInteger)presetCount;

/// Get preset display name at index
/// @param index Zero-based preset index
/// @return Display name string
+ (NSString *)presetNameAtIndex:(NSInteger)index;

/// Get preset ID at index
/// @param index Zero-based preset index
/// @return Preset ID string
+ (NSString *)presetIdAtIndex:(NSInteger)index;

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
