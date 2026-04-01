// EngineConfig.h - Engine configuration and script loading
// Handles all config path resolution, script loading - SRP compliance

#ifndef ENGINE_CONFIG_H
#define ENGINE_CONFIG_H

#include <string>

#include "config/CLIconfig.h"
#include "engine_sim_bridge.h"
#include "bridge/engine_sim_loader.h"

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
};

#endif // ENGINE_CONFIG_H
