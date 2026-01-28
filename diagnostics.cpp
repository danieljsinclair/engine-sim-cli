// ============================================================================
// ENGINE SIMULATION DIAGNOSTIC TOOL
// ============================================================================
//
// This diagnostic tool tests each stage of the engine simulation and audio
// pipeline to identify where issues occur. It measures:
//
// Stage 1: Engine Simulation (RPM generation)
// Stage 2: Combustion Events (combustion chamber activity)
// Stage 3: Exhaust Flow (raw exhaust gas flow)
// Stage 4: Synthesizer Input (exhaust flow after conversion)
// Stage 5: Audio Output (final audio samples)
//
// Usage:
//   ./diagnostics <engine_config.mr> [duration_seconds]
//
// Example:
//   ./diagnostics engine-sim/assets/main.mr 5.0
//
// ============================================================================

#include "engine-sim-bridge/include/engine_sim_bridge.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <iomanip>
#include <filesystem>
#include <chrono>

// ============================================================================
// DIAGNOSTIC STATISTICS STRUCTURE
// ============================================================================

struct DiagnosticStats {
    // Stage 1: Engine Simulation
    double minRPM = 1e9;
    double maxRPM = 0.0;
    double avgRPM = 0.0;
    int rpmSamples = 0;

    // Stage 2: Combustion Events
    int totalCombustionEvents = 0;

    // Stage 3: Exhaust Flow (raw)
    double minExhaustFlow = 1e9;
    double maxExhaustFlow = 0.0;
    double avgExhaustFlow = 0.0;
    int exhaustFlowSamples = 0;
    int zeroFlowCount = 0;

    // Stage 4: Synthesizer Input
    double minSynthInput = 1e9;
    double maxSynthInput = 0.0;
    double avgSynthInput = 0.0;
    int synthInputSamples = 0;

    // Stage 5: Audio Output
    int totalFramesRendered = 0;
    int totalSamplesRendered = 0;
    double minAudioLevel = 1e9;
    double maxAudioLevel = 0.0;
    double avgAudioLevel = 0.0;
    int audioLevelSamples = 0;
    int silentFrames = 0;
    int activeFrames = 0;
    int silentSamples = 0;
    int clippedSamples = 0;

    // Buffer status
    int bufferUnderruns = 0;
    int bufferOverruns = 0;
    int successfulReads = 0;
    int failedReads = 0;

    // Data corruption checks
    bool hasNaN = false;
    bool hasInf = false;
    bool hasOutOfRange = false;

    void updateRPM(double rpm) {
        minRPM = std::min(minRPM, rpm);
        maxRPM = std::max(maxRPM, rpm);
        avgRPM = (avgRPM * rpmSamples + rpm) / (rpmSamples + 1);
        rpmSamples++;
    }

    void updateExhaustFlow(double flow) {
        minExhaustFlow = std::min(minExhaustFlow, flow);
        maxExhaustFlow = std::max(maxExhaustFlow, flow);
        avgExhaustFlow = (avgExhaustFlow * exhaustFlowSamples + flow) / (exhaustFlowSamples + 1);
        exhaustFlowSamples++;
        if (flow < 1e-9) {
            zeroFlowCount++;
        }
    }

    void updateSynthInput(double input) {
        minSynthInput = std::min(minSynthInput, input);
        maxSynthInput = std::max(maxSynthInput, input);
        avgSynthInput = (avgSynthInput * synthInputSamples + input) / (synthInputSamples + 1);
        synthInputSamples++;
    }

