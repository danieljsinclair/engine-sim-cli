/**
 * @brief Objective-C++ thin bridge between C++ engine-sim-bridge and Swift UI.
 *
 * Wafer-thin wrapper - delegates entirely to C++ IOSRunner.
 * The .mm wrapper only bridges UI controls to C++ IOSRunner and reads
 * state from ITelemetryReader.
 *
 * Usage from Swift:
 *   let wrapper = EngineSimWrapper()
 *   wrapper.start()  // Starts C++ simulation on background thread
 *   wrapper.setThrottle(0.5)
 *   let rpm = wrapper.currentRPM
 *   wrapper.stop()
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

/// Objective-C++ wrapper for iOS simulation
/// Wafer-thin - all logic is in C++ (IOSRunner)
@interface EngineSimWrapper : NSObject

/// Initialize the simulator with default configuration
- (instancetype)init;

/// Start the simulation (C++ IOSRunner::start())
/// @return YES if started successfully
- (BOOL)start;

/// Stop the simulation (C++ IOSRunner::stop())
- (void)stop;

// Controls - forward to C++ IOSRunner
- (void)setThrottle:(double)position;
- (void)setIgnition:(BOOL)enabled;
- (void)setStarter:(BOOL)enabled;

// Telemetry - read from C++ ITelemetryReader
@property (nonatomic, readonly) double currentRPM;
@property (nonatomic, readonly) double currentLoad;
@property (nonatomic, readonly) double exhaustFlow;
@property (nonatomic, readonly) double manifoldPressure;

// Control state (from C++ telemetry)
@property (nonatomic, readonly) double throttlePosition;
@property (nonatomic, readonly) BOOL ignitionEnabled;
@property (nonatomic, readonly) BOOL starterMotorEnabled;

// Audio diagnostics (from C++ telemetry)
@property (nonatomic, readonly) int underrunCount;

// Running state
@property (nonatomic, readonly) BOOL isRunning;

@end

NS_ASSUME_NONNULL_END
