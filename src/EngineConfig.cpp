// EngineConfig.cpp - Engine configuration and script loading

#include "EngineConfig.h"

#include <iostream>
#include <filesystem>

EngineConfig::EngineConfig() {}
EngineConfig::~EngineConfig() {}

EngineSimConfig EngineConfig::createDefault(int sampleRate) {
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;
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
    
    result = api.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());
    if (result != ESIM_SUCCESS) {
        error = "Failed to load config: " + std::string(api.GetLastError(handle));
        api.Destroy(handle);
        return nullptr;
    }
    
    return handle;
}

EngineConfig::ConfigPaths EngineConfig::resolveConfigPaths(const char* engineConfig) {
    ConfigPaths paths;
    
    // Determine config path
    if (engineConfig) {
        paths.configPath = engineConfig;
    }
    else {
        // use default
        paths.configPath = "engine-sim-bridge/engine-sim/assets/main.mr";
    }
    
    // Resolve asset base path
    paths.assetBasePath = resolveAssetBasePath(paths.configPath);
    
    return paths;
}

std::string EngineConfig::resolveAssetBasePath(const std::string& configPath) {
    try {
        std::filesystem::path scriptPath(configPath);
        
        // Make absolute if relative
        if (scriptPath.is_relative()) {
            scriptPath = std::filesystem::absolute(scriptPath);
        }
        scriptPath = scriptPath.lexically_normal();
        
        std::string assetBase = "engine-sim-bridge/engine-sim";
        
        if (scriptPath.has_parent_path()) {
            std::filesystem::path parentPath = scriptPath.parent_path();
            if (parentPath.filename() == "assets") {
                assetBase = parentPath.parent_path().string();
            } else {
                assetBase = parentPath.string();
            }
        }
        
        // Make absolute
        std::filesystem::path assetPath(assetBase);
        if (assetPath.is_relative()) {
            assetPath = std::filesystem::absolute(assetPath);
        }
        assetPath = assetPath.lexically_normal();
        
        return assetPath.string();
        
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ERROR: Failed to resolve path: " << e.what() << "\n";
        return "engine-sim-bridge/engine-sim";
    }
}

bool EngineConfig::resolvePath(const std::string& inputPath, std::string& resolvedPath) {
    try {
        std::filesystem::path path(inputPath);
        if (path.is_relative()) {
            path = std::filesystem::absolute(path);
        }
        path = path.lexically_normal();
        resolvedPath = path.string();
        return true;
    } catch (const std::filesystem::filesystem_error&) {
        return false;
    }
}