    void updateAudioLevel(const float* buffer, int frames, int channels) {
        for (int i = 0; i < frames * channels; ++i) {
            float sample = buffer[i];

            // Check for data corruption
            if (std::isnan(sample)) {
                hasNaN = true;
            }
            if (std::isinf(sample)) {
                hasInf = true;
            }
            if (std::abs(sample) > 1.0f) {
                hasOutOfRange = true;
                clippedSamples++;
            }

            double level = std::abs(sample);
            minAudioLevel = std::min(minAudioLevel, level);
            maxAudioLevel = std::max(maxAudioLevel, level);
            avgAudioLevel = (avgAudioLevel * audioLevelSamples + level) / (audioLevelSamples + 1);
            audioLevelSamples++;

            // Track silent samples
            if (level < 1e-10f) {
                silentSamples++;
            }
        }
        totalFramesRendered += frames;
        totalSamplesRendered += frames * channels;

        // Check if frame is silent (all samples below threshold)
        bool frameActive = false;
        for (int i = 0; i < frames * channels; ++i) {
            if (std::abs(buffer[i]) > 1e-6) {
                frameActive = true;
                break;
            }
        }
        if (frameActive) {
            activeFrames++;
        } else {
            silentFrames++;
        }
    }

    void printReport() const {
        std::cout << "\n";
        std::cout << "==========================================\n";
        std::cout << "       DIAGNOSTIC REPORT\n";
        std::cout << "==========================================\n\n";

        // Stage 1: Engine Simulation
        std::cout << "STAGE 1: ENGINE SIMULATION\n";
        std::cout << "----------------------------\n";
        std::cout << "  RPM Range:      " << std::fixed << std::setprecision(1)
                  << minRPM << " - " << maxRPM << " RPM\n";
        std::cout << "  Average RPM:    " << std::fixed << std::setprecision(1)
                  << avgRPM << " RPM\n";
        std::cout << "  Samples:        " << rpmSamples << "\n";
        std::cout << "  Status:         ";
        if (maxRPM > 0) {
            std::cout << "PASS (Engine is simulating)\n";
        } else {
            std::cout << "FAIL (Engine not running - RPM = 0)\n";
        }
        std::cout << "\n";

        // Stage 2: Combustion Events
        std::cout << "STAGE 2: COMBUSTION EVENTS\n";
        std::cout << "----------------------------\n";
        std::cout << "  Total Events:   " << totalCombustionEvents << "\n";
        std::cout << "  Status:         ";
        if (totalCombustionEvents > 0) {
            std::cout << "PASS (Combustion detected)\n";
        } else {
            std::cout << "UNKNOWN (Cannot directly measure combustion events)\n";
            std::cout << "                  Check if RPM > 0 and exhaust flow > 0\n";
        }
        std::cout << "\n";

        // Stage 3: Exhaust Flow (raw)
        std::cout << "STAGE 3: EXHAUST FLOW (RAW)\n";
        std::cout << "----------------------------\n";
        std::cout << "  Flow Range:     " << std::scientific << std::setprecision(2)
                  << minExhaustFlow << " - " << maxExhaustFlow << " m^3/s\n";
        std::cout << "  Average Flow:   " << std::scientific << std::setprecision(2)
                  << avgExhaustFlow << " m^3/s\n";
        std::cout << "  Samples:        " << exhaustFlowSamples << "\n";
        std::cout << "  Zero Flow Count:" << zeroFlowCount << " / " << exhaustFlowSamples << "\n";
        std::cout << "  Status:         ";
        if (maxExhaustFlow > 1e-9) {
            std::cout << "PASS (Exhaust flow detected)\n";
        } else {
            std::cout << "FAIL (No exhaust flow - engine may not be combusting)\n";
        }
        std::cout << "\n";

        // Stage 4: Synthesizer Input
        std::cout << "STAGE 4: SYNTHESIZER INPUT\n";
        std::cout << "----------------------------\n";
        std::cout << "  Input Range:    " << std::scientific << std::setprecision(2)
                  << minSynthInput << " - " << maxSynthInput << "\n";
        std::cout << "  Average Input:  " << std::scientific << std::setprecision(2)
                  << avgSynthInput << "\n";
        std::cout << "  Samples:        " << synthInputSamples << "\n";
        std::cout << "  Status:         ";
        if (synthInputSamples > 0) {
            std::cout << "PASS (Synthesizer receiving data)\n";
        } else {
            std::cout << "UNKNOWN (Synthesizer input not directly measurable)\n";
            std::cout << "                  Check if exhaust flow > 0\n";
        }
        std::cout << "\n";

        // Stage 5: Audio Output
        std::cout << "STAGE 5: AUDIO OUTPUT\n";
        std::cout << "----------------------------\n";
        std::cout << "  Frames Rendered:" << totalFramesRendered << "\n";
        std::cout << "  Samples Rendered:" << totalSamplesRendered << "\n";
        std::cout << "  Audio Level:    " << std::fixed << std::setprecision(6)
                  << minAudioLevel << " - " << maxAudioLevel << "\n";
        std::cout << "  Average Level:  " << std::fixed << std::setprecision(6)
                  << avgAudioLevel << "\n";
        std::cout << "  Active Frames:  " << activeFrames << " / "
                  << (activeFrames + silentFrames) << "\n";
        std::cout << "  Silent Frames:  " << silentFrames << " / "
                  << (activeFrames + silentFrames) << "\n";
        std::cout << "  Silent Samples: " << silentSamples << " / " << totalSamplesRendered
                  << " (" << std::fixed << std::setprecision(1)
                  << (totalSamplesRendered > 0 ? 100.0 * silentSamples / totalSamplesRendered : 0.0) << "%)\n";
        std::cout << "  Clipped Samples:" << clippedSamples << "\n";
        std::cout << "  Status:         ";
        if (hasNaN || hasInf) {
            std::cout << "CORRUPTED (NaN/Inf detected)\n";
        } else if (hasOutOfRange) {
            std::cout << "WARNING (Samples out of range)\n";
        } else if (maxAudioLevel > 1e-6) {
            std::cout << "PASS (Audio samples generated)\n";
        } else {
            std::cout << "FAIL (No audio output - all samples are silent)\n";
        }
        std::cout << "\n";

        // Buffer Status
        std::cout << "BUFFER STATUS\n";
        std::cout << "----------------------------\n";
        std::cout << "  Successful Reads: " << successfulReads << "\n";
        std::cout << "  Failed Reads:     " << failedReads << "\n";
        std::cout << "  Buffer Underruns: " << bufferUnderruns << "\n";
        std::cout << "  Buffer Overruns:  " << bufferOverruns << "\n";
        std::cout << "\n";

        // Overall Summary
        std::cout << "==========================================\n";
        std::cout << "       OVERALL SUMMARY\n";
        std::cout << "==========================================\n";
        std::cout << "  Stage 1 (Engine):    ";
        std::cout << (maxRPM > 0 ? "PASS" : "FAIL") << "\n";
        std::cout << "  Stage 2 (Combustion):";
        std::cout << (maxRPM > 0 && maxExhaustFlow > 1e-9 ? "PASS (inferred)" : "UNKNOWN") << "\n";
        std::cout << "  Stage 3 (Exhaust):   ";
        std::cout << (maxExhaustFlow > 1e-9 ? "PASS" : "FAIL") << "\n";
        std::cout << "  Stage 4 (Synthesizer):";
        std::cout << (synthInputSamples > 0 ? "PASS (inferred)" : "UNKNOWN") << "\n";
        std::cout << "  Stage 5 (Audio):     ";
        if (hasNaN || hasInf) {
            std::cout << "CORRUPTED\n";
        } else if (maxAudioLevel > 1e-6) {
            std::cout << "PASS\n";
        } else {
            std::cout << "FAIL\n";
        }
        std::cout << "==========================================\n";

        // Issues found
        std::cout << "\nISSUES DETECTED\n";
        std::cout << "==========================================\n";
        int issues = 0;
        if (rpmSamples == 0 || maxRPM < 100) {
            std::cout << "  - Engine is not spinning (RPM < 100)\n";
            std::cout << "    -> Check: Starter motor, ignition, throttle\n";
            issues++;
        }
        if (exhaustFlowSamples > 0 && maxExhaustFlow < 0.001) {
            std::cout << "  - No meaningful exhaust flow detected\n";
            std::cout << "    -> Check: Engine simulation, exhaust system configuration\n";
            issues++;
        }
        if (totalSamplesRendered > 0 && silentSamples == totalSamplesRendered) {
            std::cout << "  - Complete silence in audio output\n";
            std::cout << "    -> Check: Synthesizer configuration, impulse responses\n";
            issues++;
        }
        if (hasNaN || hasInf) {
            std::cout << "  - Data corruption detected (NaN/Inf)\n";
            std::cout << "    -> Check: Buffer handling, sample rate conversion\n";
            issues++;
        }
        if (hasOutOfRange) {
            std::cout << "  - Audio samples out of range (> 1.0)\n";
            std::cout << "    -> Check: Volume settings, synthesizer gain\n";
            issues++;
        }
        if (bufferUnderruns > 10) {
            std::cout << "  - Frequent buffer underruns detected\n";
            std::cout << "    -> Check: Audio thread timing, buffer sizes\n";
            issues++;
        }
        if (issues == 0) {
            std::cout << "  No critical issues detected. Audio chain working correctly.\n";
        } else {
            std::cout << "  Found " << issues << " issue(s) requiring attention.\n";
        }
        std::cout << "==========================================\n";
    }
};

