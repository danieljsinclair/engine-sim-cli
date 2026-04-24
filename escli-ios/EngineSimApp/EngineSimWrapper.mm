// EngineSimWrapper.mm - Wafer-thin Objective-C++ bridge for iOS
// Delegates to C++ IOSRunner - no shadow state, no simulator logic

#import "EngineSimWrapper.h"

// C++ bridge headers
#include "IOSRunner.h"
#include "telemetry/ITelemetryProvider.h"

@implementation EngineSimWrapper {
    IOSRunner* _runner;  // Owned by this wrapper, deleted in stop/dealloc
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _runner = new IOSRunner();
    }
    return self;
}

- (BOOL)start {
    if (!_runner) {
        return NO;
    }
    return _runner->start();
}

- (void)stop {
    if (_runner) {
        _runner->stop();
    }
}

- (void)dealloc {
    if (_runner) {
        delete _runner;
        _runner = nullptr;
    }
}

// ============================================================================
// Controls - forward to C++ IOSRunner
// ============================================================================

- (void)setThrottle:(double)position {
    if (_runner) {
        _runner->setThrottle(position);
    }
}

- (void)setIgnition:(BOOL)enabled {
    if (_runner) {
        _runner->setIgnition(enabled);
    }
}

- (void)setStarter:(BOOL)enabled {
    if (_runner) {
        _runner->setStarterMotor(enabled);
    }
}

// ============================================================================
// Telemetry - read from C++ ITelemetryReader
// ============================================================================

- (double)currentRPM {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto state = telemetry->getEngineState();
            return state.currentRPM;
        }
    }
    return 0.0;
}

- (double)currentLoad {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto state = telemetry->getEngineState();
            return state.currentLoad;
        }
    }
    return 0.0;
}

- (double)exhaustFlow {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto state = telemetry->getEngineState();
            return state.exhaustFlow;
        }
    }
    return 0.0;
}

- (double)manifoldPressure {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto state = telemetry->getEngineState();
            return state.manifoldPressure;
        }
    }
    return 0.0;
}

- (double)throttlePosition {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto inputs = telemetry->getVehicleInputs();
            return inputs.throttlePosition;
        }
    }
    return 0.0;
}

- (BOOL)ignitionEnabled {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto inputs = telemetry->getVehicleInputs();
            return inputs.ignitionOn;
        }
    }
    return NO;
}

- (BOOL)starterMotorEnabled {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto inputs = telemetry->getVehicleInputs();
            return inputs.starterMotorEngaged;
        }
    }
    return NO;
}

- (int)underrunCount {
    if (_runner) {
        auto telemetry = _runner->getTelemetryReader();
        if (telemetry) {
            auto diag = telemetry->getAudioDiagnostics();
            return diag.underrunCount;
        }
    }
    return 0;
}

- (BOOL)isRunning {
    if (_runner) {
        return _runner->isRunning();
    }
    return NO;
}

@end
