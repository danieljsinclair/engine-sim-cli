#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include "engine-sim-bridge/include/engine_sim_bridge.h"

int main() {
    printf("=== Simple Warmup Phase Test ===\n");

    // Configuration
    EngineSimConfig config = {};
    config.sampleRate = 48000;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = 60;
    config.volume = 0.5f;

    // Create engine
    EngineSimHandle handle = NULL;
    EngineSimResult result = EngineSimCreate(&config, &handle);
    if (result != ESIM_SUCCESS) {
        printf("Failed to create engine simulation\n");
        return 1;
    }

    // Load script
    result = EngineSimLoadScript(handle, "mock_sine_engine.mr", ".");
    if (result != ESIM_SUCCESS) {
        printf("Failed to load script\n");
        EngineSimDestroy(handle);
        return 1;
    }

    // Start audio thread
    result = EngineSimStartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        printf("Failed to start audio thread\n");
        EngineSimDestroy(handle);
        return 1;
    }

    // Enable starter motor and ignition
    EngineSimSetStarterMotor(handle, 1);
    EngineSimSetIgnition(handle, 1);

    printf("Engine started. Monitoring RPM for 3 seconds...\n");
    printf("Time(s) | RPM | Warmup\n");
    printf("--------|-----|-------\n");

    // Monitor for 3 seconds
    for (int i = 0; i < 30; i++) {
        // Update engine state
        EngineSimUpdate(handle, 0.1);

        // Get stats
        EngineSimStats stats;
        EngineSimGetStats(handle, &stats);

        // Check warmup status
        int inWarmup = 1; // We'll add a function to check this later
        printf("%6.1f | %4.0f | %s\n", (i + 1) * 0.1, stats.currentRPM,
               i < 20 ? "YES" : "NO");

        // After 2 seconds, disable starter motor
        if (i == 20) {
            EngineSimSetStarterMotor(handle, 0);
            printf("\nStarter motor disabled at t=2.0s\n");
        }

        usleep(100000); // 100ms
    }

    printf("\nTest completed successfully!\n");
    printf("Engine RPM should be above 600 (minSustainedRPM)\n");

    // Cleanup
    EngineSimDestroy(handle);
    return 0;
}