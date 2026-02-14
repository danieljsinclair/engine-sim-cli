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
    std::atomic<bool> isPlaying;          // Playback state

    // SIMPLE: Basic circular buffer (96000 samples)
    // Both sine mode and engine mode use the same circular buffer for consistency
    float* circularBuffer;                // Intermediate buffer
    size_t circularBufferSize;            // Buffer capacity (96000)
    std::atomic<int> writePointer;        // Write position in circular buffer
    std::atomic<int> readPointer;         // Read position in circular buffer
    std::atomic<int> underrunCount;       // Count of buffer underruns
    int bufferStatus;                     // Buffer status for diagnostics (0=normal, 1=warning, 2=critical)

    AudioUnitContext() : engineHandle(nullptr), isPlaying(false),
                        circularBuffer(nullptr), circularBufferSize(96000),
                        writePointer(0), readPointer(0),
                        underrunCount(0), bufferStatus(0) {}
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

        // SIMPLE: Allocate circular buffer
        context->circularBufferSize = 96000;  // 2+ seconds at 44.1kHz
        context->circularBuffer = new float[context->circularBufferSize * 2];  // Stereo
        std::memset(context->circularBuffer, 0, context->circularBufferSize * 2 * sizeof(float));

        // Initialize pointers - both start at beginning for proper circular buffer
        context->writePointer.store(0);
        context->readPointer.store(0);

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
        std::cout << "[Audio] Real-time streaming mode (simplified pull model)\n";
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
            // SIMPLE: Free circular buffer
            if (context->circularBuffer) {
                delete[] context->circularBuffer;
                context->circularBuffer = nullptr;
            }
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

    // Add samples to circular buffer with true continuous writes
    void addToCircularBuffer(const float* samples, int frameCount) {
        if (!context || !context->circularBuffer) return;

        int writePtr = context->writePointer.load();
        const int bufferSize = static_cast<int>(context->circularBufferSize);

        // Handle wrap-around for large writes that might span the buffer boundary
        if (writePtr + frameCount <= bufferSize) {
            // Simple case: no wrap-around needed
            for (int i = 0; i < frameCount; i++) {
                context->circularBuffer[(writePtr + i) * 2] = samples[i * 2];
                context->circularBuffer[(writePtr + i) * 2 + 1] = samples[i * 2 + 1];
            }
        } else {
            // Complex case: write spans buffer boundary, split into two segments
            int firstSegment = bufferSize - writePtr;

            // Write first segment (from current position to end of buffer)
            for (int i = 0; i < firstSegment; i++) {
                context->circularBuffer[(writePtr + i) * 2] = samples[i * 2];
                context->circularBuffer[(writePtr + i) * 2 + 1] = samples[i * 2 + 1];
            }

            // Write second segment (from beginning of buffer for remaining samples)
            int secondSegment = frameCount - firstSegment;
            for (int i = 0; i < secondSegment; i++) {
                context->circularBuffer[i * 2] = samples[(firstSegment + i) * 2];
                context->circularBuffer[i * 2 + 1] = samples[(firstSegment + i) * 2 + 1];
            }
        }

        // Commit the write with proper wrap-around
        int newWritePtr = (writePtr + frameCount) % bufferSize;
        context->writePointer.store(newWritePtr);
    }

    // Expose context for main loop access
    AudioUnitContext* getContext() { return context; }

    // Get buffer diagnostics for monitoring synchronization issues
    void getBufferDiagnostics(int& writePtr, int& readPtr, int& available, int& status) {
        if (context) {
            writePtr = context->writePointer.load();
            readPtr = context->readPointer.load();

            // Calculate available frames
            int bufferSize = static_cast<int>(context->circularBufferSize);
            if (writePtr >= readPtr) {
                available = writePtr - readPtr;
            } else {
                available = (bufferSize - readPtr) + writePtr;
            }

            // Set status based on available frames
            if (available > bufferSize / 4) {
                status = 0;  // Normal
            } else if (available > bufferSize / 8) {
                status = 1;  // Warning
            } else if (available > 0) {
                status = 2;  // Critical
            } else {
                status = 3;  // Underrun
                context->underrunCount.fetch_add(1);
            }
        } else {
            writePtr = 0;
            readPtr = 0;
            available = 0;
            status = 3;
        }
    }

    // Reset buffer diagnostics
    void resetBufferDiagnostics() {
        if (context) {
            context->underrunCount.store(0);
            context->bufferStatus = 0;
        }
    }

    // Reset circular buffer to eliminate warmup audio latency
    void resetCircularBuffer() {
        if (context && context->circularBuffer) {
            // Reset read/write pointers to discard all buffered audio
            context->writePointer.store(0);
            context->readPointer.store(0);
            // Clear buffer contents
            std::memset(context->circularBuffer, 0, context->circularBufferSize * 2 * sizeof(float));
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

        // CRITICAL: Read from intermediate circular buffer
        // This buffer is filled by main loop calling addToCircularBuffer()
        // Both sine mode and engine mode use the same circular buffer for consistency
        if (!ctx->circularBuffer) {
            // No buffer - output silence
            for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
                AudioBuffer& buffer = ioData->mBuffers[i];
                float* data = static_cast<float*>(buffer.mData);
                std::memset(data, 0, buffer.mDataByteSize);
            }
            return noErr;
        }

        const int bufferSize = static_cast<int>(ctx->circularBufferSize);

        for (UInt32 i = 0; i < ioData->mNumberBuffers; i++) {
            AudioBuffer& buffer = ioData->mBuffers[i];
            float* data = static_cast<float*>(buffer.mData);

            // Calculate how many frames we can write
            UInt32 framesToWrite = numberFrames;
            if (framesToWrite * 2 * sizeof(float) > buffer.mDataByteSize) {
                framesToWrite = buffer.mDataByteSize / (2 * sizeof(float));
            }

            // SIMPLE: Read from circular buffer using simple modulo arithmetic
            // This handles both sine mode and engine mode identically
            int readPtr = ctx->readPointer.load();
            int writePtr = ctx->writePointer.load();

            // Calculate how much data is available in circular buffer
            int available;
            if (writePtr >= readPtr) {
                available = writePtr - readPtr;
            } else {
                available = (bufferSize - readPtr) + writePtr;
            }

            int framesToRead = std::min(static_cast<int>(framesToWrite), available);

            // Diagnostic: Track buffer status
            if (framesToRead < static_cast<int>(framesToWrite)) {
                // Underrun detected - increment counter
                ctx->underrunCount.fetch_add(1);
                // Log underrun periodically (every 10th underrun to avoid spam)
                if (ctx->underrunCount.load() % 10 == 0) {
                    std::cout << "[Audio Diagnostics] Buffer underrun #" << ctx->underrunCount.load()
                              << " - requested: " << framesToWrite << ", available: " << available << "\n";
                }
            }

            // Copy from circular buffer to AudioUnit output
            for (int frame = 0; frame < framesToRead; frame++) {
                int readPos = (readPtr + frame) % bufferSize;
                data[frame * 2] = ctx->circularBuffer[readPos * 2];
                data[frame * 2 + 1] = ctx->circularBuffer[readPos * 2 + 1];
            }

            // Fill rest with silence if underrun
            if (framesToRead < static_cast<int>(framesToWrite)) {
                int silenceFrames = framesToWrite - framesToRead;
                std::memset(data + framesToRead * 2, 0, silenceFrames * 2 * sizeof(float));

                // Set buffer status for diagnostics
                ctx->bufferStatus = (available < bufferSize / 8) ? 2 : 1;
            } else {
                ctx->bufferStatus = 0;  // Normal
            }

            // CRITICAL: Advance read pointer by only the frames we actually read
            // This prevents the read pointer from leaping over available data and creating gaps
            // Both sine mode and engine mode use the same read pointer management
            int newReadPtr = (readPtr + framesToRead) % bufferSize;
            ctx->readPointer.store(newReadPtr);
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
    // CRITICAL FIX: Update frequency to 60Hz to match GUI framerate
    // GUI updates at 60Hz, so CLI should match for consistent audio synthesis
    const double updateInterval = 1.0 / 60.0;  // 60Hz - matches GUI framerate
    const int framesPerUpdate = sampleRate / 60;  // 735 frames per update

    // ============================================================================
    // SINE MODE: Generate RPM-linked sine wave test tone
    // ============================================================================
    if (args.sineMode) {
        std::cout << "\n=== RPM-LINKED SINE WAVE TEST MODE ===\n";
        std::cout << "Generating sine wave with frequency linked to engine RPM\n";
        std::cout << "Mapping: 600 RPM = 100Hz, 6000 RPM = 1000Hz\n";
        std::cout << "This tests if the audio path can handle dynamic frequency changes\n\n";

        // Create engine simulator for RPM data
        EngineSimConfig config = {};
        config.sampleRate = sampleRate;
        config.inputBufferSize = 1024;
        config.audioBufferSize = 96000;  // Match GUI - 2+ seconds at 44.1kHz
        config.simulationFrequency = 10000;
        config.fluidSimulationSteps = 8;
        config.targetSynthesizerLatency = 0.02;
        config.volume = 1.0f;
        config.convolutionLevel = 0.5f;
        config.airNoise = 1.0f;

        EngineSimHandle handle = nullptr;
        EngineSimResult result = g_engineAPI.Create(&config, &handle);
        if (result != ESIM_SUCCESS || handle == nullptr) {
            std::cerr << "ERROR: Failed to create engine simulator (result: " << result << ")\n";
            if (handle) {
                std::cerr << "Error details: " << g_engineAPI.GetLastError(handle) << "\n";
            }
            return 1;
        }

        // Use default engine configuration
        std::string configPath = "engine-sim-bridge/engine-sim/assets/main.mr";
        std::string assetBasePath = "engine-sim-bridge/engine-sim";

        // Resolve to absolute paths
        try {
            std::filesystem::path scriptPath(configPath);
            if (scriptPath.is_relative()) {
                scriptPath = std::filesystem::absolute(scriptPath);
            }
            scriptPath = scriptPath.lexically_normal();
            configPath = scriptPath.string();

            std::filesystem::path assetPath(assetBasePath);
            if (assetPath.is_relative()) {
                assetPath = std::filesystem::absolute(assetPath);
            }
            assetPath = assetPath.lexically_normal();
            assetBasePath = assetPath.string();
        } catch (const std::filesystem::filesystem_error& e) {
            std::cerr << "ERROR: Failed to resolve paths: " << e.what() << "\n";
            g_engineAPI.Destroy(handle);
            return 1;
        }

        // Load engine configuration
        result = g_engineAPI.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());
        if (result != ESIM_SUCCESS) {
            std::cerr << "ERROR: Failed to load engine config: " << g_engineAPI.GetLastError(handle) << "\n";
            g_engineAPI.Destroy(handle);
            return 1;
        }
        std::cout << "[Engine configuration loaded]\n";

        // Start audio thread
        result = g_engineAPI.StartAudioThread(handle);
        if (result != ESIM_SUCCESS) {
            std::cerr << "ERROR: Failed to start audio thread: " << g_engineAPI.GetLastError(handle) << "\n";
            g_engineAPI.Destroy(handle);
            return 1;
        }
        std::cout << "[Audio thread started]\n";

        // Enable ignition
        g_engineAPI.SetIgnition(handle, 1);
        std::cout << "[Ignition enabled]\n";

        // Initialize audio player for real-time playback
        AudioPlayer* audioPlayer = nullptr;
        if (args.playAudio) {
            audioPlayer = new AudioPlayer();
            if (!audioPlayer->initialize(sampleRate)) {
                std::cerr << "ERROR: Failed to initialize audio player\n";
                delete audioPlayer;
                g_engineAPI.Destroy(handle);
                return 1;
            }

            // Note: We DON'T set engine handle for sine mode - sine wave is generated in main loop
            // and written to circular buffer, just like engine audio

            std::cout << "[Audio initialized for real-time playback]\n";
            std::cout << "[Playing " << args.duration << " seconds of RPM-linked sine wave...]\n";
            std::cout << "[Press Ctrl+C to stop playback]\n";

            // Pre-fill circular buffer BEFORE starting playback
            // Use moderate buffering (0.5s) to prevent underruns while minimizing latency
            // This reduces delay from ~1s to ~50-100ms for responsive RPM changes
            std::cout << "Pre-filling audio buffer...\n";
            const int framesPerUpdate = sampleRate / 60;  // 735 frames per update at 60Hz
            std::vector<float> silenceBuffer(framesPerUpdate * 2, 0.0f);
            const int preFillIterations = 40;  // Reduced from 180 (3s) to 40 (0.67s) for lower latency
            for (int i = 0; i < preFillIterations; i++) {
                audioPlayer->addToCircularBuffer(silenceBuffer.data(), framesPerUpdate);
            }
            std::cout << "Buffer pre-filled with " << (preFillIterations * framesPerUpdate)
                      << " frames (" << (preFillIterations / 60.0) << " seconds)\n";

            // Start playback (callback will read from circular buffer)
            audioPlayer->start();
        }

        // Simulation parameters
        const double updateInterval = 1.0 / 60.0;  // 60 Hz physics update
        double currentTime = 0.0;
        double throttle = 0.0;
        double smoothedThrottle = 0.0;

        std::cout << "\nStarting simulation...\n";

        // Warmup phase - reduced to 0.2s to minimize latency
        const double warmupDuration = 0.2;
        std::cout << "Warmup phase...\n";
        while (currentTime < warmupDuration) {
            EngineSimStats stats = {};
            g_engineAPI.GetStats(handle, &stats);

            // Gradual throttle increase during warmup
            double warmupThrottle;
            if (currentTime < 1.0) {
                warmupThrottle = 0.5;
            } else {
                warmupThrottle = 0.7;
            }

            smoothedThrottle = warmupThrottle * 0.5 + smoothedThrottle * 0.5;
            g_engineAPI.SetThrottle(handle, smoothedThrottle);
            g_engineAPI.Update(handle, updateInterval);

            currentTime += updateInterval;

            if (static_cast<int>(currentTime * 2) % 2 == 0) {
                std::cout << "  Warmup: " << stats.currentRPM << " RPM, Frequency: "
                          << (stats.currentRPM / 600.0 * 100.0) << " Hz\n";
            }
        }

        // Reset time for main simulation
        currentTime = 0.0;

        // CRITICAL: Reset circular buffer to eliminate warmup audio latency
        // This discards warmup audio so RPM changes are immediately audible
        if (audioPlayer) {
            audioPlayer->resetCircularBuffer();
            std::cout << "Circular buffer reset after warmup to minimize latency\n";
        }

        // Enable starter motor
        g_engineAPI.SetStarterMotor(handle, 1);

        // Calculate smooth RPM target that starts from current warmup RPM
        EngineSimStats endWarmupStats = {};
        g_engineAPI.GetStats(handle, &endWarmupStats);
        double startRPM = std::max(endWarmupStats.currentRPM, 100.0);  // At least 100 RPM to avoid zero

        // Main simulation loop with RPM ramp
        std::cout << "\nRPM ramp phase (" << static_cast<int>(startRPM) << " -> 6000 RPM)...\n";
        auto startTime = std::chrono::steady_clock::now();
        int lastProgress = 0;

        // Setup keyboard input for interactive mode in sine mode
        KeyboardInput* keyboardInput = nullptr;
        double interactiveTargetRPM = 0.0;
        double interactiveLoad = 0.7;  // Start with moderate throttle for sine mode
        double baselineLoad = interactiveLoad;
        bool wKeyPressed = false;

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

        // CRITICAL FIX: Initialize timing control for 60Hz loop rate
        // Use absolute time to prevent timing drift
        auto loopStartTime = std::chrono::steady_clock::now();
        auto absoluteStartTime = loopStartTime;
        int iterationCount = 0;

        while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
            // Get current stats
            EngineSimStats stats = {};
            g_engineAPI.GetStats(handle, &stats);

            // Disable starter motor once engine is running
            const double minSustainedRPM = 550.0;
            if (stats.currentRPM > minSustainedRPM) {
                g_engineAPI.SetStarterMotor(handle, 0);
            }

            // Handle keyboard input in interactive mode
            if (args.interactive && keyboardInput) {
                static int lastKey = -1;
                int key = keyboardInput->getKey();

                if (key < 0) {
                    lastKey = -1;
                    wKeyPressed = false;
                } else if (key != lastKey) {
                    switch (key) {
                        case 27:  // ESC
                        case 'q':
                        case 'Q':
                            g_running.store(false);
                            break;
                        case 'w':
                        case 'W':
                            wKeyPressed = true;
                            interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                            baselineLoad = interactiveLoad;
                            break;
                        case ' ':
                            interactiveLoad = 0.0;
                            baselineLoad = 0.0;
                            break;
                        case 'r':
                        case 'R':
                            interactiveLoad = 0.2;
                            baselineLoad = 0.2;
                            break;
                        case 'a':
                            // Toggle ignition - only lowercase to avoid conflict with UP arrow (65)
                            {
                                static bool ignitionState = true;
                                ignitionState = !ignitionState;
                                g_engineAPI.SetIgnition(handle, ignitionState ? 1 : 0);
                                std::cout << "\nIgnition " << (ignitionState ? "enabled" : "disabled") << "\n";
                            }
                            break;
                        case 's':
                        case 'S':
                            {
                                static bool starterState = false;
                                starterState = !starterState;
                                g_engineAPI.SetStarterMotor(handle, starterState ? 1 : 0);
                                std::cout << "\nStarter motor " << (starterState ? "enabled" : "disabled") << "\n";
                            }
                            break;
                        case 65:  // UP arrow (macOS) - also 'A' which we want to avoid
                            interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                            baselineLoad = interactiveLoad;
                            break;
                        case 66:  // DOWN arrow
                            interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                            baselineLoad = interactiveLoad;
                            break;
                        case 'k':
                        case 'K':
                            interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                            baselineLoad = interactiveLoad;
                            break;
                        case 'j':
                        case 'J':
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

            // Calculate target throttle based on desired RPM ramp
            // Start from current warmup RPM and ramp up smoothly
            // In interactive mode, use interactive load; in auto mode, use RPM ramp
            if (args.interactive) {
                throttle = interactiveLoad;
            } else {
                double targetRPM = startRPM + ((6000.0 - startRPM) * (currentTime / args.duration));
                // Simple RPM controller
                double rpmError = targetRPM - stats.currentRPM;
                throttle = std::max(0.0, std::min(1.0, rpmError / 1000.0));  // Simple P-controller
            }

            // Apply throttle smoothing
            smoothedThrottle = throttle * 0.2 + smoothedThrottle * 0.8;
            g_engineAPI.SetThrottle(handle, smoothedThrottle);
            g_engineAPI.Update(handle, updateInterval);

            // SIMPLE: Generate sine wave samples and write to circular buffer
            // This tests the EXACT same audio path as engine audio
            if (audioPlayer && audioPlayer->getContext()) {
                AudioUnitContext* ctx = audioPlayer->getContext();
                const int bufferSize = static_cast<int>(ctx->circularBufferSize);

                // SIMPLE: Always generate samples and write to buffer
                // Let the AudioUnit callback handle reading and wrapping
                std::vector<float> sineBuffer(framesPerUpdate * 2);

                // Calculate frequency based on RPM
                double frequency = (stats.currentRPM / 600.0) * 100.0;  // 600 RPM -> 100Hz, 6000 RPM -> 1000Hz

                // Maintain continuous phase across main loop iterations
                static double currentPhase = 0.0;

                // Generate sine wave samples
                for (int i = 0; i < framesPerUpdate; i++) {
                    // Calculate phase increment per sample
                    double phaseIncrement = (2.0 * M_PI * frequency) / 44100.0;
                    currentPhase += phaseIncrement;

                    // Generate sample
                    float sample = static_cast<float>(std::sin(currentPhase) * 0.9);  // 90% volume

                    // Write stereo samples
                    sineBuffer[i * 2] = sample;     // Left channel
                    sineBuffer[i * 2 + 1] = sample; // Right channel
                }

                // Write to circular buffer (same path as engine audio)
                audioPlayer->addToCircularBuffer(sineBuffer.data(), framesPerUpdate);
            }

            currentTime += updateInterval;

            // Display progress
            if (args.interactive) {
                double frequency = (stats.currentRPM / 600.0) * 100.0;
                std::cout << "\r[" << std::fixed << std::setprecision(0) << std::setw(4) << stats.currentRPM << " RPM] ";
                std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(smoothedThrottle * 100) << "%] ";
                std::cout << "[Frequency: " << std::setw(4) << static_cast<int>(frequency) << " Hz] ";
                std::cout << std::flush;
            } else {
                int progress = static_cast<int>(currentTime * 100 / args.duration);
                if (progress != lastProgress && progress % 10 == 0) {
                    double frequency = (stats.currentRPM / 600.0) * 100.0;
                    std::cout << "  Progress: " << progress << "% | RPM: " << static_cast<int>(stats.currentRPM)
                              << " | Frequency: " << static_cast<int>(frequency) << " Hz\r" << std::flush;
                    lastProgress = progress;
                }
            }

            // CRITICAL FIX: Add timing control to maintain 60Hz loop rate
            // Use absolute time-based timing to prevent drift from sleep inaccuracies
            // This is essential to keep audio generation rate synchronized with consumption rate
            iterationCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsedTime = std::chrono::duration_cast<std::chrono::microseconds>(
                now - absoluteStartTime
            ).count();
            auto targetTime = static_cast<long long>(iterationCount * updateInterval * 1000000);
            auto sleepTime = targetTime - elapsedTime;

            if (sleepTime > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleepTime));
            }
        }

        std::cout << "\n\nSimulation complete!\n";

        // Get final statistics
        EngineSimStats finalStats = {};
        g_engineAPI.GetStats(handle, &finalStats);
        std::cout << "\nFinal Statistics:\n";
        std::cout << "  RPM: " << static_cast<int>(finalStats.currentRPM) << "\n";
        std::cout << "  Final Frequency: " << static_cast<int>((finalStats.currentRPM / 600.0) * 100.0) << " Hz\n";

        // Cleanup
        if (keyboardInput) {
            delete keyboardInput;
        }

        if (audioPlayer) {
            audioPlayer->stop();
            audioPlayer->waitForCompletion();
            std::cout << "[Playback complete]\n";
            delete audioPlayer;
        }

        g_engineAPI.Destroy(handle);

        // Note: WAV export not supported in RPM-linked sine mode
        // since the sine wave is generated in real-time based on RPM
        if (args.outputWav) {
            std::cout << "\nWARNING: WAV export not supported in RPM-linked sine mode\n";
            std::cout << "The sine wave is generated in real-time based on engine RPM.\n";
        }

        return 0;
    }

    // ============================================================================
    // ENGINE SIMULATION MODE
    // ============================================================================

    // Configure simulator
    EngineSimConfig config = {};
    config.sampleRate = sampleRate;
    config.inputBufferSize = 1024;
    config.audioBufferSize = 96000;  // Match GUI - 2+ seconds at 44.1kHz
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
    config.airNoise = 1.0f;

    // Create simulator
    EngineSimHandle handle = nullptr;
    EngineSimResult result = g_engineAPI.Create(&config, &handle);
    if (result != ESIM_SUCCESS || handle == nullptr) {
        std::cerr << "ERROR: Failed to create simulator: " << g_engineAPI.GetLastError(handle) << "\n";
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
        g_engineAPI.Destroy(handle);
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
            g_engineAPI.Destroy(handle);
            return 1;
        }
    }

    // Note: All paths have been converted to absolute paths, so we don't need to change directories.
    // The Piranha compiler can now find imports using the absolute paths provided.
    // Load the engine configuration script with absolute paths
    result = g_engineAPI.LoadScript(handle, configPath.c_str(), assetBasePath.c_str());

    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to load engine config: " << g_engineAPI.GetLastError(handle) << "\n";
        g_engineAPI.Destroy(handle);
        return 1;
    }

    std::cout << "[2/5] Engine configuration loaded: " << configPath << "\n";
    std::cout << "[2.5/5] Impulse responses loaded automatically\n";

    // Start the audio thread only for interactive/play mode (async rendering).
    // For WAV-only export, use synchronous EngineSimRender() which avoids
    // the producer/consumer race condition that causes crackles.
    if (args.playAudio) {
        result = g_engineAPI.StartAudioThread(handle);
        if (result != ESIM_SUCCESS) {
            std::cerr << "ERROR: Failed to start audio thread: " << g_engineAPI.GetLastError(handle) << "\n";
            g_engineAPI.Destroy(handle);
            return 1;
        }
        std::cout << "[3/5] Audio thread started (asynchronous rendering)\n";
    } else {
        std::cout << "[3/5] Synchronous rendering mode (WAV export)\n";
    }

    // Auto-enable ignition so the engine can run
    g_engineAPI.SetIgnition(handle, 1);
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

            // Pre-fill circular buffer before starting AudioUnit playback
            // Use moderate buffering (0.5s) to absorb timing jitter while minimizing latency
            // This reduces delay from ~1s to ~50-100ms for responsive RPM changes
            const int preFillFrames = (sampleRate * 40) / 60;  // 40 iterations at 60Hz (~0.67s = ~29400 frames) for lower latency
            std::vector<float> silenceBuffer(framesPerUpdate * 2, 0.0f);
            for (int i = 0; i < preFillFrames / framesPerUpdate; i++) {
                audioPlayer->addToCircularBuffer(silenceBuffer.data(), framesPerUpdate);
            }
            std::cout << "[5/5] Pre-filled circular buffer with " << preFillFrames << " frames of silence\n";

            // Start real-time streaming after pre-fill
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

    // CRITICAL: Timing control for 60Hz loop rate (matches sine mode)
    // Without this, the main loop runs at ~90kHz, overwhelming the circular buffer
    // and causing crackles in audio playback
    auto absoluteStartTime = std::chrono::steady_clock::now();
    int totalIterationCount = 0;

    // Initialize warmup phase
    g_engineAPI.SetStarterMotor(handle, 1);
    bool starterDisengaged = false;
    const double warmupDuration = 0.5;  // Reduced from 4.0s to minimize latency
    const double maxStarterTime = 0.4;   // Reduced proportionally
    const double minSustainedRPM = 300.0;
    std::cout << "Starting engine cranking sequence...\n";

    // Set initial warmup RPM target that increases gradually
    double warmupTargetRPM = 200.0;  // Start with very low target

    while (currentTime < warmupDuration) {
        EngineSimStats stats = {};
        g_engineAPI.GetStats(handle, &stats);

        // Gradually increase target RPM during warmup (shortened for reduced latency)
        if (currentTime < 0.2) {
            warmupTargetRPM = 800.0;   // Start at idle
        } else {
            warmupTargetRPM = 1000.0;  // Slightly above idle
        }

        // Set RPM controller target for warmup
        rpmController.setTargetRPM(warmupTargetRPM);

        // Use RPM controller to calculate throttle
        throttle = rpmController.update(stats.currentRPM, updateInterval);

        // Apply smoothing
        smoothedThrottle = throttle * 0.5 + smoothedThrottle * 0.5;
        g_engineAPI.SetThrottle(handle, smoothedThrottle);
        g_engineAPI.Update(handle, updateInterval);
        currentTime += updateInterval;

        // CRITICAL: Drain synthesizer audio during warmup to prevent stale data buildup.
        // For play mode: feed circular buffer via async read.
        // For WAV-only mode: drain via synchronous render and discard (warmup audio not wanted in WAV).
        if (args.playAudio && audioPlayer) {
            std::vector<float> warmupAudio(framesPerUpdate * 2);
            int warmupRead = 0;
            for (int retry = 0; retry <= 3 && warmupRead < framesPerUpdate; retry++) {
                int readThisTime = 0;
                g_engineAPI.ReadAudioBuffer(handle,
                    warmupAudio.data() + warmupRead * 2,
                    framesPerUpdate - warmupRead, &readThisTime);
                if (readThisTime > 0) warmupRead += readThisTime;
                if (warmupRead < framesPerUpdate && retry < 3) {
                    std::this_thread::sleep_for(std::chrono::microseconds(500));
                }
            }
            if (warmupRead > 0) {
                audioPlayer->addToCircularBuffer(warmupAudio.data(), warmupRead);
            }
        } else if (!args.playAudio) {
            // WAV-only mode: drain warmup audio synchronously and discard it
            std::vector<float> discardBuf(framesPerUpdate * 2);
            int discarded = 0;
            g_engineAPI.Render(handle, discardBuf.data(), framesPerUpdate, &discarded);
        }

        // Check if engine has started and disengage starter if needed
        static int sustainedRPMCount = 0;
        const int requiredSustainedFrames = 15;

        if (!starterDisengaged) {
            if (stats.currentRPM > minSustainedRPM) {
                sustainedRPMCount++;
                if (sustainedRPMCount >= requiredSustainedFrames) {
                    g_engineAPI.SetStarterMotor(handle, 0);
                    starterDisengaged = true;
                    std::cout << "Engine started! Disabling starter motor at " << stats.currentRPM << " RPM.\n";
                }
            } else {
                sustainedRPMCount = 0;
            }
        }

        // Emergency cutoff
        if (!starterDisengaged && currentTime >= maxStarterTime) {
            g_engineAPI.SetStarterMotor(handle, 0);
            starterDisengaged = true;
            std::cout << "Starter motor timeout after " << maxStarterTime << " seconds.\n";
        }

        if (static_cast<int>(currentTime * 2) % 2 == 0) {
            std::cout << "  Cranking: " << std::fixed << std::setprecision(0) << stats.currentRPM
                      << " RPM, Target: " << std::fixed << std::setprecision(0) << warmupTargetRPM
                      << ", Throttle: " << std::fixed << std::setprecision(2) << smoothedThrottle;
            if (!starterDisengaged) {
                std::cout << " (starter ON)";
            } else {
                std::cout << " (starter OFF)";
            }
            std::cout << "\n";
        }

        // Timing control: pace warmup loop to 60Hz when playing audio
        if (args.playAudio) {
            totalIterationCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - absoluteStartTime).count();
            auto targetUs = static_cast<long long>(totalIterationCount * updateInterval * 1000000);
            auto sleepUs = targetUs - elapsedUs;
            if (sleepUs > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
            }
        }
    }

    std::cout << "Warmup complete. Switching to target RPM control...\n";

    // CRITICAL: Reset circular buffer to eliminate warmup audio latency
    // This discards warmup audio so RPM changes are immediately audible
    if (args.playAudio && audioPlayer) {
        audioPlayer->resetCircularBuffer();
        std::cout << "Circular buffer reset after warmup to minimize latency\n";
    }

    // Set final target RPM
    if (args.targetRPM > 0) {
        rpmController.setTargetRPM(args.targetRPM);
    }

    currentTime = 0.0;

    // Maintain starter motor state from warmup phase
    bool starterEnabled = !starterDisengaged;
    bool engineHasStarted = starterDisengaged;  // Engine has started if starter is disengaged
    const double runningRPMThreshold = 350.0;  // Lower threshold for sustained running
    const double restartRPM = 150.0;       // RPM threshold to re-enable starter if engine stalls

  while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
    // Get current stats
    EngineSimStats stats = {};
    g_engineAPI.GetStats(handle, &stats);

    // Handle starter motor logic with improved logic
    // Only disable starter if it's currently enabled and engine is running
    if (starterEnabled && stats.currentRPM > runningRPMThreshold) {
        g_engineAPI.SetStarterMotor(handle, 0);
        starterEnabled = false;
        engineHasStarted = true;
        std::cout << "Engine started! Disabling starter motor at " << stats.currentRPM << " RPM.\n";
    }
    // Only re-enable starter if it's currently disabled, engine has started before,
    // and engine has stalled
    else if (!starterEnabled && engineHasStarted && stats.currentRPM < restartRPM) {
        g_engineAPI.SetStarterMotor(handle, 1);
        starterEnabled = true;
        std::cout << "Engine stalled. Re-enabling starter motor.\n";
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
                            g_engineAPI.SetIgnition(handle, ignitionState ? 1 : 0);
                            std::cout << "Ignition " << (ignitionState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 's':
                    case 'S':
                        // Toggle starter motor - only on initial press, not repeat
                        {
                            static bool starterState = false;
                            starterState = !starterState;
                            g_engineAPI.SetStarterMotor(handle, starterState ? 1 : 0);
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
                // Quick decay back to baseline (50% per frame for immediate response)
                interactiveLoad = std::max(baselineLoad, interactiveLoad * 0.5);
            }

            // In interactive mode, use RPM control if target is set
            if (interactiveTargetRPM > 0) {
                double pTerm, iTerm, dTerm;
                throttle = rpmController.update(stats.currentRPM, updateInterval);
                rpmController.getDebugInfo(pTerm, iTerm, dTerm);

                // Display debug info for stability verification
                if (framesRendered % 60 == 0) {  // Every second at 60fps
                    std::cout << "RPM Control Debug - P: " << std::fixed << std::setprecision(3) << pTerm
                              << ", I: " << iTerm << ", Target: " << interactiveTargetRPM
                              << ", Current: " << stats.currentRPM << "\n";
                }
            } else {
                throttle = interactiveLoad;
            }
        }
        else if (args.targetRPM > 0 && args.targetLoad < 0) {
            // RPM control mode
            throttle = rpmController.update(stats.currentRPM, updateInterval);

            // More frequent output to verify stabilization
            if (framesRendered % 30 == 0) {  // Every 0.5 seconds at 60fps
                double pTerm, iTerm, dTerm;
                rpmController.getDebugInfo(pTerm, iTerm, dTerm);
                std::cout << "RPM Control: " << stats.currentRPM << "/" << args.targetRPM
                          << ", Throttle: " << std::fixed << std::setprecision(3) << smoothedThrottle
                          << ", Load: " << static_cast<int>(stats.currentLoad * 100) << "%"
                          << ", P: " << pTerm << ", I: " << iTerm << "\n";
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

        // Apply throttle smoothing matching GUI behavior
        // 50/50 split provides responsive control while preventing audio artifacts
        // Matches engine_sim_application.cpp line 798-801 smoothing behavior
        smoothedThrottle = throttle * 0.5 + smoothedThrottle * 0.5;

        g_engineAPI.SetThrottle(handle, smoothedThrottle);
        g_engineAPI.Update(handle, updateInterval);
        auto simEnd = std::chrono::steady_clock::now();

        // Debug: Log timing issues for synchronization problems
        auto simDuration = std::chrono::duration_cast<std::chrono::microseconds>(simEnd - simStart).count();
        if (simDuration > 10000) {  // If physics update takes more than 10ms
            static int warningCount = 0;
            if (warningCount++ % 30 == 0) {  // Log periodically to avoid spam
                std::cout << "[Timing Warning] Physics update took " << (simDuration / 1000.0)
                          << "ms (target: " << (updateInterval * 1000) << "ms)\n";
            }
        }

        // Render audio
        int framesToRender = framesPerUpdate;

        // In non-interactive mode, check buffer limits
        if (!args.interactive) {
            int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
            framesToRender = std::min(framesPerUpdate, totalExpectedFrames - framesProcessed);
        }

        if (framesToRender > 0) {
            int samplesWritten = 0;

            // CRITICAL: Read from synthesizer into intermediate circular buffer (simplified)
            // This matches GUI's logic but uses simple circular buffer approach
            // GUI reads from synthesizer -> writes to m_audioBuffer -> hardware reads from m_audioBuffer
            // CLI reads from synthesizer -> writes to circularBuffer -> AudioUnit reads from circularBuffer
            if (args.playAudio && audioPlayer && audioPlayer->getContext()) {
                // Read from synthesizer with retry to handle audio thread latency
                // The audio thread may not have finished processing by the time
                // we read, so we retry with a short sleep to get full frames.
                std::vector<float> tempBuffer(framesPerUpdate * 2);
                int totalRead = 0;
                const int maxRetries = 3;

                for (int retry = 0; retry <= maxRetries && totalRead < framesPerUpdate; retry++) {
                    int remaining = framesPerUpdate - totalRead;
                    int readThisTime = 0;
                    result = g_engineAPI.ReadAudioBuffer(handle,
                        tempBuffer.data() + totalRead * 2,
                        remaining, &readThisTime);
                    if (result == ESIM_SUCCESS && readThisTime > 0) {
                        totalRead += readThisTime;
                    }
                    if (totalRead < framesPerUpdate && retry < maxRetries) {
                        // Brief yield to let audio thread process
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                }
                samplesWritten = totalRead;

                if (totalRead > 0) {
                    // Write to intermediate circular buffer (AudioUnit will read from here)
                    audioPlayer->addToCircularBuffer(tempBuffer.data(), totalRead);
                }
            }

            // WAV export: use synchronous rendering directly to file buffer
            // EngineSimRender generates audio inline (no async audio thread needed),
            // eliminating the producer/consumer race that causes zero-gap crackles
            if (!args.playAudio) {
                float* writePtr = audioBuffer.data() + framesRendered * channels;
                result = g_engineAPI.Render(handle, writePtr, framesToRender, &samplesWritten);

                if (result == ESIM_SUCCESS && samplesWritten > 0) {
                    framesRendered += samplesWritten;
                    framesProcessed += samplesWritten;
                }
            } else {
                // AudioUnit mode: track progress for WAV export if needed
                framesProcessed += framesToRender;
                framesRendered += framesToRender;
            }
        }

        currentTime += updateInterval;

        // Display progress and diagnostics
        if (args.interactive) {
            displayHUD(stats.currentRPM, smoothedThrottle, interactiveTargetRPM, stats);

            // Log buffer diagnostics periodically in interactive mode
            static int lastDiagnosticFrame = 0;
            if (framesProcessed - lastDiagnosticFrame >= 180) {  // Every 3 seconds at 60fps
                int writePtr, readPtr, available, status;
                audioPlayer->getBufferDiagnostics(writePtr, readPtr, available, status);
                std::cout << "[Buffer Diagnostics] Write: " << writePtr << ", Read: " << readPtr
                          << ", Available: " << available << ", Status: " << status
                          << ", Underruns: " << (audioPlayer->getContext() ? audioPlayer->getContext()->underrunCount.load() : 0) << "\n";
                lastDiagnosticFrame = framesProcessed;
            }
        } else {
            int totalExpectedFrames = static_cast<int>(args.duration * sampleRate);
            int progress = static_cast<int>(framesProcessed * 100 / totalExpectedFrames);
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% (" << framesProcessed << " frames)\r" << std::flush;
                lastProgress = progress;

                // Log buffer issues periodically during non-interactive mode
                if (audioPlayer) {
                    int writePtr, readPtr, available, status;
                    audioPlayer->getBufferDiagnostics(writePtr, readPtr, available, status);
                    if (status > 0) {
                        std::cout << " [Buffer: " << (status == 1 ? "warning" : "critical")
                                  << ", Available: " << available << "]\r";
                    }
                }
            }
        }

        // CRITICAL: Timing control to maintain 60Hz loop rate for audio playback
        // Without this, the loop runs at ~90kHz, overwhelming the circular buffer
        // and causing crackles. Uses absolute time to prevent drift.
        if (args.playAudio) {
            totalIterationCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - absoluteStartTime).count();
            auto targetUs = static_cast<long long>(totalIterationCount * updateInterval * 1000000);
            auto sleepUs = targetUs - elapsedUs;
            if (sleepUs > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
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
    g_engineAPI.GetStats(handle, &finalStats);
    std::cout << "\nFinal Statistics:\n";
    std::cout << "  RPM: " << static_cast<int>(finalStats.currentRPM) << "\n";
    std::cout << "  Load: " << static_cast<int>(finalStats.currentLoad * 100) << "%\n";
    std::cout << "  Exhaust Flow: " << finalStats.exhaustFlow << " m^3/s\n";
    std::cout << "  Manifold Pressure: " << static_cast<int>(finalStats.manifoldPressure) << " Pa\n";

    g_engineAPI.Destroy(handle);

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
