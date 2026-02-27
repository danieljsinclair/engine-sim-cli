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
#include "engine_sim_loader.h"
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
    const EngineSimAPI* engineAPI;       // Engine simulator API for pull-based reads
    std::atomic<bool> isPlaying;          // Playback state
    std::atomic<int> underrunCount;       // Count of buffer underruns
    int sampleRate;                       // Sample rate for calculations

    AudioUnitContext() : engineHandle(nullptr), engineAPI(nullptr), isPlaying(false),
                        underrunCount(0), sampleRate(44100) {}
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
        context->sampleRate = sr;

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

        // Request small hardware buffer for low latency
        // Default is ~512 frames (~11.6ms). Request 64 frames (~1.5ms).
        UInt32 requestedFrames = 64;
        status = AudioUnitSetProperty(
            audioUnit,
            kAudioUnitProperty_MaximumFramesPerSlice,
            kAudioUnitScope_Global,
            0,
            &requestedFrames,
            sizeof(requestedFrames)
        );
        if (status != noErr) {
            std::cerr << "WARNING: Could not set buffer size: " << status << "\n";
        }

        // Initialize AudioUnit
        status = AudioUnitInitialize(audioUnit);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to initialize AudioUnit: " << status << "\n";
            cleanup();
            return false;
        }

        // Also set the hardware device buffer size for truly low latency
        {
            AudioObjectPropertyAddress bufferSizeAddr = {
                kAudioDevicePropertyBufferFrameSize,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            // Get default device first
            AudioDeviceID defaultDevice = 0;
            AudioObjectPropertyAddress defaultDeviceAddr = {
                kAudioHardwarePropertyDefaultOutputDevice,
                kAudioObjectPropertyScopeGlobal,
                kAudioObjectPropertyElementMain
            };
            UInt32 devSize = sizeof(defaultDevice);
            AudioObjectGetPropertyData(kAudioObjectSystemObject, &defaultDeviceAddr, 0, nullptr, &devSize, &defaultDevice);

            if (defaultDevice != 0) {
                UInt32 hwBufferSize = 64;
                status = AudioObjectSetPropertyData(defaultDevice, &bufferSizeAddr, 0, nullptr, sizeof(hwBufferSize), &hwBufferSize);
                if (status == noErr) {
                    std::cout << "[Audio] Hardware buffer size set to " << hwBufferSize << " frames\n";
                } else {
                    // Try 128 as fallback
                    hwBufferSize = 128;
                    status = AudioObjectSetPropertyData(defaultDevice, &bufferSizeAddr, 0, nullptr, sizeof(hwBufferSize), &hwBufferSize);
                    if (status == noErr) {
                        std::cout << "[Audio] Hardware buffer size set to " << hwBufferSize << " frames (fallback)\n";
                    } else {
                        std::cerr << "WARNING: Could not set hardware buffer size: " << status << "\n";
                    }
                }

                // Read back actual buffer size
                UInt32 actualSize = 0;
                UInt32 propSize = sizeof(actualSize);
                AudioObjectGetPropertyData(defaultDevice, &bufferSizeAddr, 0, nullptr, &propSize, &actualSize);
                std::cout << "[Audio] Actual hardware buffer size: " << actualSize << " frames ("
                          << std::fixed << std::setprecision(1) << (actualSize * 1000.0 / sr) << "ms)\n";
            }
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
        std::cout << "[Audio] PULL-BASED MODE: Callback reads directly from synthesizer buffer (async thread with no cap)\n";
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

    // Set the engine simulator API for pull-based reads
    void setEngineAPI(const EngineSimAPI* api) {
        if (context) {
            context->engineAPI = api;
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

    // Expose context for main loop access
    AudioUnitContext* getContext() { return context; }

    // Get underrun count for diagnostics
    int getUnderrunCount() {
        if (!context) return 0;
        return context->underrunCount.load();
    }

    // Reset underrun count
    void resetUnderrunCount() {
        if (context) {
            context->underrunCount.store(0);
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
    // PULL-BASED: Reads directly from synthesizer via g_engineAPI.ReadAudioBuffer()
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

        // Diagnostic: log if CoreAudio provides unexpected buffer count
        static bool loggedBufferCount = false;
        if (!loggedBufferCount) {
            std::cout << "[AudioCallback] mNumberBuffers=" << ioData->mNumberBuffers
                      << " numberFrames=" << numberFrames << "\n";
            loggedBufferCount = true;
        }

        // Pull-based: Read directly from synthesizer buffer
        if (ctx->engineHandle) {
            for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
                AudioBuffer& buffer = ioData->mBuffers[i];
                float* data = static_cast<float*>(buffer.mData);

                // Calculate how many frames we can write
                UInt32 framesToWrite = numberFrames;
                if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
                    framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
                }

                // Read directly from synthesizer
                int32_t framesRead = 0;
                if (ctx->engineAPI) {
                    ctx->engineAPI->ReadAudioBuffer(ctx->engineHandle, data, static_cast<int>(framesToWrite), &framesRead);
                }

                // Check for underrun
                if (framesRead < static_cast<int>(framesToWrite)) {
                    ctx->underrunCount.fetch_add(1);
                    // Log underrun periodically (every 10th underrun to avoid spam)
                    if (ctx->underrunCount.load() % 10 == 0) {
                        std::cout << "[Audio Diagnostics] Buffer underrun #" << ctx->underrunCount.load()
                                  << " - requested: " << framesToWrite << ", available: " << framesRead << "\n";
                    }

                    // Fill rest with silence if underrun
                    if (framesRead > 0) {
                        int silenceFrames = framesToWrite - framesRead;
                        std::memset(data + framesRead * 2, 0, silenceFrames * 2 * sizeof(float));
                    }
                }
            }
        } else {
            // No engine handle - output silence
            for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
                AudioBuffer& buffer = ioData->mBuffers[i];
                float* data = static_cast<float*>(buffer.mData);
                std::memset(data, 0, buffer.mDataByteSize);
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
// RPM Controller (Simple, Responsive)
// ============================================================================

class RPMController {
private:
    // Simple P-ONLY controller for responsive throttle without over-constraint
    static constexpr double KP = 0.3;    // Moderate P-gain for responsive but stable control
    static constexpr double KI = 0.0;    // No integral term to prevent overshoot
    static constexpr double KD = 0.0;    // No derivative term to prevent overshoot
    static constexpr double MIN_THROTTLE = 0.05;  // Minimum 5% throttle to prevent stalling
    static constexpr double MAX_THROTTLE = 1.0;  // Standard maximum throttle
    static constexpr double MIN_RPM_FOR_CONTROL = 300.0;  // Minimum RPM to enable control

    double targetRPM;
    double kp;
    double ki;
    double kd;
    double integral;
    double lastError;
    bool firstUpdate;

public:
    RPMController() : targetRPM(0),
                     kp(KP),
                     ki(KI),
                     kd(KD),
                     integral(0),
                     lastError(0),
                     firstUpdate(true) {}

    void setTargetRPM(double rpm) {
        targetRPM = rpm;
        integral = 0;  // Reset integral when target changes
        firstUpdate = true;
    }

    double update(double currentRPM, double dt) {
        if (targetRPM <= 0) return 0.0;

        // Only enable RPM control above minimum RPM to prevent hunting at idle
        if (currentRPM < MIN_RPM_FOR_CONTROL) {
            // Use minimum throttle to keep engine running but don't control RPM
            return MIN_THROTTLE;
        }

        double error = targetRPM - currentRPM;

        // Simple P-term calculation - responsive but stable
        double pTerm = error * kp;

        // No additional smoothing or rate limiting - let the main loop smoothing handle this
        double throttle = pTerm;

        // Conditional minimum throttle: only apply minimum when accelerating
        // When error > 0 (need to accelerate): use MIN_THROTTLE to prevent stalling
        // When error <= 0 (need to decelerate): allow 0.0 throttle to let engine slow down
        double minThrottle = (error > 0) ? MIN_THROTTLE : 0.0;

        // Clamp throttle with conditional minimum
        return std::max(minThrottle, std::min(MAX_THROTTLE, throttle));
    }

    // Get debug information for diagnostics
    void getDebugInfo(double& pTerm, double& iTerm, double& dTerm) const {
        // Note: pTerm would need to be calculated from current error
        pTerm = 0.0;
        iTerm = integral * ki;
        dTerm = 0.0;
    }
};

// ============================================================================
// Global State
// ============================================================================

static std::atomic<bool> g_running(true);
static std::atomic<bool> g_interactiveMode(false);
static EngineSimAPI g_engineAPI = {};

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

void displayHUD(double rpm, double throttle, double targetRPM, const EngineSimStats& stats, int underrunCount) {
    std::cout << "\r";
    std::cout << "[" << std::fixed << std::setprecision(0) << std::setw(4) << rpm << " RPM] ";
    std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(throttle * 100) << "%] ";
    if (targetRPM > 0) {
        std::cout << "[Target: " << std::setw(4) << static_cast<int>(targetRPM) << " RPM] ";
    }
    std::cout << "[Flow: " << std::setprecision(2) << stats.exhaustFlow << " m3/s] ";
    std::cout << "[Underruns: " << underrunCount << "] ";
    std::cout << std::flush;
}

// ============================================================================
// Shared Audio Loop Infrastructure
// ============================================================================

// Shared configuration constants
struct AudioLoopConfig {
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr int MAX_READ_FRAMES = 4096;  // Max frames to read per iteration
};

// Warmup: prime the synthesizer so it has audio data ready before playback starts
void runWarmup(EngineSimHandle handle, const EngineSimAPI& api, AudioPlayer* audioPlayer, bool playAudio) {
    static constexpr int WARMUP_ITERATIONS = 3;
    std::cout << "Priming synthesizer pipeline (" << WARMUP_ITERATIONS << " iterations)...\n";

    for (int i = 0; i < WARMUP_ITERATIONS; i++) {
        EngineSimStats stats = {};
        api.GetStats(handle, &stats);

        api.SetThrottle(handle, 0.6);
        api.Update(handle, 1.0 / 60.0);

        // Use RenderAudioSync if available, otherwise just Update (pull-based mode will handle rendering)
        if (api.RenderAudioSync) {
            api.RenderAudioSync(handle);
        }

        std::cout << "  Priming: " << stats.currentRPM << " RPM\n";

        // Drain warmup audio to prevent crackles from starter motor transients
        if (playAudio) {
            std::vector<float> discardBuffer(AudioLoopConfig::MAX_READ_FRAMES * 2);
            int discarded = 0;
            api.ReadAudioBuffer(handle, discardBuffer.data(), AudioLoopConfig::MAX_READ_FRAMES, &discarded);
        }
    }
}

// Audio source abstraction - the ONLY difference between modes
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual int generateAudio(std::vector<float>& buffer, int frames) = 0;
    virtual void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount) = 0;
};

// Sine wave audio source
class SineAudioSource : public IAudioSource {
private:
    EngineSimHandle handle;
    const EngineSimAPI& api;
    double currentPhase;
    double directRPM;  // Direct RPM control - no physics smoothing

public:
    SineAudioSource(EngineSimHandle h, const EngineSimAPI& a)
        : handle(h), api(a), currentPhase(0.0), directRPM(0.0) {}

    void setDirectRPM(double rpm) { directRPM = rpm; }
    double getDirectRPM() const { return directRPM; }

    int generateAudio(std::vector<float>& buffer, int frames) override {
        // Audio diagnostics - track what's being generated vs consumed
        double frequency = (directRPM / 600.0) * 100.0;

        // Log samples generated for debugging
        static int debugCounter = 0;
        if (++debugCounter % 50 == 0) {
            std::cout << "[Sine DEBUG] Generated " << frames << " samples at " << frequency << " Hz, phase=" << currentPhase << "\n";
        }

        double phaseIncrement = (2.0 * M_PI * frequency) / AudioLoopConfig::SAMPLE_RATE;
        for (int i = 0; i < frames; i++) {
            float sample = static_cast<float>(std::sin(currentPhase) * 0.9);
            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
            currentPhase += phaseIncrement;
        }
        // Keep phase in [0, 2*PI) to prevent floating-point precision loss
        currentPhase = std::fmod(currentPhase, 2.0 * M_PI);

        return frames;
    }

    void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount) override {
        double frequency = (directRPM / 600.0) * 100.0;
        if (interactive) {
            std::cout << "\r[" << std::fixed << std::setprecision(0) << std::setw(4) << directRPM << " RPM] ";
            std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(throttle * 100) << "%] ";
            std::cout << "[Frequency: " << std::setw(4) << static_cast<int>(frequency) << " Hz] ";
            std::cout << "[Underruns: " << underrunCount << "] ";
            std::cout << std::flush;
        } else {
            static int lastProgress = 0;
            int progress = static_cast<int>(currentTime * 100 / duration);
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% | RPM: " << static_cast<int>(directRPM)
                          << " | Frequency: " << static_cast<int>(frequency) << " Hz"
                          << " | Underruns: " << underrunCount << "\r" << std::flush;
                lastProgress = progress;
            }
        }
    }
};

// Engine audio source
class EngineAudioSource : public IAudioSource {
private:
    EngineSimHandle handle;
    const EngineSimAPI& api;

public:
    EngineAudioSource(EngineSimHandle h, const EngineSimAPI& a)
        : handle(h), api(a) {}

    int generateAudio(std::vector<float>& buffer, int frames) override {
        int totalRead = 0;
        api.ReadAudioBuffer(handle, buffer.data(), frames, &totalRead);
        return totalRead;
    }

    void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle, int underrunCount) override {
        if (interactive) {
            std::cout << "\r[" << std::fixed << std::setprecision(0) << std::setw(4) << stats.currentRPM << " RPM] ";
            std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(throttle * 100) << "%] ";
            std::cout << "[Flow: " << std::setprecision(2) << stats.exhaustFlow << " m3/s] ";
            std::cout << "[Underruns: " << underrunCount << "] ";
            std::cout << std::flush;
        } else {
            static int lastProgress = 0;
            int progress = static_cast<int>(currentTime * 100 / duration);
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% | RPM: " << static_cast<int>(stats.currentRPM)
                          << " | Throttle: " << static_cast<int>(throttle * 100) << "%"
                          << " | Underruns: " << underrunCount << "\r" << std::flush;
                lastProgress = progress;
            }
        }
    }
};

