#import "EngineSimWrapper.h"
#include "simulator/engine_sim_bridge.h"
#include <memory>

@implementation EngineSimWrapper {
    EngineSimHandle _handle;
    EngineSimStats _lastStats;
    BOOL _running;
}

- (instancetype)init {
    self = [super init];
    if (self) {
        _handle = nullptr;
        _running = NO;
        memset(&_lastStats, 0, sizeof(_lastStats));
    }
    return self;
}

- (BOOL)loadScript:(NSString *)scriptPath assetBase:(NSString *)assetBasePath {
    // Create simulator if not already created
    if (!_handle) {
        EngineSimConfig config;
        config.sampleRate = 48000;
        config.inputBufferSize = 1024;
        config.audioBufferSize = 96000;
        config.simulationFrequency = 10000;
        config.fluidSimulationSteps = 8;
        config.targetSynthesizerLatency = 0.05;
        config.volume = 1.0f;
        config.convolutionLevel = 0.5f;
        config.airNoise = 1.0f;
        config.sineMode = 0;

        EngineSimResult result = EngineSimCreate(&config, &_handle);
        if (result != ESIM_SUCCESS) {
            return NO;
        }
    }

    EngineSimResult result = EngineSimLoadScript(
        _handle,
        [scriptPath UTF8String],
        [assetBasePath UTF8String]
    );
    return result == ESIM_SUCCESS;
}

- (BOOL)loadPreset:(NSString *)presetId {
    // Create simulator if not already created
    if (!_handle) {
        EngineSimConfig config;
        config.sampleRate = 48000;
        config.inputBufferSize = 1024;
        config.audioBufferSize = 96000;
        config.simulationFrequency = 10000;
        config.fluidSimulationSteps = 8;
        config.targetSynthesizerLatency = 0.05;
        config.volume = 1.0f;
        config.convolutionLevel = 0.5f;
        config.airNoise = 1.0f;
        config.sineMode = 0;

        EngineSimResult result = EngineSimCreate(&config, &_handle);
        if (result != ESIM_SUCCESS) {
            return NO;
        }
    }

    EngineSimResult result = EngineSimLoadPresetById(
        _handle,
        [presetId UTF8String]
    );
    return result == ESIM_SUCCESS;
}

+ (NSInteger)presetCount {
    return EngineSimGetPresetCount();
}

+ (NSString *)presetNameAtIndex:(NSInteger)index {
    char name[256] = {};
    EngineSimGetPresetInfo((int32_t)index, name, nullptr, sizeof(name), 0);
    return [NSString stringWithUTF8String:name];
}

+ (NSString *)presetIdAtIndex:(NSInteger)index {
    char id[256] = {};
    EngineSimGetPresetInfo((int32_t)index, nullptr, id, 0, sizeof(id));
    return [NSString stringWithUTF8String:id];
}

- (BOOL)startAudioThread {
    if (!_handle) return NO;
    EngineSimResult result = EngineSimStartAudioThread(_handle);
    if (result == ESIM_SUCCESS) {
        _running = YES;
        return YES;
    }
    return NO;
}

- (void)stop {
    if (_handle) {
        EngineSimDestroy(_handle);
        _handle = nullptr;
    }
    _running = NO;
}

- (void)update:(double)deltaTime {
    if (!_handle) return;
    EngineSimUpdate(_handle, deltaTime);
    EngineSimGetStats(_handle, &_lastStats);
}

- (void)setThrottle:(double)position {
    if (_handle) EngineSimSetThrottle(_handle, position);
}

- (void)setIgnition:(BOOL)enabled {
    if (_handle) EngineSimSetIgnition(_handle, enabled ? 1 : 0);
}

- (void)setStarter:(BOOL)enabled {
    if (_handle) EngineSimSetStarterMotor(_handle, enabled ? 1 : 0);
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
    return _running && _handle != nullptr;
}

- (void)dealloc {
    [self stop];
}

@end
