// engine-sim CLI: Simple command-line interface for engine simulation
// Usage: engine-sim-cli <engine_config.mr> <output.wav> [duration_seconds]

#include "engine_sim_bridge.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <vector>

// Simple WAV file writer
struct WaveHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmtChunkMarker[4] = {'f', 'm', 't', ' '};
    uint32_t fmtLength = 16;
    uint16_t audioFormat = 3; // IEEE float
    uint16_t numChannels = 2;
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 48000 * 2 * 4; // sampleRate * channels * bytesPerSample
    uint16_t blockAlign = 2 * 4; // channels * bytesPerSample
    uint16_t bitsPerSample = 32;
    char dataChunkMarker[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};

void printUsage(const char* progName) {
    std::cout << std::flush; std::cout << "Engine Simulator CLI\n";
    std::cout << std::flush; std::cout << "Usage: " << progName << " <engine_config.mr> <output.wav> [duration_seconds]\n";
    std::cout << std::flush; std::cout << "\nExamples:\n";
    std::cout << std::flush; std::cout << "  " << progName << " assets/engines/chevrolet/engine_03_for_e1.mr output.wav 5.0\n";
    std::cout << std::flush; std::cout << "  " << progName << " assets/engines/atg-video-1/01_honda_trx520.mr honda.wav 3.0\n";
    std::cout << std::flush; std::cout << "\nNote: Engine configs must use absolute paths or be relative to current directory.\n";
}

