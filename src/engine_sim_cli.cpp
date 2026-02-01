// engine-sim CLI: Interactive command-line interface for engine simulation
//
// Features:
// - Load engine configurations from .mr files
// - RPM control via --rpm or interactive mode
// - Load control via --load or interactive mode
// - Interactive keyboard control with --interactive
// - Real-time audio playback with --play
// - WAV file export

#include "engine_sim_bridge.h"
#include "sine_wave_generator.h"

#include <iostream>
#include <fstream>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <algorithm>
#include <iomanip>
#include <filesystem>

#include <AudioUnit/AudioUnit.h>
#include <AudioToolbox/AudioToolbox.h>
#include <CoreAudio/CoreAudio.h>

// Terminal handling for interactive mode
#ifdef _WIN32
#include <conio.h>
#include <windows.h>
#else
#include <termios.h>
#include <unistd.h>
#include <fcntl.h>
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
    uint16_t numChannels = 2;
    uint32_t sampleRate = 44100;  // Default to 44.1kHz (will be overridden)
    uint32_t byteRate = 44100 * 2 * 4;  // Will be overridden
    uint16_t blockAlign = 2 * 4;
    uint16_t bitsPerSample = 32;
    char dataChunkMarker[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};

// ============================================================================
// CoreAudio AudioUnit Player (macOS/iOS)
// Real-time streaming audio with no queuing latency
// ============================================================================

// AudioUnit callback context - stores engine simulator handle for rendering
struct AudioUnitContext {
    EngineSimHandle engineHandle;         // Engine simulator handle
    std::atomic<bool> isPlaying;          // Playback state
    AudioUnitContext() : engineHandle(nullptr), isPlaying(false) {}
};

class AudioPlayer {
public:
    AudioPlayer() : audioUnit(nullptr), deviceID(0),
                    isPlaying(false), sampleRate(0),
                    context(nullptr) {
    }

    ~AudioPlayer() {
        cleanup();
    }

    bool initialize(int sr) {
        sampleRate = sr;

        // Create callback context
        context = new AudioUnitContext();

        // Set up audio format - PCM float32 stereo
        AudioStreamBasicDescription format = {};
        format.mSampleRate = sampleRate;
        format.mFormatID = kAudioFormatLinearPCM;
        format.mFormatFlags = kLinearPCMFormatFlagIsFloat | kLinearPCMFormatFlagIsPacked;
        format.mBytesPerPacket = 2 * sizeof(float);  // Stereo
        format.mFramesPerPacket = 1;
        format.mBytesPerFrame = 2 * sizeof(float);   // Stereo float
        format.mChannelsPerFrame = 2;                // Stereo
        format.mBitsPerChannel = 8 * sizeof(float);

        // Create AudioUnit (AUHAL - Audio Unit Hardware Abstraction Layer)
        AudioComponentDescription desc = {};
        desc.componentType = kAudioUnitType_Output;
        desc.componentSubType = kAudioUnitSubType_DefaultOutput;
        desc.componentManufacturer = kAudioUnitManufacturer_Apple;
        desc.componentFlags = 0;
        desc.componentFlagsMask = 0;

        AudioComponent component = AudioComponentFindNext(nullptr, &desc);
        if (!component) {
            std::cerr << "ERROR: Failed to find AudioComponent\n";
            delete context;
            context = nullptr;
            return false;
        }

        OSStatus status = AudioComponentInstanceNew(component, &audioUnit);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to create AudioUnit: " << status << "\n";
            delete context;
            context = nullptr;
            return false;
        }

        // Set format for output
        status = AudioUnitSetProperty(
            audioUnit,
            kAudioUnitProperty_StreamFormat,
            kAudioUnitScope_Input,
            0,
            &format,
            sizeof(format)
        );

        if (status != noErr) {
            std::cerr << "ERROR: Failed to set AudioUnit format: " << status << "\n";
            cleanup();
            return false;
        }

        // Set up render callback
        AURenderCallbackStruct callbackStruct = {};
        callbackStruct.inputProc = audioUnitCallback;
        callbackStruct.inputProcRefCon = context;

        status = AudioUnitSetProperty(
            audioUnit,
            kAudioUnitProperty_SetRenderCallback,
            kAudioUnitScope_Input,
            0,
            &callbackStruct,
            sizeof(callbackStruct)
        );

        if (status != noErr) {
            std::cerr << "ERROR: Failed to set AudioUnit callback: " << status << "\n";
            cleanup();
            return false;
        }

