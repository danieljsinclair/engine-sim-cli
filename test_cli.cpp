#include <iostream>
#include "engine_sim_bridge.h"

int main() {
    std::cout << "Testing EngineSim Bridge API...\n";

    // Create simulator
    EngineSimConfig config = {};
    config.sampleRate = 48000;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.05;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;

    EngineSimHandle handle = nullptr;
    EngineSimResult result = EngineSimCreate(&config, &handle);

    if (result != ESIM_SUCCESS || handle == nullptr) {
        std::cerr << "ERROR: Failed to create simulator\n";
        return 1;
    }

    std::cout << "✓ Simulator created successfully\n";

    // Test loading default engine
    result = EngineSimLoadScript(handle, "../engine-sim/assets/main.mr", "../engine-sim/assets");
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load engine: " << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    std::cout << "✓ Engine configuration loaded\n";

    // Enable starter
    result = EngineSimSetStarterMotor(handle, 1);
    if (result != ESIM_SUCCESS) {
        std::cerr << "WARNING: Failed to enable starter motor\n";
    } else {
        std::cout << "✓ Starter motor enabled\n";
    }

    // Test throttle setting (RPM is controlled via throttle)
    result = EngineSimSetThrottle(handle, 0.5);
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to set throttle: " << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    std::cout << "✓ Throttle set to 50%\n";

    // Update simulation
    result = EngineSimUpdate(handle, 0.1);
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to update simulation\n";
        EngineSimDestroy(handle);
        return 1;
    }

    std::cout << "✓ Simulation updated\n";

    // Get stats
    EngineSimStats stats = {};
    EngineSimGetStats(handle, &stats);
    std::cout << "✓ Current RPM: " << static_cast<int>(stats.currentRPM) << "\n";

    EngineSimDestroy(handle);
    std::cout << "✓ Simulator destroyed\n";

    std::cout << "\nAll tests passed! Bridge API is working correctly.\n";
    return 0;
}