// ============================================================================
// UNIFIED Main Loop - Works for BOTH sine and engine modes
// ============================================================================

int runUnifiedAudioLoop(
    EngineSimHandle handle,
    const EngineSimAPI& api,
    IAudioSource& audioSource,
    const CommandLineArgs& args,
    AudioPlayer* audioPlayer,
    SineAudioSource* sineSource = nullptr)
{
    auto loopStartTime = std::chrono::steady_clock::now();
    auto lastTime = std::chrono::steady_clock::now();

    // Setup keyboard input if interactive
    KeyboardInput* keyboardInput = nullptr;
    double interactiveLoad = 0.7;
    double baselineLoad = interactiveLoad;
    bool wKeyPressed = false;

    if (args.interactive) {
        keyboardInput = new KeyboardInput();
        std::cout << "\nInteractive mode enabled. Press Q to quit.\n";
    }

    // Enable starter motor
    g_engineAPI.SetStarterMotor(handle, 1);

    // Reusable audio buffer - generous size for variable frame reads
    std::vector<float> audioBuffer(AudioLoopConfig::MAX_READ_FRAMES * 2);

    // Main loop - Pull-based mode, callback reads directly from synthesizer
    std::cout << "\nStarting main loop (pull-based mode)..." << std::endl;

    while (g_running.load()) {
        // Check duration for non-interactive mode using wall-clock time
        double currentTime = std::chrono::duration<double>(
            std::chrono::steady_clock::now() - loopStartTime).count();
        if (!args.interactive && currentTime >= args.duration) break;

        // Measure real elapsed wall-clock time
        auto now = std::chrono::steady_clock::now();
        double elapsed = std::chrono::duration<double>(now - lastTime).count();
        lastTime = now;

        // Clamp dt to prevent spiral on first frame or after debugger pause
        if (elapsed > 0.1) elapsed = 0.1;
        if (elapsed < 0.0001) elapsed = 0.0001;

        // Get current stats for starter motor check only
        EngineSimStats starterStats = {};
        api.GetStats(handle, &starterStats);

        // Disable starter once running
        const double minSustainedRPM = 550.0;
        if (starterStats.currentRPM > minSustainedRPM) {
            api.SetStarterMotor(handle, 0);
        }

        // Handle keyboard input
        if (args.interactive && keyboardInput) {
            static int lastKey = -1;
            int key = keyboardInput->getKey();

            if (key < 0) {
                lastKey = -1;
                wKeyPressed = false;
            } else if (key != lastKey) {
                switch (key) {
                    case 27: case 'q': case 'Q':
                        g_running.store(false);
                        break;
                    case 'w': case 'W':
                        wKeyPressed = true;
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case ' ':
                        interactiveLoad = 0.0;
                        baselineLoad = 0.0;
                        break;
                    case 'r': case 'R':
                        interactiveLoad = 0.2;
                        baselineLoad = interactiveLoad;
                        break;
                    case 'a':
                        {
                            static bool ignitionState = true;
                            ignitionState = !ignitionState;
                            api.SetIgnition(handle, ignitionState ? 1 : 0);
                            std::cout << "Ignition " << (ignitionState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 's':
                        {
                            static bool starterState = false;
                            starterState = !starterState;
                            api.SetStarterMotor(handle, starterState ? 1 : 0);
                            std::cout << "Starter motor " << (starterState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 65:  // UP arrow
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case 66:  // DOWN arrow
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case 'k': case 'K':
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case 'j': case 'J':
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                }
                lastKey = key;
            }

            if (!wKeyPressed && interactiveLoad > baselineLoad) {
                interactiveLoad = std::max(baselineLoad, interactiveLoad * 0.5);
            }
        }

        // Calculate throttle
        double throttle = args.interactive ? interactiveLoad :
                         (currentTime < 0.5 ? currentTime / 0.5 : 1.0);

        // For sine mode: map throttle directly to RPM (instant, no physics)
        if (sineSource) {
            sineSource->setDirectRPM(throttle * 6000.0);
        }

        // Advance simulation by real elapsed time
        api.SetThrottle(handle, throttle);
        api.Update(handle, elapsed);

        // Render ALL input that was just produced into audio buffer
        // Use RenderAudioSync if available
        if (api.RenderAudioSync) {
            api.RenderAudioSync(handle);
        }

        // Pull-based mode: Callback reads directly from synthesizer
        // No need to read and copy to circular buffer here
        int32_t framesRead = 0;

        // Get stats for display
        EngineSimStats stats = {};
        api.GetStats(handle, &stats);

        // Get underrun count for display
        int underrunCount = audioPlayer ? audioPlayer->getUnderrunCount() : 0;

        // Early iteration diagnostics (first 50 iterations)
        static int iterCount = 0;
        iterCount++;
        if (iterCount <= 50) {
            std::cout << "[iter " << iterCount << "] dt=" << std::fixed << std::setprecision(3) << (elapsed*1000)
                      << "ms underruns=" << underrunCount << std::endl;
        }

        // Periodic diagnostics (~every 1s)
        static auto lastDiagTime = std::chrono::steady_clock::now();
        auto diagNow = std::chrono::steady_clock::now();
        if (std::chrono::duration<double>(diagNow - lastDiagTime).count() > 1.0) {
            lastDiagTime = diagNow;
            std::cout << "\n[Diag] dt=" << std::fixed << std::setprecision(1) << (elapsed*1000)
                      << "ms underruns=" << underrunCount << "\n";
        }

        // Display progress
        audioSource.displayProgress(currentTime, args.duration, args.interactive, stats, throttle, underrunCount);

        // Yield CPU briefly - NOT sleep for timing, just prevent 100% CPU spin
        std::this_thread::yield();
    }

    if (keyboardInput) {
        delete keyboardInput;
    }

    return 0;
}

// ============================================================================
// Main Simulation Entry Point - UNIFIED for both modes
// ============================================================================

int runSimulation(const CommandLineArgs& args) {
    const int sampleRate = AudioLoopConfig::SAMPLE_RATE;
    const int channels = AudioLoopConfig::CHANNELS;

    // ============================================================================
    // COMMON SETUP - Same for both sine and engine modes
    // ============================================================================

    // Create simulator
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 22050;  // ~500ms latency (down from 96000 = 2.2s)
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 1.0f;
    config.airNoise = 1.0f;

    EngineSimHandle handle = nullptr;
    EngineSimResult result = g_engineAPI.Create(&config, &handle);
    if (result != ESIM_SUCCESS || !handle) {
        std::cerr << "ERROR: Failed to create simulator\n";
        return 1;
    }

    // Load engine configuration
    std::string configPath;
    std::string assetBasePath = "engine-sim-bridge/engine-sim";  // Base directory, not full path

    if (args.sineMode) {
        configPath = "engine-sim-bridge/engine-sim/assets/main.mr";
    } else if (args.useDefaultEngine) {
        configPath = "engine-sim-bridge/engine-sim/assets/main.mr";
    } else if (args.engineConfig) {
        configPath = args.engineConfig;
    } else {
        std::cerr << "ERROR: No engine configuration specified\n";
        std::cerr << "Use --script <config.mr> or --default-engine\n";
        g_engineAPI.Destroy(handle);
        return 1;
    }

    // Resolve to absolute paths
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
                assetBasePath = parentPath.parent_path().string();  // Parent of assets is the base
            } else {
                assetBasePath = parentPath.string();
            }
        }

        // Resolve asset base path
        std::filesystem::path assetPath(assetBasePath);
        if (assetPath.is_relative()) {
            assetPath = std::filesystem::absolute(assetPath);
        }
        assetPath = assetPath.lexically_normal();
        assetBasePath = assetPath.string();
    } catch (const std::filesystem::filesystem_error& e) {
        std::cerr << "ERROR: Failed to resolve path: " << e.what() << "\n";
        g_engineAPI.Destroy(handle);
        return 1;
    }

    result = g_engineAPI.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load config: " << g_engineAPI.GetLastError(handle) << "\n";
        g_engineAPI.Destroy(handle);
        return 1;
    }
    std::cout << "[Configuration loaded: " << configPath << "]" << std::endl;

    // No async audio thread - using synchronous rendering via RenderAudioSync().
    // Async thread has 2000-sample cap and WaitProcessed deadlocks.
    // Sync render uses full 96000-sample buffer, no race conditions.
    std::cout << "[Using synchronous audio rendering]" << std::endl;

    // Enable ignition
    g_engineAPI.SetIgnition(handle, 1);
    std::cout << "[Ignition enabled]\n";

    // Initialize audio player
    AudioPlayer* audioPlayer = nullptr;
    if (args.playAudio) {
        audioPlayer = new AudioPlayer();
        if (!audioPlayer->initialize(sampleRate)) {
            std::cerr << "ERROR: Audio init failed\n";
            delete audioPlayer;
            g_engineAPI.Destroy(handle);
            return 1;
        }

        // Set engine handle and API for pull-based callback
        audioPlayer->setEngineHandle(handle);
        audioPlayer->setEngineAPI(&g_engineAPI);

        // Start async render thread - fills m_audioBuffer continuously
        // Callback reads directly from m_audioBuffer (pull-based, no CLI circular buffer)
        // Async thread has NO CAP (removed 2000-sample limit) to prevent underruns
        g_engineAPI.StartAudioThread(handle);
        std::cout << "[Async render thread started (no buffer cap)]\n";

        audioPlayer->start();
        std::cout << "[Audio playback enabled]\n";
    }

    // Warmup (common for both modes)
    runWarmup(handle, g_engineAPI, audioPlayer, args.playAudio);

    // Reset underrun count after warmup
    if (audioPlayer) {
        audioPlayer->resetUnderrunCount();
    }

    // Create appropriate audio source - THE ONLY DIFFERENCE
    std::unique_ptr<IAudioSource> audioSource;
    SineAudioSource* sineSource = nullptr;
    if (args.sineMode) {
        std::cout << "Mode: SINE TEST\n";
        auto sine = std::make_unique<SineAudioSource>(handle, g_engineAPI);
        sineSource = sine.get();
        audioSource = std::move(sine);
    } else {
        std::cout << "Mode: REAL ENGINE\n";
        audioSource = std::make_unique<EngineAudioSource>(handle, g_engineAPI);
    }

    // Run unified loop - SAME CODE FOR BOTH MODES
    int exitCode = runUnifiedAudioLoop(handle, g_engineAPI, *audioSource, args, audioPlayer, sineSource);

    // Cleanup (common for both modes)
    if (audioPlayer) {
        audioPlayer->stop();
        audioPlayer->waitForCompletion();
        delete audioPlayer;
    }

    g_engineAPI.Destroy(handle);

    // Note: WAV export not supported in unified mode
    // The unified loop uses ReadAudioBuffer which doesn't support WAV export
    // For WAV export, need to use the old engine mode code path
    if (args.outputWav) {
        std::cout << "\nWARNING: WAV export not supported in unified mode\n";
        std::cout << "Use the old engine mode code path for WAV export.\n";
    }

    return exitCode;
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

    // Load engine-sim library dynamically based on mode
    if (!LoadEngineSimLibrary(g_engineAPI, args.sineMode)) {
        std::cerr << "ERROR: Failed to load engine-sim library\n";
        return 1;
    }

    std::cout << "Configuration:\n";
    if (args.sineMode) {
        std::cout << "  Mode: RPM-Linked Sine Wave Test\n";
        std::cout << "  Mapping: 600 RPM = 100Hz, 6000 RPM = 1000Hz\n";
        std::cout << "  Engine: Default (Subaru EJ25)\n";
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
    int result = runSimulation(args);

    // Cleanup: unload library
    UnloadEngineSimLibrary(g_engineAPI);

    return result;
}
