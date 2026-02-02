// Audio Diagnostics: Measure latency and detect periodic crackling
//
// This program adds instrumentation to measure:
// 1. Actual latency from throttle change to audio output
// 2. Buffer level oscillations that cause crackling
// 3. Audio thread cycle timing

#include "../engine-sim-bridge/include/engine_sim_bridge.h"
#include <iostream>
#include <fstream>
#include <chrono>
#include <vector>
#include <iomanip>

struct TimingEvent {
    std::chrono::steady_clock::time_point timestamp;
    const char* event;
    double value;  // RPM, throttle, buffer level, etc.
};

std::vector<TimingEvent> g_timingLog;

void logEvent(const char* event, double value = 0.0) {
    auto now = std::chrono::steady_clock::now();
    g_timingLog.push_back({now, event, value});
}

void saveTimingLog(const char* filename) {
    std::ofstream out(filename);
    out << "timestamp_ms,event,value\n";

    auto first = g_timingLog[0].timestamp;
    for (const auto& e : g_timingLog) {
        auto ms = std::chrono::duration_cast<std::chrono::microseconds>(
            e.timestamp - first).count() / 1000.0;
        out << std::fixed << std::setprecision(3)
            << ms << "," << e.event << "," << e.value << "\n";
    }
    std::cout << "Timing log saved to " << filename << "\n";
}

// Modified AudioUnit callback with buffer level tracking
OSStatus diagnosticAudioUnitCallback(
    void* refCon,
    AudioUnitRenderActionFlags* actionFlags,
    const AudioTimeStamp* timeStamp,
    UInt32 busNumber,
    UInt32 numberFrames,
    AudioBufferList* ioData
) {
    // Log buffer level before read
    int bufferLevelBefore = 0;  // TODO: Get actual buffer level
    logEvent("audio_callback_start", bufferLevelBefore);

    // ... existing callback code ...

    // Log buffer level after read
    logEvent("audio_callback_end", numberFrames);

    return noErr;
}

// Test 1: Measure throttle-to-audio latency
void testThrottleLatency(EngineSimHandle handle) {
    std::cout << "\n=== TEST 1: Throttle-to-Audio Latency ===\n";

    // Stabilize at idle
    EngineSimSetThrottle(handle, 0.0);
    for (int i = 0; i < 60; ++i) {  // 1 second at 60Hz
        EngineSimUpdate(handle, 1.0/60.0);
    }

    // Clear timing log
    g_timingLog.clear();
    logEvent("throttle_change_start", 0.0);

    // Make sudden throttle change
    EngineSimSetThrottle(handle, 0.5);
    logEvent("throttle_change_end", 0.5);

    // Track when we see the change in audio
    double lastExhaustFlow = 0.0;
    int framesToDetectChange = 0;

    for (int i = 0; i < 600; ++i) {  // 10 seconds max
        EngineSimStats stats;
        EngineSimGetStats(handle, &stats);

        // Look for significant exhaust flow change
        if (stats.exhaustFlow > lastExhaustFlow + 0.01) {
            logEvent("audio_change_detected", stats.exhaustFlow);
            framesToDetectChange = i;
            break;
        }
        lastExhaustFlow = stats.exhaustFlow;

        EngineSimUpdate(handle, 1.0/60.0);
    }

    // Calculate latency
    if (framesToDetectChange > 0) {
        double latencyMs = (framesToDetectChange / 60.0) * 1000.0;
        std::cout << "Throttle-to-audio latency: " << latencyMs << " ms\n";
        std::cout << "Frames to detect change: " << framesToDetectChange << "\n";
    }

    saveTimingLog("throttle_latency.csv");
}

