#import "EngineSimWrapper.h"
#include "simulator/SimulatorFactory.h"
#include "simulator/ISimulator.h"
#include <memory>

@implementation EngineSimWrapper {
    std::unique_ptr<ISimulator> _simulator;
    EngineSimStats _lastStats;
    BOOL _running;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _running = NO;
        memset(&_lastStats, 0, sizeof(_lastStats));
    }
    return self;
}

- (BOOL)loadScript:(NSString *)scriptPath assetBase:(NSString *)assetBasePath {
    @try {
        SimulatorFactoryConfig config;
        config.type = SimulatorType::PistonEngine;
        config.scriptPath = std::string([scriptPath UTF8String]);
        config.assetBasePath = std::string([assetBasePath UTF8String] ?: "");

        _simulator = SimulatorFactory::create(config, nullptr, nullptr);
        return _simulator != nullptr;
    } @catch (...) {
        _simulator.reset();
        return NO;
    }
}

- (BOOL)startAudioThread {
    if (!_simulator) return NO;
    if (_simulator->start()) {
        _running = YES;
        return YES;
    }
    return NO;
}

- (void)stop {
    if (_simulator) {
        if (_running) {
            _simulator->stop();
        }
        _simulator->destroy();
        _simulator.reset();
    }
    _running = NO;
}

- (void)update:(double)deltaTime {
    if (!_simulator) return;
    _simulator->update(deltaTime);
    _lastStats = _simulator->getStats();
}

- (void)setThrottle:(double)position {
    if (_simulator) _simulator->setThrottle(position);
}

- (void)setIgnition:(BOOL)enabled {
    if (_simulator) _simulator->setIgnition(enabled);
}

- (void)setStarter:(BOOL)enabled {
    if (_simulator) _simulator->setStarterMotor(enabled);
}

- (double)currentRPM {
    return _lastStats.currentRPM;
}

- (double)currentLoad {
    return _lastStats.currentLoad;
}

- (double)exhaustFlow {
    return _lastStats.exhaustFlow;
}

- (double)manifoldPressure {
    return _lastStats.manifoldPressure;
}

- (BOOL)isRunning {
    return _running && _simulator != nullptr;
}

- (void)dealloc {
    [self stop];
}

@end
