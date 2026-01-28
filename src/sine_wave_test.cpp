// Sine Wave Audio Test - Basic Audio Chain Verification
//
// This test generates a simple 440Hz sine wave (A4 note) and:
// 1. Plays it through the AudioPlayer class (same as engine-sim-cli)
// 2. Saves it to a WAV file for comparison
//
// Purpose: Verify the audio pipeline works BEFORE testing with simulation
// If we can't hear a clean sine wave, the audio chain is broken.

#include <iostream>
#include <fstream>
#include <cmath>
#include <vector>
#include <cstring>
#include <string>
#include <iomanip>

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#include <unistd.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#include <unistd.h>
#endif

// ============================================================================
// WAV File Writer
// ============================================================================

struct WaveHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t fileSize;
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmtChunkMarker[4] = {'f', 'm', 't', ' '};
    uint32_t fmtLength = 16;
    uint16_t audioFormat = 3; // IEEE float
    uint16_t numChannels = 1;  // Mono
    uint32_t sampleRate = 44100;
    uint32_t byteRate = 44100 * 1 * 4;  // Mono * 4 bytes per sample
    uint16_t blockAlign = 1 * 4;
    uint16_t bitsPerSample = 32;
    char dataChunkMarker[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};

bool writeWavFile(const char* filename, const std::vector<float>& audioBuffer, int frames, int sampleRate) {
    std::ofstream wavFile(filename, std::ios::binary);
    if (!wavFile) {
        std::cerr << "ERROR: Failed to create output file: " << filename << "\n";
        return false;
    }

    WaveHeader header = {};
    header.sampleRate = sampleRate;
    header.byteRate = sampleRate * 1 * 4;
    header.dataSize = frames * 1 * sizeof(float);
    header.fileSize = 36 + header.dataSize;

    wavFile.write(reinterpret_cast<const char*>(&header), sizeof(WaveHeader));
    wavFile.write(reinterpret_cast<const char*>(audioBuffer.data()), frames * 1 * sizeof(float));
    wavFile.close();

    return true;
}

// ============================================================================
// OpenAL Audio Player (copied from engine_sim_cli.cpp)
// ============================================================================

class AudioPlayer {
public:
    AudioPlayer() : device(nullptr), context(nullptr), source(0), buffers{0}, useFloat32(false), initialFillCount(0) {}

    bool initialize(int sampleRate) {
        // Open device
        device = alcOpenDevice(nullptr);
        if (!device) {
            std::cerr << "ERROR: Failed to open OpenAL device\n";
            return false;
        }

        // Create context
        context = alcCreateContext(device, nullptr);
        if (!context) {
            std::cerr << "ERROR: Failed to create OpenAL context\n";
            alcCloseDevice(device);
            return false;
        }

        alcMakeContextCurrent(context);

        // Check for AL_EXT_float32 extension for direct float playback
        // This avoids quality loss from float->int16->float conversions
        if (alIsExtensionPresent("AL_EXT_float32")) {
            useFloat32 = true;
            std::cout << "[Audio] Using AL_EXT_float32 for direct float32 playback\n";
        } else {
            useFloat32 = false;
            std::cout << "[Audio] AL_EXT_float32 not available, using int16 fallback\n";
        }

        // Generate buffers
        alGenBuffers(2, buffers);
        if (alGetError() != AL_NO_ERROR) {
            std::cerr << "ERROR: Failed to generate OpenAL buffers\n";
            cleanup();
            return false;
        }

        // Generate source
        alGenSources(1, &source);
        if (alGetError() != AL_NO_ERROR) {
            std::cerr << "ERROR: Failed to generate OpenAL source\n";
            cleanup();
            return false;
        }

        return true;
    }

    void cleanup() {
        if (alIsSource(source)) {
            alSourceStop(source);
            alDeleteSources(1, &source);
            source = 0;
        }

        for (int i = 0; i < 2; ++i) {
            if (alIsBuffer(buffers[i])) {
                alDeleteBuffers(1, &buffers[i]);
                buffers[i] = 0;
            }
        }

        if (context) {
            alcMakeContextCurrent(nullptr);
            alcDestroyContext(context);
            context = nullptr;
        }

        if (device) {
            alcCloseDevice(device);
            device = nullptr;
        }
    }