// Test 2: Detect buffer oscillation (crackling cause)
void testBufferOscillation(EngineSimHandle handle) {
    std::cout << "\n=== TEST 2: Buffer Oscillation Detection ===\n";

    g_timingLog.clear();

    // Run for 5 seconds, logging buffer levels
    for (int i = 0; i < 300; ++i) {  // 5 seconds at 60Hz
        // TODO: Get actual buffer level from synthesizer
        int bufferLevel = 0;  // Placeholder
        logEvent("buffer_level", bufferLevel);

        EngineSimUpdate(handle, 1.0/60.0);
    }

    saveTimingLog("buffer_oscillation.csv");

    // Analyze oscillation
    std::vector<double> levels;
    for (const auto& e : g_timingLog) {
        if (std::string(e.event) == "buffer_level") {
            levels.push_back(e.value);
        }
    }

    // Find min/max
    if (!levels.empty()) {
        auto minMax = std::minmax_element(levels.begin(), levels.end());
        double minLevel = *minMax.first;
        double maxLevel = *minMax.second;
        double oscillation = maxLevel - minLevel;

        std::cout << "Buffer level range: " << minLevel << " to " << maxLevel << "\n";
        std::cout << "Oscillation amplitude: " << oscillation << " samples\n";

        if (oscillation > 1000) {
            std::cout << "WARNING: Large buffer oscillation detected!\n";
            std::cout << "This causes periodic crackling.\n";
        }
    }
}

// Test 3: Audio thread cycle timing
void testAudioThreadTiming(EngineSimHandle handle) {
    std::cout << "\n=== TEST 3: Audio Thread Cycle Timing ===\n";

    g_timingLog.clear();

    // Run for 5 seconds
    for (int i = 0; i < 300; ++i) {
        // TODO: Hook into audio thread to log render events
        logEvent("physics_update", 0);
        EngineSimUpdate(handle, 1.0/60.0);
    }

    saveTimingLog("audio_thread_timing.csv");
}

// Test 4: Measure throttle smoothing effect
void testThrottleSmoothing(EngineSimHandle handle) {
    std::cout << "\n=== TEST 4: Throttle Smoothing Analysis ===\n";

    g_timingLog.clear();

    // Test step response
    double throttle = 0.0;
    double smoothedThrottle = 0.0;

    for (int i = 0; i < 120; ++i) {  // 2 seconds
        // Step input
        if (i == 10) throttle = 0.5;  // Step at 167ms
        if (i == 60) throttle = 0.0;  // Step back at 1000ms

        // Apply smoothing (CLI formula)
        smoothedThrottle = throttle * 0.5 + smoothedThrottle * 0.5;

        EngineSimSetThrottle(handle, smoothedThrottle);
        EngineSimUpdate(handle, 1.0/60.0);

        logEvent("throttle_input", throttle);
        logEvent("throttle_smoothed", smoothedThrottle);
    }

    saveTimingLog("throttle_smoothing.csv");

    // Calculate rise time (10% to 90%)
    bool found10 = false, found90 = false;
    int frame10 = 0, frame90 = 0;

    for (size_t i = 0; i < g_timingLog.size(); i += 2) {
        if (!found10 && g_timingLog[i].value >= 0.05) {  // 10% of 0.5
            frame10 = i / 2;
            found10 = true;
        }
        if (!found90 && g_timingLog[i].value >= 0.45) {  // 90% of 0.5
            frame90 = i / 2;
            found90 = true;
        }
    }

    if (found10 && found90) {
        double riseTimeMs = (frame90 - frame10) * (1000.0 / 60.0);
        std::cout << "Throttle smoothing rise time (10%-90%): " << riseTimeMs << " ms\n";
        std::cout << "This contributes to perceived latency.\n";
    }
}

int main(int argc, char* argv[]) {
    std::cout << "Engine Sim Audio Diagnostics\n";
    std::cout << "============================\n\n";

    // Initialize simulator
    EngineSimConfig config = {};
    config.sampleRate = 44100;
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

    // Load default engine
    result = EngineSimLoadScript(handle,
        "engine-sim-bridge/engine-sim/assets/main.mr",
        "engine-sim-bridge/engine-sim");
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load engine\n";
        EngineSimDestroy(handle);
        return 1;
    }

    // Start audio thread
    result = EngineSimStartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to start audio thread\n";
        EngineSimDestroy(handle);
        return 1;
    }

    // Enable ignition
    EngineSimSetIgnition(handle, 1);
    EngineSimSetStarterMotor(handle, 1);

    std::cout << "Simulator initialized. Running diagnostics...\n";

    // Run tests
    testThrottleLatency(handle);
    testBufferOscillation(handle);
    testAudioThreadTiming(handle);
    testThrottleSmoothing(handle);

    // Cleanup
    EngineSimDestroy(handle);

    std::cout << "\n=== Diagnostics Complete ===\n";
    std::cout << "Review the generated CSV files for detailed analysis.\n";

    return 0;
}