int main(int argc, char* argv[]) {
    std::cout << std::flush; std::cout << "Engine Simulator CLI" << std::endl;
    std::cout << std::flush; std::cout.flush();

    if (argc < 3) {
        printUsage(argv[0]);
        return 1;
    }

    const char* engineConfigPath = argv[1];
    const char* outputWavPath = argv[2];
    const double duration = (argc >= 4) ? std::atof(argv[3]) : 3.0;

    std::cout << std::flush; std::cout << "===================" << std::endl;
    std::cout << std::flush; std::cout << "Config: " << engineConfigPath << std::endl;
    std::cout << std::flush; std::cout << "Output: " << outputWavPath << std::endl;
    std::cout << std::flush; std::cout << "Duration: " << duration << " seconds" << std::endl;
    std::cout << std::flush; std::cout << std::endl;
    std::cout << std::flush; std::cout.flush();

    // Configure simulator
    EngineSimConfig config = {};
    config.sampleRate = 48000;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 48000 * 2; // 2 seconds buffer
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.05;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;

    // Create simulator
    std::cout << std::flush; std::cout << "[DEBUG] Creating simulator...\n";
    EngineSimHandle handle = nullptr;
    EngineSimResult result = EngineSimCreate(&config, &handle);
    std::cout << std::flush; std::cout << "[DEBUG] Create result: " << result << ", handle: " << handle << "\n";
    if (result != ESIM_SUCCESS || handle == nullptr) {
        std::cerr << "ERROR: Failed to create simulator: " << EngineSimGetLastError(handle) << "\n";
        return 1;
    }

    std::cout << std::flush; std::cout << "[1/4] Simulator created successfully\n";

    // Load engine configuration
    std::cout << std::flush; std::cout << "[DEBUG] Loading script: " << engineConfigPath << "\n";
    result = EngineSimLoadScript(handle, engineConfigPath);
    std::cout << std::flush; std::cout << "[DEBUG] Load result: " << result << "\n";
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load engine config: " << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    std::cout << std::flush; std::cout << "[2/4] Engine configuration loaded: " << engineConfigPath << "\n";

    // Start audio processing
    std::cout << std::flush; std::cout << "[DEBUG] Starting audio thread...\n";
    result = EngineSimStartAudioThread(handle);
    std::cout << std::flush; std::cout << "[DEBUG] Start audio result: " << result << "\n";
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to start audio thread: " << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    std::cout << std::flush; std::cout << "[3/4] Audio processing started\n";

    // Calculate audio buffer size
    const int sampleRate = 48000;
    const int channels = 2;
    const int totalFrames = static_cast<int>(duration * sampleRate);
    const int totalSamples = totalFrames * channels;

    std::cout << "Allocating audio buffer: " << totalSamples << " samples (" << (totalSamples * sizeof(float) / 1024.0) << " KB)\n" << std::flush;

    std::vector<float> audioBuffer(totalSamples);

    std::cout << "Audio buffer allocated successfully\n" << std::flush;

    std::cout << "[4/4] Rendering audio (" << totalFrames << " frames)...\n" << std::flush;

    // Simulate and render
    const double updateInterval = 1.0 / 60.0; // 60Hz physics update
    double currentTime = 0.0;
    int framesRendered = 0;
    int lastProgress = 0;

    // Ramp up throttle smoothly
    double throttle = 0.0;
    const double throttleRampDuration = 0.5; // seconds to reach full throttle

    while (currentTime < duration && framesRendered < totalFrames) {
        std::cout << "Update: t=" << currentTime << ", frames=" << framesRendered << "\r" << std::flush;

        // Update physics
        result = EngineSimUpdate(handle, updateInterval);
        if (result != ESIM_SUCCESS) {
            std::cerr << "WARNING: Update failed: " << EngineSimGetLastError(handle) << "\n";
        }

        // Gradually increase throttle
        if (currentTime < throttleRampDuration) {
            throttle = currentTime / throttleRampDuration;
        } else {
            throttle = 1.0; // Full throttle after ramp
        }

        EngineSimSetThrottle(handle, throttle);

        // Render audio (small chunks at a time)
        const int framesToRender = 256; // Small buffer size for real-time simulation
        const int samplesToRender = std::min(framesToRender, totalFrames - framesRendered) * channels;

        if (samplesToRender > 0) {
            int samplesWritten = 0;
            result = EngineSimRender(
                handle,
                audioBuffer.data() + framesRendered * channels,
                samplesToRender / channels,
                &samplesWritten
            );

            if (result == ESIM_SUCCESS && samplesWritten > 0) {
                framesRendered += samplesWritten / channels;
            }
        }

        currentTime += updateInterval;

        // Show progress
        int progress = static_cast<int>(framesRendered * 100 / totalFrames);
        if (progress != lastProgress && progress % 10 == 0) {
            std::cout << std::flush; std::cout << "  Progress: " << progress << "% (" << framesRendered << " frames)\r" << std::flush;
            lastProgress = progress;
        }
    }

    std::cout << std::flush; std::cout << "\n\nRendering complete!\n";

    // Get final statistics
    EngineSimStats stats = {};
    if (EngineSimGetStats(handle, &stats) == ESIM_SUCCESS) {
        std::cout << std::flush; std::cout << "\nFinal Statistics:\n";
        std::cout << std::flush; std::cout << "  RPM: " << static_cast<int>(stats.currentRPM) << "\n";
        std::cout << std::flush; std::cout << "  Load: " << static_cast<int>(stats.currentLoad * 100) << "%\n";
        std::cout << std::flush; std::cout << "  Exhaust Flow: " << stats.exhaustFlow << " m^3/s\n";
        std::cout << std::flush; std::cout << "  Manifold Pressure: " << static_cast<int>(stats.manifoldPressure) << " Pa\n";
    }

    // Cleanup
    EngineSimDestroy(handle);

    // Write WAV file
    std::cout << std::flush; std::cout << "\nWriting WAV file: " << outputWavPath << "\n";
    std::ofstream wavFile(outputWavPath, std::ios::binary);
    if (!wavFile) {
        std::cerr << "ERROR: Failed to create output file\n";
        return 1;
    }

    WaveHeader header = {};
    header.dataSize = totalSamples * sizeof(float);
    header.fileSize = 36 + header.dataSize;

    wavFile.write(reinterpret_cast<const char*>(&header), sizeof(WaveHeader));
    wavFile.write(reinterpret_cast<const char*>(audioBuffer.data()), totalSamples * sizeof(float));
    wavFile.close();

    std::cout << std::flush; std::cout << "Done! Wrote " << totalSamples << " samples (" << totalSamples * sizeof(float) << " bytes)\n";

    return 0;
}