        // Initialize AudioUnit
        status = AudioUnitInitialize(audioUnit);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to initialize AudioUnit: " << status << "\n";
            cleanup();
            return false;
        }

        // Get default output device for diagnostics
        AudioObjectPropertyAddress propertyAddress = {
            kAudioHardwarePropertyDefaultOutputDevice,
            kAudioObjectPropertyScopeGlobal,
            kAudioObjectPropertyElementMain
        };

        UInt32 deviceIDSize = sizeof(deviceID);
        status = AudioObjectGetPropertyData(
            kAudioObjectSystemObject,
            &propertyAddress,
            0,
            nullptr,
            &deviceIDSize,
            &deviceID
        );

        if (status != noErr) {
            std::cerr << "WARNING: Could not get audio device ID\n";
        }

        std::cout << "[Audio] AudioUnit initialized at " << sampleRate << " Hz (stereo float32)\n";
        std::cout << "[Audio] Real-time streaming mode (no queuing latency)\n";
        return true;
    }

    void cleanup() {
        if (audioUnit) {
            AudioOutputUnitStop(audioUnit);
            AudioUnitUninitialize(audioUnit);
            AudioComponentInstanceDispose(audioUnit);
            audioUnit = nullptr;
        }

        if (context) {
            delete context;
            context = nullptr;
        }

        isPlaying = false;
    }

    // Set the engine simulator handle for audio rendering
    void setEngineHandle(EngineSimHandle handle) {
        if (context) {
            context->engineHandle = handle;
        }
    }

    // Start playback - begins real-time streaming
    bool start() {
        if (!audioUnit) return false;

        OSStatus status = AudioOutputUnitStart(audioUnit);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to start AudioUnit: " << status << "\n";
            return false;
        }

        isPlaying = true;
        if (context) {
            context->isPlaying.store(true);
        }
        return true;
    }

    // Stop playback
    void stop() {
        if (audioUnit && isPlaying) {
            AudioOutputUnitStop(audioUnit);
            isPlaying = false;
            if (context) {
                context->isPlaying.store(false);
            }
        }
    }

    // In real-time streaming mode, we don't need playBuffer
    // The callback is invoked automatically by hardware when it needs samples
    bool playBuffer(const float* data, int frames, int sampleRate) {
        // This method is kept for compatibility but does nothing in streaming mode
        // The AudioUnit callback handles all audio rendering
        if (!isPlaying) {
            start();
        }
        return true;
    }

    void waitForCompletion() {
        // In streaming mode, we just wait a bit for final samples to play
        if (isPlaying) {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }
    }

private:
    AudioUnit audioUnit;
    AudioDeviceID deviceID;
    bool isPlaying;
    int sampleRate;
    AudioUnitContext* context;

    // Static callback for real-time audio rendering
    // This is called by the audio hardware when it needs samples
    // CRITICAL: Must be real-time safe (no allocations, no blocking, no locks)
    static OSStatus audioUnitCallback(
        void* refCon,
        AudioUnitRenderActionFlags* actionFlags,
        const AudioTimeStamp* timeStamp,
        UInt32 busNumber,
        UInt32 numberFrames,
        AudioBufferList* ioData
    ) {
        AudioUnitContext* ctx = static_cast<AudioUnitContext*>(refCon);

        // Check if we should be playing
        if (!ctx || !ctx->isPlaying.load()) {
            // Output silence
            for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
                AudioBuffer& buffer = ioData->mBuffers[i];
                float* data = static_cast<float*>(buffer.mData);
                std::memset(data, 0, buffer.mDataByteSize);
            }
            return noErr;
        }

        // Check if we have an engine handle
        if (!ctx->engineHandle) {
            // Output silence
            for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
                AudioBuffer& buffer = ioData->mBuffers[i];
                float* data = static_cast<float*>(buffer.mData);
                std::memset(data, 0, buffer.mDataByteSize);
            }
            return noErr;
        }

        // Render audio directly to hardware buffer (real-time streaming)
        // This is the key difference from AudioQueue - no queuing, direct write
        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            float* data = static_cast<float*>(buffer.mData);

            // Calculate how many frames we can write
            UInt32 framesToWrite = numberFrames;
            if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
                framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
            }

            // Read from engine simulator audio buffer
            // This is NON-BLOCKING - if there's not enough data, we get what's available
            int32_t samplesRead = 0;
            EngineSimResult result = EngineSimReadAudioBuffer(
                ctx->engineHandle,
                data,
                framesToWrite,
                &samplesRead
            );

            // If we didn't get enough samples, fill the rest with silence
            if (samplesRead < static_cast<int32_t>(framesToWrite)) {
                int silenceFrames = framesToWrite - samplesRead;
                float* silenceStart = data + samplesRead * 2;  // Stereo
                std::memset(silenceStart, 0, silenceFrames * 2 * sizeof(float));
            }
        }

        return noErr;
    }
};

// ============================================================================
// Terminal Keyboard Input (Non-blocking)
// ============================================================================

class KeyboardInput {
public:
    KeyboardInput() : oldSettings{}, initialized(false) {
#ifndef _WIN32
        tcgetattr(STDIN_FILENO, &oldSettings);
        termios newSettings = oldSettings;
        newSettings.c_lflag &= ~(ICANON | ECHO);
        newSettings.c_cc[VMIN] = 0;
        newSettings.c_cc[VTIME] = 0;
        tcsetattr(STDIN_FILENO, TCSANOW, &newSettings);

        // Set non-blocking
        int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
        fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK);
        initialized = true;
#endif
    }

    ~KeyboardInput() {
#ifndef _WIN32
        if (initialized) {
            tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
        }
#endif
    }

    int getKey() {
#ifdef _WIN32
        return _kbhit() ? _getch() : -1;
#else
        if (initialized) {
            char c;
            if (read(STDIN_FILENO, &c, 1) > 0) {
                return c;
            }
        }
        return -1;
#endif
    }

private:
#ifndef _WIN32
    termios oldSettings;
    bool initialized;
#endif
};

// ============================================================================
// RPM Controller (Simple P-Controller)
// ============================================================================

class RPMController {
public:
    RPMController() : targetRPM(0), kp(0.8), integral(0), ki(0.05), lastError(0), firstUpdate(true) {}

