// EngineConfig.cpp - Engine configuration and script loading

#include "EngineConfig.h"

EngineConfig::EngineConfig() {}
EngineConfig::~EngineConfig() {}

EngineSimConfig EngineConfig::createDefault(int sampleRate, int simulationFrequency) {
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = simulationFrequency > 0 ? simulationFrequency : EngineConstants::DEFAULT_SIMULATION_FREQUENCY;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;
    config.sineMode = 0;  // Default: no sine mode (requires engine script)
    return config;
}

EngineSimHandle EngineConfig::createAndLoad(
    const EngineSimConfig& config,
    const std::string& configPath,
    const std::string& assetBasePath,
    EngineSimAPI& api,
    std::string& error)
{
    EngineSimHandle handle = nullptr;
    EngineSimResult result = api.Create(&config, &handle);
    if (result != ESIM_SUCCESS || !handle) {
        error = "Failed to create simulator";
        return nullptr;
    }

    // Load script separately
    if (!configPath.empty()) {
        result = api.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());
        if (result != ESIM_SUCCESS) {
            error = "Failed to load script";
            api.Destroy(handle);
            return nullptr;
        }
    }

    return handle;
}