// ============================================================================
// WAV FILE WRITER
// ============================================================================

struct WaveHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmtChunkMarker[4] = {'f', 'm', 't', ' '};
    uint32_t fmtLength = 16;
    uint16_t audioFormat = 3; // IEEE float
    uint16_t numChannels = 2;
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 48000 * 2 * 4;
    uint16_t blockAlign = 2 * 4;
    uint16_t bitsPerSample = 32;
    char dataChunkMarker[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};

bool writeWavFile(const char* filename, const float* buffer, int frames, int channels) {
    std::ofstream wavFile(filename, std::ios::binary);
    if (!wavFile) {
        std::cerr << "ERROR: Failed to create output file: " << filename << "\n";
        return false;
    }

    WaveHeader header = {};
    header.dataSize = frames * channels * sizeof(float);
    header.fileSize = 36 + header.dataSize;

    wavFile.write(reinterpret_cast<const char*>(&header), sizeof(WaveHeader));
    wavFile.write(reinterpret_cast<const char*>(buffer), frames * channels * sizeof(float));
    wavFile.close();

    return true;
}

// ============================================================================
// MAIN DIAGNOSTIC ROUTINE
// ============================================================================

int runDiagnostics(const char* engineConfig, double duration, const char* outputPath) {
    const int sampleRate = 48000;
    const int channels = 2;
    const double updateInterval = 1.0 / 60.0;
    const int framesPerUpdate = sampleRate / 60;

    std::cout << "Engine Simulation Diagnostic Tool\n";
    std::cout << "==================================\n";
    std::cout << "Engine Config: " << engineConfig << "\n";
    std::cout << "Duration: " << duration << " seconds\n";
    std::cout << "\n";

    // Configure simulator
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.05;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;

    // Create simulator
    EngineSimHandle handle = nullptr;
    EngineSimResult result = EngineSimCreate(&config, &handle);
    if (result != ESIM_SUCCESS || handle == nullptr) {
        std::cerr << "ERROR: Failed to create simulator: "
                  << EngineSimGetLastError(handle) << "\n";
        return 1;
    }
    std::cout << "[INIT] Simulator created successfully\n";

    // Resolve engine config path to absolute path
    std::string configPath = engineConfig;
    std::string assetBasePath;

    try {
        std::filesystem::path scriptPath(configPath);
        if (scriptPath.is_relative()) {
            scriptPath = std::filesystem::absolute(scriptPath);
        }
        scriptPath = scriptPath.lexically_normal();
        configPath = scriptPath.string();

        // Extract directory for asset resolution
        if (scriptPath.has_parent_path()) {
            std::filesystem::path parentPath = scriptPath.parent_path();
            if (parentPath.filename() == "assets") {
                assetBasePath = (parentPath.parent_path() / "es" / "sound-library").string();
            } else {
                assetBasePath = parentPath.string();
            }
        } else {
            assetBasePath = ".";
        }
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ERROR: Failed to resolve path '" << configPath << "': " << e.what() << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    // Load engine configuration
    result = EngineSimLoadScript(handle, configPath.c_str(), assetBasePath.c_str());
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load engine config: "
                  << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }
    std::cout << "[INIT] Engine configuration loaded\n";

    // Start audio thread
    result = EngineSimStartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        std::cerr << "WARNING: Failed to start audio thread\n";
    } else {
        std::cout << "[INIT] Audio thread started\n";
    }

    // Enable ignition and starter motor
    EngineSimSetIgnition(handle, 1);
    EngineSimSetStarterMotor(handle, 1);
    std::cout << "[INIT] Ignition and starter motor enabled\n";

    // Setup recording buffer
    const int totalFrames = static_cast<int>(duration * sampleRate);
    const int totalSamples = totalFrames * channels;
    std::vector<float> audioBuffer(totalSamples);

    // Initialize diagnostic stats
    DiagnosticStats stats;

    std::cout << "\n";
    std::cout << "Running diagnostics...\n";
    std::cout << "------------------------\n";

    // Warmup sequence
    const double warmupDuration = 2.0;
    double currentTime = 0.0;
    int framesRendered = 0;
    int lastProgress = 0;

    std::cout << "Phase 1: Warming up engine (" << warmupDuration << "s)\n";

    while (currentTime < warmupDuration) {
        // Ramp up throttle during warmup
        double throttle = 0.0;
        if (currentTime < 0.5) {
            throttle = currentTime / 0.5 * 0.5;
        } else if (currentTime < 1.0) {
            throttle = 0.5 + (currentTime - 0.5) / 0.5 * 0.2;
        } else {
            throttle = 0.7 + (currentTime - 1.0) / 1.0 * 0.2;
        }

        EngineSimSetThrottle(handle, throttle);
        EngineSimUpdate(handle, updateInterval);

        // Get and log stats
        EngineSimStats simStats = {};
        EngineSimGetStats(handle, &simStats);
        stats.updateRPM(simStats.currentRPM);
        stats.updateExhaustFlow(simStats.exhaustFlow);

        if (static_cast<int>(currentTime * 2) % 2 == 0) {
            std::cout << "  Warmup: " << std::fixed << std::setprecision(1)
                      << currentTime << "s | RPM: " << std::setprecision(0)
                      << simStats.currentRPM << " | Flow: "
                      << simStats.exhaustFlow << " m3/s\n";
        }

        currentTime += updateInterval;
    }

    // Disable starter motor once engine is running
    EngineSimSetStarterMotor(handle, 0);
    std::cout << "Phase 1 complete. Starter motor disabled.\n\n";

    // Main diagnostic phase
    std::cout << "Phase 2: Collecting diagnostic data (" << duration << "s)\n";

    currentTime = 0.0;
    int updateCount = 0;

    while (currentTime < duration && framesRendered < totalFrames) {
        // Use high throttle for diagnostic purposes
        double throttle = 0.8;
        EngineSimSetThrottle(handle, throttle);

        // Update physics
        EngineSimUpdate(handle, updateInterval);

        // Get stats
        EngineSimStats simStats = {};
        EngineSimGetStats(handle, &simStats);
        stats.updateRPM(simStats.currentRPM);
        stats.updateExhaustFlow(simStats.exhaustFlow);

        // Render audio
        int framesToRender = std::min(framesPerUpdate, totalFrames - framesRendered);
        if (framesToRender > 0) {
            int samplesWritten = 0;
            float* writePtr = audioBuffer.data() + framesRendered * channels;

            result = EngineSimRender(handle, writePtr, framesToRender, &samplesWritten);

            if (result == ESIM_SUCCESS) {
                stats.successfulReads++;

                if (samplesWritten > 0) {
                    stats.updateAudioLevel(writePtr, samplesWritten, channels);
                    framesRendered += samplesWritten;

                    // Check for buffer underrun (requested more than we got)
                    if (samplesWritten < framesToRender) {
                        stats.bufferUnderruns++;
                    }
                } else {
                    stats.bufferUnderruns++;
                }
            } else {
                stats.failedReads++;
            }
        }

        // Progress reporting
        int progress = static_cast<int>(framesRendered * 100 / totalFrames);
        if (progress != lastProgress && progress % 10 == 0) {
            std::cout << "  Progress: " << progress << "% | RPM: "
                      << std::fixed << std::setprecision(0) << simStats.currentRPM
                      << " | Flow: " << std::scientific << std::setprecision(2)
                      << simStats.exhaustFlow << " m3/s\n";
            lastProgress = progress;
        }

        currentTime += updateInterval;
        updateCount++;

        // Detailed logging every 60 updates (1 second)
        if (updateCount % 60 == 0) {
            std::cout << "  [" << std::fixed << std::setprecision(1) << currentTime << "s] "
                      << "RPM: " << std::setprecision(0) << simStats.currentRPM
                      << " | Load: " << std::setprecision(1) << (simStats.currentLoad * 100)
                      << "% | Flow: " << std::scientific << std::setprecision(2)
                      << simStats.exhaustFlow << " m3/s\n";
        }
    }

    std::cout << "\nData collection complete.\n";

    // Cleanup
    EngineSimDestroy(handle);

    // Generate report
    stats.printReport();

    // Save diagnostic audio output
    if (writeWavFile(outputPath, audioBuffer.data(), framesRendered, channels)) {
        std::cout << "\nDiagnostic audio saved to: " << outputPath << "\n";
        std::cout << "You can listen to this file to verify audio output quality.\n";
    } else {
        std::cout << "\nWARNING: Failed to write diagnostic audio to: " << outputPath << "\n";
    }

    return 0;
}