    void setTargetRPM(double rpm) {
        targetRPM = rpm;
        integral = 0;  // Reset integral when target changes
    }

    double update(double currentRPM, double dt) {
        if (targetRPM <= 0) return 0.0;

        double error = targetRPM - currentRPM;

        // P-term
        double pTerm = error * kp;

        // I-term with anti-windup
        if (firstUpdate) {
            integral = 0;
            firstUpdate = false;
        }

        // Reduce integral term to prevent windup
        integral = std::max(-100.0, std::min(100.0, integral + error * dt));
        double iTerm = integral * ki;

        // D-term - reduce to prevent overshoot
        double dTerm = (error - lastError) / dt * 0.05;
        lastError = error;

        // Calculate throttle
        double throttle = pTerm + iTerm + dTerm;

        // Clamp throttle
        return std::max(0.0, std::min(1.0, throttle));
    }

private:
    double targetRPM;
    double kp;
    double ki;
    double integral;
    double lastError;
    bool firstUpdate;
};

// ============================================================================
// Global State
// ============================================================================

static std::atomic<bool> g_running(true);
static std::atomic<bool> g_interactiveMode(false);

void signalHandler(int signal) {
    g_running.store(false);
}

// ============================================================================
// Command Line Arguments
// ============================================================================

struct CommandLineArgs {
    const char* engineConfig = nullptr;
    const char* outputWav = nullptr;
    double duration = 3.0;
    double targetRPM = 0.0;
    double targetLoad = -1.0;  // -1 means auto (RPM control)
    bool interactive = false;
    bool playAudio = false;
    bool useDefaultEngine = false;
    bool sineMode = false;  // Generate sine wave test tone instead of engine audio
};

void printUsage(const char* progName) {
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "Usage: " << progName << " [options] <engine_config.mr> <output.wav>\n";
    std::cout << "   OR: " << progName << " --script <engine_config.mr> [options] [output.wav]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --script <path>      Path to engine .mr configuration file\n";
    std::cout << "  --rpm <value>        Target RPM to maintain (default: auto)\n";
    std::cout << "  --load <0-100>       FIXED throttle load percentage (ignored in interactive mode)\n";
    std::cout << "  --interactive        Enable interactive keyboard control (overrides --load)\n";
    std::cout << "  --play, --play-audio Play audio to speakers in real-time\n";
    std::cout << "  --duration <seconds> Duration in seconds (default: 3.0, ignored in interactive)\n";
    std::cout << "  --output <path>      Output WAV file path\n";
    std::cout << "  --default-engine     Use default engine from main repo (ignores config file)\n";
    std::cout << "  --sine               Generate 440Hz sine wave test tone (no engine sim)\n\n";
    std::cout << "NOTES:\n";
    std::cout << "  --load sets a FIXED throttle for non-interactive mode only\n";
    std::cout << "  In interactive mode, use J/K or Up/Down arrows to control load\n";
    std::cout << "  Use --rpm for RPM control mode (throttle auto-adjusts)\n\n";
    std::cout << "Interactive Controls:\n";
    std::cout << "  A                      Toggle ignition on/off (starts ON)\n";
    std::cout << "  S                      Toggle starter motor on/off\n";
    std::cout << "  UP/DOWN Arrows or K/J  Increase/decrease throttle\n";
    std::cout << "  W                      Increase throttle\n";
    std::cout << "  SPACE                  Apply brake\n";
    std::cout << "  R                      Reset to idle\n";
    std::cout << "  Q/ESC                  Quit\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " --script v8_engine.mr --rpm 850 --duration 5 --output output.wav\n";
    std::cout << "  " << progName << " --script v8_engine.mr --interactive --play\n";
    std::cout << "  " << progName << " --script engine-sim-bridge/engine-sim/assets/main.mr --interactive --output recording.wav\n";
    std::cout << "  " << progName << " --default-engine --rpm 2000 --play --output engine.wav\n";
}

bool parseArguments(int argc, char* argv[], CommandLineArgs& args) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];

        if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return false;
        }
        else if (arg == "--rpm") {
            if (++i >= argc) {
                std::cerr << "ERROR: --rpm requires a value\n";
                return false;
            }
            args.targetRPM = std::atof(argv[i]);
        }
        else if (arg == "--load") {
            if (++i >= argc) {
                std::cerr << "ERROR: --load requires a value\n";
                return false;
            }
            args.targetLoad = std::atof(argv[i]) / 100.0;
        }
        else if (arg == "--interactive") {
            args.interactive = true;
            g_interactiveMode.store(true);
        }
        else if (arg == "--play" || arg == "--play-audio") {
            args.playAudio = true;
        }
        else if (arg == "--script") {
            if (++i >= argc) {
                std::cerr << "ERROR: --script requires a path\n";
                return false;
            }
            args.engineConfig = argv[i];
        }
        else if (arg == "--duration") {
            if (++i >= argc) {
                std::cerr << "ERROR: --duration requires a value\n";
                return false;
            }
            args.duration = std::atof(argv[i]);
        }
        else if (arg == "--default-engine") {
            args.useDefaultEngine = true;
        }
        else if (arg == "--output") {
            if (++i >= argc) {
                std::cerr << "ERROR: --output requires a path\n";
                return false;
            }
            args.outputWav = argv[i];
        }
        else if (arg == "--sine") {
            args.sineMode = true;
        }
        else if (args.useDefaultEngine && args.outputWav == nullptr) {
            // When using default engine, first positional arg is output file
            args.outputWav = argv[i];
        }
        else if (!args.useDefaultEngine && args.engineConfig == nullptr) {
            args.engineConfig = argv[i];
        }
        else if (args.outputWav == nullptr) {
            args.outputWav = argv[i];
        }
        else {
            std::cerr << "ERROR: Unknown argument: " << arg << "\n";
            return false;
        }
    }

    // Use default engine if requested
    if (args.useDefaultEngine) {
        // Will use main.mr from main repo
        args.engineConfig = "(default engine)";
    }

    // Engine config is required unless in sine mode
    if (args.engineConfig == nullptr && !args.sineMode) {
        std::cerr << "ERROR: Engine configuration file is required\n";
        std::cerr << "       Use --script <path>, --sine, or provide positional argument\n\n";
        printUsage(argv[0]);
        return false;
    }

    // Validate arguments
    if (args.targetRPM < 0 || args.targetRPM > 20000) {
        std::cerr << "ERROR: RPM must be between 0 and 20000\n";
        return false;
    }

    if (args.targetLoad < -1.0 || args.targetLoad > 1.0) {
        std::cerr << "ERROR: Load must be between 0 and 100\n";
        return false;
    }

    // Auto-enable RPM mode if target RPM is specified and load is not
    if (args.targetRPM > 0 && args.targetLoad < 0) {
        args.targetLoad = -1.0;  // Auto mode
    }

    return true;
}