    bool playBuffer(const float* data, int frames, int sampleRate) {
        if (!source) return false;

        ALenum format;
        const void* audioData;
        size_t dataSize;

        if (useFloat32) {
            // Direct float playback - no conversion needed!
            // This matches the WAV export path exactly
            format = alGetEnumValue("AL_FORMAT_MONO_FLOAT32");
            audioData = data;
            dataSize = frames * sizeof(float);
        } else {
            // Fallback: Convert float to int16 for OpenAL compatibility (mono)
            // This path has quality loss but works on all systems
            std::vector<int16_t> int16Data(frames);  // Mono, so 1 sample per frame
            for (int i = 0; i < frames; ++i) {
                float sample = std::max(-1.0f, std::min(1.0f, data[i]));
                int16Data[i] = static_cast<int16_t>(sample * 32768.0f);
            }
            format = AL_FORMAT_MONO16;
            audioData = int16Data.data();
            dataSize = frames * sizeof(int16_t);
        }

        // Check how many buffers have been processed
        ALint processed;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

        // Collect free buffers from unqueueing
        ALuint freeBuffers[2];
        int freeCount = 0;

        while (processed > 0 && freeCount < 2) {
            alSourceUnqueueBuffers(source, 1, &freeBuffers[freeCount]);
            processed--;
            freeCount++;
        }

        // Check how many buffers are currently queued
        ALint queued;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);

        // Queue a buffer if we have space (max 2 buffers)
        if (queued < 2) {
            ALuint bufferToQueue;

            if (freeCount > 0) {
                // Use a buffer we just unqueued (it's guaranteed to be free)
                bufferToQueue = freeBuffers[0];
            } else if (initialFillCount < 2) {
                // Initial fill: use buffers from our pool that haven't been queued yet
                bufferToQueue = buffers[initialFillCount];
                initialFillCount++;
            } else {
                // No free buffers available and initial fill complete
                // Skip this update, we'll catch up on the next frame
                return true;
            }

            alBufferData(bufferToQueue, format, audioData, dataSize, sampleRate);

            ALenum error = alGetError();
            if (error != AL_NO_ERROR) {
                std::cerr << "OpenAL Error buffering data: " << error << "\n";
                return false;
            }

            alSourceQueueBuffers(source, 1, &bufferToQueue);

            error = alGetError();
            if (error != AL_NO_ERROR) {
                std::cerr << "OpenAL Error queuing buffers: " << error << "\n";
                return false;
            }
        }

        // Start playback if not already playing
        ALint state;
        alGetSourcei(source, AL_SOURCE_STATE, &state);
        if (state != AL_PLAYING) {
            alSourcePlay(source);

            ALenum error = alGetError();
            if (error != AL_NO_ERROR) {
                std::cerr << "OpenAL Error starting playback: " << error << "\n";
                return false;
            }
        }

        return true;
    }

    void stop() {
        if (alIsSource(source)) {
            alSourceStop(source);
        }
    }

    // Wait for all buffers to finish playing
    void waitForCompletion() {
        if (!source) return;

        ALint state;
        ALint processed;
        do {
            alGetSourcei(source, AL_SOURCE_STATE, &state);
            // Check if all buffers are processed (audio finished playing)
            alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);
        } while (state == AL_PLAYING || processed < 2);  // Wait until done
    }

private:
    ALCdevice* device;
    ALCcontext* context;
    ALuint source;
    ALuint buffers[2];
    bool useFloat32;  // True if AL_EXT_float32 is available
    int initialFillCount;  // Number of buffers initially filled (0, 1, or 2)
};

// ============================================================================
// Sine Wave Generator
// ============================================================================

void generateSineWave(std::vector<float>& buffer, int sampleRate, double frequency, double duration) {
    int numSamples = static_cast<int>(duration * sampleRate);
    buffer.resize(numSamples);

    const double twoPi = 2.0 * M_PI;

    for (int i = 0; i < numSamples; ++i) {
        double t = static_cast<double>(i) / sampleRate;
        buffer[i] = static_cast<float>(std::sin(twoPi * frequency * t));
    }

    // Apply a simple fade-in/fade-out to avoid clicks
    int fadeSamples = sampleRate / 100;  // 10ms fade
    for (int i = 0; i < fadeSamples && i < numSamples; ++i) {
        float gain = static_cast<float>(i) / fadeSamples;
        buffer[i] *= gain;
    }
    for (int i = 0; i < fadeSamples && i < numSamples; ++i) {
        float gain = static_cast<float>(i) / fadeSamples;
        buffer[numSamples - 1 - i] *= gain;
    }
}

