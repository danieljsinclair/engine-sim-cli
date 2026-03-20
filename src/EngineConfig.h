// EngineConfig.h - Engine configuration and script loading
// Handles all config path resolution, script loading - SRP compliance

#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include <string>
#include <memory>

#include "CLIConfig.h"
#include "engine_sim_bridge.h"
#include "engine_sim_loader.h"

class EngineSimAPI;

// ============================================================================
// EngineConfig - Handles engine configuration and script loading
// Single responsibility: manage engine configuration
// ============================================================================

class EngineConfig {
public:
    EngineConfig();
    ~EngineConfig();
    
    // Create default engine configuration
    static EngineSimConfig createDefault(int sampleRate, int simulationFrequency = EngineConstants::DEFAULT_SIMULATION_FREQUENCY);
    
    // Load engine from config path
    // Returns handle on success, nullptr on failure
    static EngineSimHandle createAndLoad(
        const EngineSimConfig& config,
        const std::string& configPath,
        const std::string& assetBasePath,
        EngineSimAPI& api,
        std::string& error);
    
    // Resolve config path from args - returns both paths
    struct ConfigPaths {
        std::string configPath;
        std::string assetBasePath;
    };
    static ConfigPaths resolveConfigPaths(const char* engineConfig);
    
    // Resolve asset base path from config path
    static std::string resolveAssetBasePath(const std::string& configPath);
    
private:
    static bool resolvePath(const std::string& inputPath, std::string& resolvedPath);
};

#endif // ENGINE_CONFIG_H