// ============================================================================
// MAIN ENTRY POINT
// ============================================================================

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Usage: " << argv[0] << " <engine_config.mr> [duration_seconds] [--output <path>]\n";
        std::cout << "\nExample:\n";
        std::cout << "  " << argv[0] << " engine-sim/assets/main.mr 5.0\n";
        std::cout << "  " << argv[0] << " engine-sim/assets/main.mr 5.0 --output custom_output.wav\n";
        std::cout << "\nDefault duration: 5.0 seconds\n";
        std::cout << "Default output: diagnostic_output.wav\n";
        return 1;
    }

    const char* engineConfig = argv[1];
    double duration = 5.0;
    const char* outputPath = "diagnostic_output.wav";

    // Parse duration
    if (argc >= 3 && argv[2][0] != '-') {
        duration = std::atof(argv[2]);
        if (duration <= 0) {
            std::cerr << "ERROR: Duration must be positive\n";
            return 1;
        }
    }

    // Parse optional arguments
    for (int i = 2; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--output" && i + 1 < argc) {
            outputPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            std::cout << "Usage: " << argv[0] << " <engine_config.mr> [duration_seconds] [options]\n\n";
            std::cout << "Arguments:\n";
            std::cout << "  engine_config.mr   Path to engine configuration file (required)\n";
            std::cout << "  duration_seconds   Test duration in seconds (default: 5.0)\n\n";
            std::cout << "Options:\n";
            std::cout << "  --output <path>    Output WAV file path (default: diagnostic_output.wav)\n";
            std::cout << "  --help, -h         Show this help\n\n";
            std::cout << "Example:\n";
            std::cout << "  " << argv[0] << " es/v8_engine.mr 10.0 --output test.wav\n";
            return 0;
        }
    }

    return runDiagnostics(engineConfig, duration, outputPath);
}