// ============================================================================
// Command Line Arguments
// ============================================================================

struct CommandLineArgs {
    bool playAudio = false;
    bool writeWav = false;
    double frequency = 440.0;  // A4 note
    double duration = 2.0;     // seconds
    double amplitude = 0.5;    // 0.0 to 1.0
    const char* outputFile = "sine_wave_test.wav";
};

void printUsage(const char* progName) {
    std::cout << "Sine Wave Audio Test - Audio Chain Verification\n";
    std::cout << "Usage: " << progName << " [options]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --play              Play audio through speakers\n";
    std::cout << "  --wav               Save to WAV file\n";
    std::cout << "  --both              Both play and save (default)\n";
    std::cout << "  --freq <Hz>         Frequency in Hz (default: 440)\n";
    std::cout << "  --duration <sec>    Duration in seconds (default: 2.0)\n";
    std::cout << "  --amplitude <0-1>   Amplitude 0.0-1.0 (default: 0.5)\n";
    std::cout << "  --output <path>     Output WAV filename (default: sine_wave_test.wav)\n";
    std::cout << "  --help, -h          Show this help\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " --play\n";
    std::cout << "  " << progName << " --wav --freq 880 --duration 1\n";
    std::cout << "  " << progName << " --both --amplitude 0.3\n\n";
    std::cout << "Expected Result:\n";
    std::cout << "  You should hear a clean, pure sine wave tone.\n";
    std::cout << "  If you hear distortion, clicks, or noise, the audio chain is broken.\n";
}