// ============================================================================
// Display Interactive HUD
// ============================================================================

void displayHUD(double rpm, double throttle, double targetRPM, const EngineSimStats& stats) {
    std::cout << "\r";
    std::cout << "[" << std::fixed << std::setprecision(0) << std::setw(4) << rpm << " RPM] ";
    std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(throttle * 100) << "%] ";
    if (targetRPM > 0) {
        std::cout << "[Target: " << std::setw(4) << static_cast<int>(targetRPM) << " RPM] ";
    }
    std::cout << "[Flow: " << std::setprecision(2) << stats.exhaustFlow << " m3/s] ";
    std::cout << std::flush;
}

// ============================================================================
// Main Simulation Loop
// ============================================================================

int runSimulation(const CommandLineArgs& args) {
    // CRITICAL: Use 44100 Hz to match GUI (engine_sim_application.cpp line 169-177)
    // GUI uses 44.1kHz for all audio synthesis parameters
    const int sampleRate = 44100;
    const int channels = 2;
    const double updateInterval = 1.0 / 60.0;
    const int framesPerUpdate = sampleRate / 60;

    // ============================================================================
    // SINE MODE: Generate pure sine wave test tone (no engine simulation)
    // ============================================================================
    if (args.sineMode) {
        std::cout << "\n=== SINE WAVE TEST MODE ===\n";
        std::cout << "Generating 440Hz sine wave test tone\n";
        std::cout << "This bypasses all engine simulation to test the audio path\n\n";

        // Configure sine wave generator
        SineWaveConfig config;
        config.frequency = 440.0;       // A4 note
        config.duration = args.duration;
        config.amplitude = 0.5;         // 50% volume
        config.sampleRate = sampleRate;
        config.channels = channels;

        // Generate the sine wave
        std::cout << "[Generating sine wave audio...]\n";
        std::vector<float> audioBuffer;
        generateSineWave(audioBuffer, config);
        std::cout << "[Generated " << audioBuffer.size() / channels << " frames ("
                  << config.duration << " seconds)]\n\n";

        // Initialize audio player for real-time playback
        AudioPlayer* audioPlayer = nullptr;
        if (args.playAudio) {
            audioPlayer = new AudioPlayer();
            if (!audioPlayer->initialize(sampleRate)) {
                std::cerr << "ERROR: Failed to initialize audio player\n";
                delete audioPlayer;
                return 1;
            }
            std::cout << "[Audio initialized for real-time playback]\n";
        }

        // Play audio in streaming mode using AudioUnit callback
        // For sine wave, we need to implement a different approach since we don't have an engine handle
        // We'll use the traditional buffer approach for sine wave
        if (audioPlayer) {
            const int playbackChunkSize = 512;  // ~11.6ms @ 44.1kHz
            int totalFrames = audioBuffer.size() / channels;
            int framesPlayed = 0;

            std::cout << "[Playing " << config.duration << " seconds of " << config.frequency << " Hz tone...]\n";

            while (framesPlayed < totalFrames) {
                int framesToQueue = std::min(playbackChunkSize, totalFrames - framesPlayed);

                if (!audioPlayer->playBuffer(audioBuffer.data() + framesPlayed * channels,
                                            framesToQueue, sampleRate)) {
                    std::cerr << "WARNING: Failed to queue audio chunk\n";
                }

                framesPlayed += framesToQueue;

                // Display progress
                int progress = static_cast<int>(framesPlayed * 100 / totalFrames);
                std::cout << "  Progress: " << progress << "% (" << framesPlayed << " frames)\r" << std::flush;
            }
            std::cout << "\n";

            // Wait for playback to complete
            std::cout << "[Waiting for audio playback to finish...]\n";
            audioPlayer->waitForCompletion();
            std::cout << "[Playback complete]\n";
        }

        // Write to WAV file if requested
        if (args.outputWav) {
            std::cout << "[Writing to WAV file: " << args.outputWav << "]\n";
            std::ofstream wavFile(args.outputWav, std::ios::binary);
            if (!wavFile) {
                std::cerr << "ERROR: Failed to create output file\n";
                delete audioPlayer;
                return 1;
            }

            int totalFrames = audioBuffer.size() / channels;
            WaveHeader header = {};
            header.numChannels = channels;
            header.sampleRate = sampleRate;
            header.byteRate = sampleRate * channels * sizeof(float);
            header.blockAlign = channels * sizeof(float);
            header.dataSize = audioBuffer.size() * sizeof(float);
            header.fileSize = 36 + header.dataSize;

            wavFile.write(reinterpret_cast<const char*>(&header), sizeof(WaveHeader));
            wavFile.write(reinterpret_cast<const char*>(audioBuffer.data()), audioBuffer.size() * sizeof(float));
            wavFile.close();

            std::cout << "Done! Wrote " << audioBuffer.size() << " samples ("
                      << totalFrames << " frames, " << config.duration << " seconds)\n";
        }

        // Cleanup
        delete audioPlayer;
        return 0;
    }

    // ============================================================================
    // ENGINE SIMULATION MODE
    // ============================================================================

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
        std::cerr << "ERROR: Failed to create simulator: " << EngineSimGetLastError(handle) << "\n";
        return 1;
    }

    std::cout << "[1/5] Simulator created successfully\n";

    // Load engine configuration
    std::string configPath = args.engineConfig;
    std::string assetBasePath;
    std::string piranhaLibraryPath;

    // Resolve Piranha library path to absolute path
    try {
        std::filesystem::path esPath("engine-sim-bridge/engine-sim/es");
        if (esPath.is_relative()) {
            esPath = std::filesystem::absolute(esPath);
        }
        esPath = esPath.lexically_normal();
        piranhaLibraryPath = esPath.string();
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ERROR: Failed to resolve Piranha library path: " << e.what() << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    if (args.useDefaultEngine) {
        // Use main.mr from the engine-sim submodule
        std::filesystem::path defaultScriptPath("engine-sim-bridge/engine-sim/assets/main.mr");
        // Note: The impulse response filenames already include "es/sound-library/" prefix,
        // so assetBasePath should point to the parent directory (engine-sim-bridge/engine-sim)
        std::filesystem::path defaultAssetPath("engine-sim-bridge/engine-sim");

        if (defaultScriptPath.is_relative()) {
            defaultScriptPath = std::filesystem::absolute(defaultScriptPath);
        }
        if (defaultAssetPath.is_relative()) {
            defaultAssetPath = std::filesystem::absolute(defaultAssetPath);
        }

        defaultScriptPath = defaultScriptPath.lexically_normal();
        defaultAssetPath = defaultAssetPath.lexically_normal();

        configPath = defaultScriptPath.string();
        assetBasePath = defaultAssetPath.string();
    } else {
        // Resolve to absolute path using std::filesystem
        try {
            std::filesystem::path scriptPath(configPath);
            if (scriptPath.is_relative()) {
                // Convert relative path to absolute based on current working directory
                scriptPath = std::filesystem::absolute(scriptPath);
            }
            // Normalize the path (resolve . and ..)
            scriptPath = scriptPath.lexically_normal();
            configPath = scriptPath.string();

            // Extract directory from config path for asset resolution
            // For engine-sim scripts, find the es/sound-library directory
            if (scriptPath.has_parent_path()) {
                std::filesystem::path parentPath = scriptPath.parent_path();
                // Check if we're in the assets directory - if so, use es/sound-library
                if (parentPath.filename() == "assets") {
                    // Script is in assets/, go up to engine-sim then to es/sound-library
                    assetBasePath = (parentPath.parent_path() / "es" / "sound-library").string();
                } else {
                    // For other scripts, try to find es/sound-library relative to script
                    assetBasePath = parentPath.string();
                }
            } else {
                // Use current directory if no parent path
                assetBasePath = ".";
            }
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "ERROR: Failed to resolve path '" << configPath << "': " << e.what() << "\n";
            EngineSimDestroy(handle);
            return 1;
        }
    }

    // Note: All paths have been converted to absolute paths, so we don't need to change directories.
    // The Piranha compiler can now find imports using the absolute paths provided.
    // Load the engine configuration script with absolute paths
    result = EngineSimLoadScript(handle, configPath.c_str(), assetBasePath.c_str());

    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load engine config: " << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }

    std::cout << "[2/5] Engine configuration loaded: " << configPath << "\n";
    std::cout << "[2.5/5] Impulse responses loaded automatically\n";

    // CRITICAL: Start the audio thread like GUI does (engine_sim_application.cpp line 509)
    // This enables asynchronous audio rendering which eliminates delay
    result = EngineSimStartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to start audio thread: " << EngineSimGetLastError(handle) << "\n";
        EngineSimDestroy(handle);
        return 1;
    }
    std::cout << "[3/5] Audio thread started (asynchronous rendering)\n";

    // Auto-enable ignition so the engine can run
    EngineSimSetIgnition(handle, 1);
    if (!args.interactive) {
        std::cout << "[4/5] Ignition enabled (auto)\n";
    } else {
        std::cout << "[4/5] Ignition enabled (ready for start) - Press 'S' for starter motor\n";
    }

    // Initialize audio player if requested
    AudioPlayer* audioPlayer = nullptr;
    if (args.playAudio) {
        audioPlayer = new AudioPlayer();
        if (!audioPlayer->initialize(sampleRate)) {
            std::cerr << "WARNING: Failed to initialize audio player, continuing without playback\n";
            delete audioPlayer;
            audioPlayer = nullptr;
        } else {
            // Set engine handle for AudioUnit callback
            audioPlayer->setEngineHandle(handle);
            // Start real-time streaming immediately
            if (!audioPlayer->start()) {
                std::cerr << "WARNING: Failed to start audio playback\n";
            }
            std::cout << "[5/5] AudioUnit audio player initialized (real-time streaming)\n";
        }
    } else {
        std::cout << "[5/5] Audio playback disabled (WAV export mode)\n";
    }

    // Setup recording buffer
    // For smooth audio: allocate full duration buffer, write sequentially (like sine wave test)
    const int bufferFrames = static_cast<int>(args.duration * sampleRate);
    const int totalSamples = bufferFrames * channels;
    std::vector<float> audioBuffer(totalSamples);

    // Audio accumulation for smooth playback - track total frames rendered
    int framesRendered = 0;  // Total frames rendered (for both WAV and playback)
    // Note: AudioUnit streaming mode doesn't need chunking - callback handles it in real-time

    // Initialize controller
    RPMController rpmController;
    if (args.targetRPM > 0) {
        rpmController.setTargetRPM(args.targetRPM);
        std::cout << "[5/5] RPM controller set to " << args.targetRPM << " RPM\n";
    } else if (args.targetLoad >= 0) {
        std::cout << "[5/5] Load mode set to " << static_cast<int>(args.targetLoad * 100) << "%\n";
    } else {
        std::cout << "[5/5] Auto throttle mode\n";
    }

    std::cout << "\nStarting simulation...\n";

    // Setup keyboard input for interactive mode
    KeyboardInput* keyboardInput = nullptr;
    double interactiveTargetRPM = args.targetRPM;
    double interactiveLoad = (args.targetLoad >= 0) ? args.targetLoad : 0.0;
    double baselineLoad = interactiveLoad;  // Remember baseline for W key decay
    bool wKeyPressed = false;  // Track if W is currently pressed

    if (args.interactive) {
        keyboardInput = new KeyboardInput();
        std::cout << "\nInteractive mode enabled. Press Q to quit.\n\n";
        std::cout << "Interactive Controls:\n";
        std::cout << "  A - Toggle ignition (starts ON)\n";
        std::cout << "  S - Toggle starter motor\n";
        std::cout << "  W - Increase throttle\n";
        std::cout << "  SPACE - Brake\n";
        std::cout << "  R - Reset to idle\n";
        std::cout << "  J/K or Down/Up - Decrease/Increase load\n";
        std::cout << "  Q/ESC - Quit\n\n";
    }

    // Main simulation loop
    double currentTime = 0.0;
    int framesProcessed = 0;  // For progress tracking
    int lastProgress = 0;
    double throttle = 0.0;
    double smoothedThrottle = 0.0;  // For throttle smoothing (matches GUI line 798)
    auto loopStartTime = std::chrono::steady_clock::now();

    // Initial warmup - use RPM controller if target is set
    const double warmupDuration = 2.0;  // Longer warmup for combustion
    if (args.targetRPM > 0) {
        // Gradual throttle ramp during warmup
        std::cout << "Starting warmup sequence...\n";
        double warmupThrottle = 0.0;
        while (currentTime < warmupDuration) {
            EngineSimStats stats = {};
            EngineSimGetStats(handle, &stats);

            // Use sufficient throttle during warmup to ensure strong combustion
            // Higher airflow allows more fuel/air mixture for combustion torque
            if (currentTime < 1.0) {
                warmupThrottle = 0.5;  // Medium throttle for initial start
            } else if (currentTime < 1.5) {
                warmupThrottle = 0.7;  // Higher throttle for combustion development
            } else {
                warmupThrottle = 0.8;  // High throttle ready for starter disable
            }

            // Apply smoothing even during warmup
            smoothedThrottle = warmupThrottle * 0.5 + smoothedThrottle * 0.5;
            EngineSimSetThrottle(handle, smoothedThrottle);
            EngineSimUpdate(handle, updateInterval);
            currentTime += updateInterval;

            if (static_cast<int>(currentTime * 2) % 2 == 0) {  // Print every 0.5 seconds
                std::cout << "  Warmup: " << stats.currentRPM << " RPM, Throttle: " << warmupThrottle << "\n";
            }
        }
        std::cout << "Warmup complete. Starting RPM control...\n";
    } else {
        // Fixed warmup for auto mode
        while (currentTime < warmupDuration) {
            EngineSimUpdate(handle, updateInterval);
            // Use higher throttle during warmup to ensure strong combustion development
            // At 0.5-0.7 throttle, we get 52-72% airflow which should provide enough
            // fuel/air mixture for the engine to develop combustion torque
            double warmupThrottle;
            if (currentTime < 1.0) {
                warmupThrottle = 0.5;  // Medium throttle for initial start
            } else {
                warmupThrottle = 0.7;  // Higher throttle for combustion development
            }

            // Apply smoothing
            smoothedThrottle = warmupThrottle * 0.5 + smoothedThrottle * 0.5;
            EngineSimSetThrottle(handle, smoothedThrottle);
            currentTime += updateInterval;
        }
    }

    currentTime = 0.0;

    // CRITICAL: Enable starter motor to start the engine!
    // Without this, the engine never cranks and never generates audio
    EngineSimSetStarterMotor(handle, 1);

    while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
        // Get current stats
        EngineSimStats stats = {};
        EngineSimGetStats(handle, &stats);

        // Disable starter motor once engine is running fast enough to sustain combustion
        // This particular engine seems to max out around 550-600 RPM on the starter
        // Try disabling at 550 RPM to see if it can sustain on its own
        const double minSustainedRPM = 550.0;
        if (stats.currentRPM > minSustainedRPM && EngineSimSetStarterMotor(handle, 0) == ESIM_SUCCESS) {
            static bool starterDisabled = false;
            if (!starterDisabled) {
                std::cout << "Engine started! Disabling starter motor at " << stats.currentRPM << " RPM.\n";
                starterDisabled = true;
            }
        }
        else if (stats.currentRPM < minSustainedRPM / 2) {
            // Re-enable starter if engine speed drops too low (failed to start)
            static bool starterReenabled = false;
            if (!starterReenabled) {
                EngineSimSetStarterMotor(handle, 1);
                std::cout << "Engine speed too low. Re-enabling starter motor.\n";
                starterReenabled = true;
            }
        }

        // Handle keyboard input in interactive mode
        if (args.interactive && keyboardInput) {
            // Track previous key to detect repeat vs new press
            static int lastKey = -1;
            int key = keyboardInput->getKey();

            // Reset key state when no key is pressed (key is released)
            if (key < 0) {
                lastKey = -1;
                wKeyPressed = false;  // W key released, enable decay
            } else if (key != lastKey) {
                // Only process if this is a new key press (not a repeat)
                switch (key) {
                    case 27:  // ESC
                    case 'q':
                    case 'Q':
                        g_running.store(false);
                        break;
                    case 'w':
                    case 'W':
                        // Increase throttle (load) while pressed, will decay when released
                        wKeyPressed = true;
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;  // Update baseline to current value
                        break;
                    case ' ':
                        // Brake
                        interactiveLoad = 0.0;
                        baselineLoad = 0.0;  // Update baseline
                        break;
                    case 'r':
                    case 'R':
                        // Reset to idle - DISABLE RPM control to avoid pulsation
                        interactiveTargetRPM = 0.0;  // Disable RPM control mode
                        interactiveLoad = 0.2;
                        baselineLoad = 0.2;
                        // Don't set RPM controller target - stay in load control mode
                        break;
                    case 'a':
                        // Toggle ignition module - only on initial press, not repeat
                        // Note: 'A' (65) conflicts with UP arrow on macOS, so we handle it separately
                        {
                            static bool ignitionState = true;  // Start enabled by default
                            ignitionState = !ignitionState;
                            EngineSimSetIgnition(handle, ignitionState ? 1 : 0);
                            std::cout << "Ignition " << (ignitionState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 's':
                    case 'S':
                        // Toggle starter motor - only on initial press, not repeat
                        {
                            static bool starterState = false;
                            starterState = !starterState;
                            EngineSimSetStarterMotor(handle, starterState ? 1 : 0);
                            std::cout << "Starter motor " << (starterState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 65:  // UP arrow (macOS) - also 'A' which we want to avoid
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;  // Update baseline
                        break;
                    case 66:  // DOWN arrow (macOS)
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        baselineLoad = interactiveLoad;  // Update baseline
                        break;
                    case 'k':  // Alternative UP key
                    case 'K':
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;  // Update baseline
                        break;
                    case 'j':  // Alternative DOWN key
                    case 'J':
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        baselineLoad = interactiveLoad;  // Update baseline
                        break;
                }
                lastKey = key;
            }

            // Apply W key decay when not pressed
            if (!wKeyPressed && interactiveLoad > baselineLoad) {
                // Gradually decay back to baseline (0.05 per frame at 60fps = 3% per second)
                interactiveLoad = std::max(baselineLoad, interactiveLoad - 0.001);
            }

            // In interactive mode, use RPM control if target is set
            if (interactiveTargetRPM > 0) {
                throttle = rpmController.update(stats.currentRPM, updateInterval);
            } else {
                throttle = interactiveLoad;
            }
        }
        else if (args.targetRPM > 0 && args.targetLoad < 0) {
            // RPM control mode
            throttle = rpmController.update(stats.currentRPM, updateInterval);
            if (framesRendered % 600 == 0) {  // Print every 10 seconds at 60fps for more frequent updates
                std::cout << "RPM Control: " << stats.currentRPM << "/" << args.targetRPM
                          << ", Throttle: " << std::fixed << std::setprecision(3) << smoothedThrottle
                          << ", Load: " << static_cast<int>(stats.currentLoad * 100) << "%\n";
            }
        }
        else if (args.targetLoad >= 0) {
            // Direct load mode
            throttle = args.targetLoad;
        }
        else {
            // Auto throttle ramp
            if (currentTime < 0.5) {
                throttle = currentTime / 0.5;
            } else {
                throttle = 1.0;
            }
        }

        // Update physics
        auto simStart = std::chrono::steady_clock::now();

        // CRITICAL: Apply throttle smoothing like GUI (engine_sim_application.cpp line 798)
        // This prevents square-toothed aggressive noise changes
        // Formula: smoothed = target * 0.5 + smoothed * 0.5
        smoothedThrottle = throttle * 0.5 + smoothedThrottle * 0.5;

        EngineSimSetThrottle(handle, smoothedThrottle);
        EngineSimUpdate(handle, updateInterval);
        auto simEnd = std::chrono::steady_clock::now();

        // Render audio
        int framesToRender = framesPerUpdate;

        // In non-interactive mode, check buffer limits
        if (!args.interactive) {
            int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
            framesToRender = std::min(framesPerUpdate, totalExpectedFrames - framesProcessed);
        }

        if (framesToRender > 0) {
            int samplesWritten = 0;

            // CRITICAL FIX: Only read audio in main loop if NOT using AudioUnit streaming
            // When args.playAudio is true, the AudioUnit callback (line 322-327) handles
            // all audio reading. Reading here causes double-consumption and buffer underruns.
            if (!args.playAudio) {
                // CRITICAL: Always write sequentially to the buffer (like sine wave test)
                // This prevents overwriting previously rendered audio
                float* writePtr = audioBuffer.data() + framesRendered * channels;

                auto renderStart = std::chrono::steady_clock::now();
                // CRITICAL: Use EngineSimReadAudioBuffer when audio thread is running
                // This matches GUI behavior (engine_sim_application.cpp line 274)
                // The audio thread continuously fills the buffer, we just read from it
                result = EngineSimReadAudioBuffer(handle, writePtr, framesToRender, &samplesWritten);
                auto renderEnd = std::chrono::steady_clock::now();

                if (result == ESIM_SUCCESS && samplesWritten > 0) {
                    // Update counters
                    framesRendered += samplesWritten;
                    framesProcessed += samplesWritten;
                }
            } else {
                // AudioUnit streaming mode: callback handles all audio rendering
                // Just track progress for WAV export if needed
                framesProcessed += framesToRender;
                framesRendered += framesToRender;
            }
        }

        currentTime += updateInterval;

        // Display progress
        if (args.interactive) {
            displayHUD(stats.currentRPM, smoothedThrottle, interactiveTargetRPM, stats);
        } else {
            int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
            int progress = static_cast<int>(framesProcessed * 100 / totalExpectedFrames);
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% (" << framesProcessed << " frames)\r" << std::flush;
                lastProgress = progress;
            }
        }
    }

    std::cout << "\n\nSimulation complete!\n";

    // Cleanup
    if (keyboardInput) {
        delete keyboardInput;
    }

    if (audioPlayer) {
        audioPlayer->stop();
        audioPlayer->cleanup();
        delete audioPlayer;
    }

    // Get final statistics
    EngineSimStats finalStats = {};
    EngineSimGetStats(handle, &finalStats);
    std::cout << "\nFinal Statistics:\n";
    std::cout << "  RPM: " << static_cast<int>(finalStats.currentRPM) << "\n";
    std::cout << "  Load: " << static_cast<int>(finalStats.currentLoad * 100) << "%\n";
    std::cout << "  Exhaust Flow: " << finalStats.exhaustFlow << " m^3/s\n";
    std::cout << "  Manifold Pressure: " << static_cast<int>(finalStats.manifoldPressure) << " Pa\n";

    EngineSimDestroy(handle);

    // Write WAV file
    if (args.outputWav) {
        std::cout << "\nWriting WAV file: " << args.outputWav << "\n";
        std::ofstream wavFile(args.outputWav, std::ios::binary);
        if (!wavFile) {
            std::cerr << "ERROR: Failed to create output file\n";
            return 1;
        }

        WaveHeader header = {};
        header.dataSize = framesRendered * channels * sizeof(float);
        header.fileSize = 36 + header.dataSize;

        wavFile.write(reinterpret_cast<const char*>(&header), sizeof(WaveHeader));
        wavFile.write(reinterpret_cast<const char*>(audioBuffer.data()), framesRendered * channels * sizeof(float));
        wavFile.close();

        std::cout << "Done! Wrote " << framesRendered * channels << " samples ("
                  << framesRendered << " frames, " << args.duration << " seconds)\n";
    }

    return 0;
}

// ============================================================================
// Main Entry Point
// ============================================================================

int main(int argc, char* argv[]) {
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "========================\n\n";

    // Setup signal handler
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    // Parse command line arguments
    CommandLineArgs args;
    if (!parseArguments(argc, argv, args)) {
        return 1;
    }

    std::cout << "Configuration:\n";
    if (args.sineMode) {
        std::cout << "  Mode: Sine Wave Test (440Hz)\n";
        std::cout << "  Engine: (bypassed - sine wave mode)\n";
    } else {
        std::cout << "  Engine: " << args.engineConfig << "\n";
    }
    std::cout << "  Output: " << (args.outputWav ? args.outputWav : "(none - audio not saved)") << "\n";
    if (args.interactive) {
        std::cout << "  Duration: (interactive - runs until quit)\n";
    } else {
        std::cout << "  Duration: " << args.duration << " seconds\n";
    }
    if (args.targetRPM > 0) {
        std::cout << "  Target RPM: " << args.targetRPM << "\n";
    }
    if (args.targetLoad >= 0) {
        std::cout << "  Target Load: " << static_cast<int>(args.targetLoad * 100) << "%\n";
    }
    std::cout << "  Interactive: " << (args.interactive ? "Yes" : "No") << "\n";
    std::cout << "  Audio Playback: " << (args.playAudio ? "Yes" : "No") << "\n";
    std::cout << "\n";

    // Run simulation
    return runSimulation(args);
}