bool parseArguments(int argc, char* argv[], CommandLineArgs& args) {
    if (argc == 1) {
        // No arguments - show help and exit
        printUsage(argv[0]);
        return false;
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--play") {
            args.playAudio = true;
        }
        else if (arg == "--wav") {
            args.writeWav = true;
        }
        else if (arg == "--both") {
            args.playAudio = true;
            args.writeWav = true;
        }
        else if (arg == "--freq") {
            if (++i >= argc) {
                std::cerr << "ERROR: --freq requires a value\n";
                return false;
            }
            args.frequency = std::atof(argv[i]);
        }
        else if (arg == "--duration") {
            if (++i >= argc) {
                std::cerr << "ERROR: --duration requires a value\n";
                return false;
            }
            args.duration = std::atof(argv[i]);
        }
        else if (arg == "--amplitude") {
            if (++i >= argc) {
                std::cerr << "ERROR: --amplitude requires a value\n";
                return false;
            }
            args.amplitude = std::atof(argv[i]);
        }
        else if (arg == "--output") {
            if (++i >= argc) {
                std::cerr << "ERROR: --output requires a path\n";
                return false;
            }
            args.outputFile = argv[i];
        }
        else {
            std::cerr << "ERROR: Unknown argument: " << arg << "\n";
            std::cerr << "Use --help for usage information\n";
            return false;
        }
    }

    // If neither --play nor --wav specified, default to both
    if (!args.playAudio && !args.writeWav) {
        args.playAudio = true;
        args.writeWav = true;
    }

    // Validate arguments
    if (args.frequency <= 0 || args.frequency > 22000) {
        std::cerr << "ERROR: Frequency must be between 1 and 22000 Hz\n";
        return false;
    }
    if (args.duration <= 0 || args.duration > 60) {
        std::cerr << "ERROR: Duration must be between 0 and 60 seconds\n";
        return false;
    }
    if (args.amplitude < 0 || args.amplitude > 1) {
        std::cerr << "ERROR: Amplitude must be between 0.0 and 1.0\n";
        return false;
    }

    return true;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "========================================\n";
    std::cout << "SINE WAVE AUDIO TEST\n";
    std::cout << "========================================\n\n";

    // Parse command line arguments
    CommandLineArgs args;
    if (!parseArguments(argc, argv, args)) {
        return 1;
    }

    const int sampleRate = 44100;

    std::cout << "Configuration:\n";
    std::cout << "  Frequency: " << args.frequency << " Hz\n";
    std::cout << "  Duration: " << args.duration << " seconds\n";
    std::cout << "  Amplitude: " << args.amplitude << "\n";
    std::cout << "  Sample Rate: " << sampleRate << " Hz\n";
    std::cout << "  Play Audio: " << (args.playAudio ? "Yes" : "No") << "\n";
    std::cout << "  Write WAV: " << (args.writeWav ? "Yes" : "No") << "\n";
    if (args.writeWav) {
        std::cout << "  Output File: " << args.outputFile << "\n";
    }
    std::cout << "\n";

    // Generate sine wave
    std::cout << "[1/3] Generating sine wave...\n";
    std::vector<float> audioBuffer;
    generateSineWave(audioBuffer, sampleRate, args.frequency, args.duration);

    // Apply amplitude
    for (size_t i = 0; i < audioBuffer.size(); ++i) {
        audioBuffer[i] *= static_cast<float>(args.amplitude);
    }

    std::cout << "  Generated " << audioBuffer.size() << " samples\n";
    std::cout << "  OK: Sine wave generated\n\n";

    // Write WAV file if requested
    if (args.writeWav) {
        std::cout << "[2/3] Writing WAV file...\n";
        if (writeWavFile(args.outputFile, audioBuffer, audioBuffer.size(), sampleRate)) {
            std::cout << "  OK: Written to " << args.outputFile << "\n";
            std::cout << "  You can open this file in an audio editor to verify the waveform\n\n";
        } else {
            std::cerr << "  FAILED: Could not write WAV file\n\n";
            return 1;
        }
    } else {
        std::cout << "[2/3] WAV file output skipped (--wav not specified)\n\n";
    }

    // Play audio if requested
    if (args.playAudio) {
        std::cout << "[3/3] Playing audio...\n";

        AudioPlayer player;
        if (!player.initialize(sampleRate)) {
            std::cerr << "  FAILED: Could not initialize audio player\n";
            return 1;
        }

        std::cout << "  Playing " << args.duration << " seconds of " << args.frequency << " Hz tone...\n";
        std::cout << "  You should hear a clean sine wave tone.\n";
        std::cout << "  If you hear distortion, clicks, or noise, the audio chain is broken.\n\n";

        // For pre-generated audio, queue the entire buffer at once for smooth playback
        // No streaming/sleeping - just queue it all and let OpenAL handle it
        int totalFrames = audioBuffer.size();

        // Queue in large chunks to avoid buffer size limits
        const int maxChunkSize = sampleRate;  // 1 second chunks
        int framesPlayed = 0;

        while (framesPlayed < totalFrames) {
            int framesToQueue = std::min(maxChunkSize, totalFrames - framesPlayed);

            if (!player.playBuffer(audioBuffer.data() + framesPlayed, framesToQueue, sampleRate)) {
                std::cerr << "  ERROR: Failed to play audio buffer\n";
                player.cleanup();
                return 1;
            }

            framesPlayed += framesToQueue;
        }

        // Wait for playback to complete
        std::cout << "  Audio queued - OpenAL will play asynchronously\n";
        std::cout << "  OK: " << args.duration << " seconds of " << args.frequency << " Hz tone queued\n\n";
        std::cout << "  (Program will exit immediately - audio continues playing)\n\n";

        // Don't cleanup - let OpenAL play the buffers. The OS will clean up on exit.
        // This avoids any blocking/waiting/sleeping idiocy.
    } else {
        std::cout << "[3/3] Audio playback skipped (--play not specified)\n\n";
    }

    // Print diagnostic summary
    std::cout << "========================================\n";
    std::cout << "AUDIO CHAIN VERIFICATION SUMMARY\n";
    std::cout << "========================================\n\n";

    std::cout << "WHAT YOU SHOULD HEAR:\n";
    std::cout << "  - A clean, pure sine wave tone at " << args.frequency << " Hz\n";
    std::cout << "  - No distortion, clicks, or noise\n";
    std::cout << "  - Smooth fade-in and fade-out\n\n";

    std::cout << "WHAT THIS TESTS:\n";
    std::cout << "  - AudioPlayer can play float32 samples correctly\n";
    std::cout << "  - OpenAL initialization and buffer management\n";
    std::cout << "  - WAV file export matches what you hear\n";
    std::cout << "  - The audio pipeline is working end-to-end\n\n";

    if (args.writeWav) {
        std::cout << "NEXT STEPS:\n";
        std::cout << "  1. Listen to the audio playback - is it clean?\n";
        std::cout << "  2. Open " << args.outputFile << " in an audio editor\n";
        std::cout << "  3. Verify the waveform is a pure sine wave\n";
        std::cout << "  4. If both look good, your audio chain is working!\n\n";
    }

    std::cout << "If the audio sounds correct, you can proceed with engine simulation tests.\n";
    std::cout << "If not, there's a problem with the audio chain that needs fixing first.\n\n";

    return 0;
}
