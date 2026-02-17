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

    // Cursor-chasing buffer (matches GUI approach)
    // Uses hardware feedback to maintain 100ms lead, preventing underruns
    float* circularBuffer;                // Intermediate buffer
    size_t circularBufferSize;            // Buffer capacity (96000 = 2+ seconds)
    std::atomic<int> writePointer;        // Write position in circular buffer
    std::atomic<int> readPointer;         // Read position (hardware playback cursor)
    std::atomic<int> underrunCount;       // Count of buffer underruns
    int bufferStatus;                     // Buffer status for diagnostics (0=normal, 1=warning, 2=critical)

    // Cursor-chasing state
    std::atomic<int64_t> totalFramesRead; // Total frames read by callback (for cursor tracking)
    int sampleRate;                       // Sample rate for calculations

    AudioUnitContext() : engineHandle(nullptr), isPlaying(false),
                        circularBuffer(nullptr), circularBufferSize(96000),
                        writePointer(0), readPointer(0),
                        underrunCount(0), bufferStatus(0),
                        totalFramesRead(0), sampleRate(44100) {}
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

        // Initialize AudioUnit
        status = AudioUnitInitialize(audioUnit);
        if (status != noErr) {
            std::cerr << "ERROR: Failed to initialize AudioUnit: " << status << "\n";
            cleanup();
            return false;
        }

        // Cursor-chasing buffer: 2+ seconds capacity (matches GUI)
        context->circularBufferSize = 96000;  // 96000 samples = 2+ seconds at 44.1kHz
        context->circularBuffer = new float[context->circularBufferSize * 2];  // Stereo
        std::memset(context->circularBuffer, 0, context->circularBufferSize * 2 * sizeof(float));

        // Initialize pointers - start with 100ms offset (cursor-chasing approach)
        // GUI starts writePointer at 4410 (100ms) for initial lead
        context->writePointer.store(static_cast<int>(sr * 0.1));  // 100ms ahead
        context->readPointer.store(0);
        context->totalFramesRead.store(0);

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
        std::cout << "[Audio] Cursor-chasing mode: 1s buffer with 100ms target lead\n";
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

            // Calculate available frames (current lead)
            int bufferSize = static_cast<int>(context->circularBufferSize);
            if (writePtr >= readPtr) {
                available = writePtr - readPtr;
            } else {
                available = (bufferSize - readPtr) + writePtr;
            }

            // Set status based on cursor-chasing target (100ms = 4410 samples)
            // Normal: within 50-200ms lead (2205-8820 samples)
            // Warning: 25-50ms or 200-400ms lead
            // Critical: <25ms lead (but still has data)
            // Underrun: no data
            const int targetLead = static_cast<int>(context->sampleRate * 0.1);  // 4410
            if (available >= targetLead / 2 && available <= targetLead * 2) {
                status = 0;  // Normal: 50-200ms lead
            } else if (available >= targetLead / 4 || available <= targetLead * 4) {
                status = 1;  // Warning: 25-50ms or 200-400ms lead
            } else if (available > 0) {
                status = 2;  // Critical: <25ms but not underrun yet
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
            // Reset with 100ms offset (cursor-chasing initial state)
            context->writePointer.store(static_cast<int>(context->sampleRate * 0.1));
            context->readPointer.store(0);
            context->totalFramesRead.store(0);
            // Clear buffer contents
            std::memset(context->circularBuffer, 0, context->circularBufferSize * 2 * sizeof(float));
        }
    }

    // Calculate how many samples to read based on cursor-chasing logic (matches GUI)
    // Returns number of samples to maintain 100ms lead, or 0 if buffer is too full
    int calculateCursorChasingSamples(int defaultFrames) {
        if (!context) return defaultFrames;

        const int bufferSize = static_cast<int>(context->circularBufferSize);
        const int writePtr = context->writePointer.load();
        const int readPtr = context->readPointer.load();

        // Calculate current lead (distance ahead of playback cursor)
        int currentLead;
        if (writePtr >= readPtr) {
            currentLead = writePtr - readPtr;
        } else {
            currentLead = (bufferSize - readPtr) + writePtr;
        }

        // Target: 100ms ahead (matches GUI line 257)
        const int targetLead = static_cast<int>(context->sampleRate * 0.1);  // 4410 samples at 44.1kHz

        // Safety: if too far ahead (>500ms), snap back to 50ms (matches GUI lines 263-267)
        const int maxLead = static_cast<int>(context->sampleRate * 0.5);  // 22050 samples
        if (currentLead > maxLead) {
            // Reset write pointer to 50ms ahead of read pointer
            int newWritePtr = (readPtr + static_cast<int>(context->sampleRate * 0.05)) % bufferSize;
            context->writePointer.store(newWritePtr);
            currentLead = static_cast<int>(context->sampleRate * 0.05);
        }

        // Calculate target write position (100ms ahead of current read position)
        int targetWritePtr = (readPtr + targetLead) % bufferSize;

        // Calculate how many samples to write to reach target
        int maxWrite;
        if (targetWritePtr >= writePtr) {
            maxWrite = targetWritePtr - writePtr;
        } else {
            maxWrite = (bufferSize - writePtr) + targetWritePtr;
        }

        // Prevent underrun: don't write if it would make buffer smaller (matches GUI line 269-271)
        int newLead;
        if (targetWritePtr >= readPtr) {
            newLead = targetWritePtr - readPtr;
        } else {
            newLead = (bufferSize - readPtr) + targetWritePtr;
        }

        if (currentLead > newLead) {
            return 0;  // Would reduce buffer - skip this write
        }

        return std::min(maxWrite, defaultFrames);
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

            // Update read pointer (hardware playback cursor position in circular buffer)
            int newReadPtr = (readPtr + framesToRead) % bufferSize;
            ctx->readPointer.store(newReadPtr);

            // Track total frames read for cursor tracking (used by main loop)
            ctx->totalFramesRead.fetch_add(framesToRead);
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
// Shared Audio Loop Infrastructure
// ============================================================================

// Shared configuration constants
struct AudioLoopConfig {
    static constexpr int SAMPLE_RATE = 44100;
    static constexpr int CHANNELS = 2;
    static constexpr double UPDATE_INTERVAL = 1.0 / 60.0;  // 60Hz
    static constexpr int FRAMES_PER_UPDATE = SAMPLE_RATE / 60;  // 735 frames
    static constexpr int WARMUP_ITERATIONS = 3;  // Minimal warmup
    static constexpr int PRE_FILL_ITERATIONS = 40;  // 0.67s initial buffer (matches working commit)
    static constexpr int RE_PRE_FILL_ITERATIONS = 0;  // No re-pre-fill (matches working commit)
};

// Shared buffer operations - DRY: same for both modes
namespace BufferOps {
    void preFillCircularBuffer(AudioPlayer* player) {
        if (!player) return;

        std::cout << "Pre-filling audio buffer...\n";
        std::vector<float> silence(AudioLoopConfig::FRAMES_PER_UPDATE * 2, 0.0f);

        for (int i = 0; i < AudioLoopConfig::PRE_FILL_ITERATIONS; i++) {
            player->addToCircularBuffer(silence.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
        }

        std::cout << "Buffer pre-filled: " << (AudioLoopConfig::PRE_FILL_ITERATIONS * AudioLoopConfig::FRAMES_PER_UPDATE)
                  << " frames (" << (AudioLoopConfig::PRE_FILL_ITERATIONS / 60.0) << "s)\n";
    }

    void resetAndRePrefillBuffer(AudioPlayer* player) {
        if (!player) return;

        player->resetCircularBuffer();
        std::cout << "Circular buffer reset after warmup\n";

        // Re-pre-fill only if configured (0 iterations = no re-pre-fill)
        if (AudioLoopConfig::RE_PRE_FILL_ITERATIONS > 0) {
            std::vector<float> silence(AudioLoopConfig::FRAMES_PER_UPDATE * 2, 0.0f);
            for (int i = 0; i < AudioLoopConfig::RE_PRE_FILL_ITERATIONS; i++) {
                player->addToCircularBuffer(silence.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
            }
            std::cout << "Re-pre-filled: " << (AudioLoopConfig::RE_PRE_FILL_ITERATIONS * AudioLoopConfig::FRAMES_PER_UPDATE)
                      << " frames (" << (AudioLoopConfig::RE_PRE_FILL_ITERATIONS / 60.0) << "s)\n";
        }
    }
}

// Shared warmup phase logic
namespace WarmupOps {
    void runWarmup(EngineSimHandle handle, const EngineSimAPI& api, AudioPlayer* audioPlayer, bool playAudio) {
        std::cout << "Priming synthesizer pipeline (" << AudioLoopConfig::WARMUP_ITERATIONS << " iterations)...\n";

        double smoothedThrottle = 0.6;
        double currentTime = 0.0;

        for (int i = 0; i < AudioLoopConfig::WARMUP_ITERATIONS; i++) {
            EngineSimStats stats = {};
            api.GetStats(handle, &stats);

            api.SetThrottle(handle, smoothedThrottle);
            api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);

            currentTime += AudioLoopConfig::UPDATE_INTERVAL;

            std::cout << "  Priming: " << stats.currentRPM << " RPM\n";

            // Drain audio if in play mode - DISCARD to prevent crackles
            // Warmup audio contains starter motor noise and transients
            // The buffer was pre-filled with silence, callback reads from that during warmup
            if (playAudio && audioPlayer) {
                std::vector<float> discardBuffer(AudioLoopConfig::FRAMES_PER_UPDATE * 2);
                int warmupRead = 0;

                for (int retry = 0; retry <= 3 && warmupRead < AudioLoopConfig::FRAMES_PER_UPDATE; retry++) {
                    int readThisTime = 0;
                    api.ReadAudioBuffer(handle,
                        discardBuffer.data() + warmupRead * 2,
                        AudioLoopConfig::FRAMES_PER_UPDATE - warmupRead,
                        &readThisTime);

                    if (readThisTime > 0) warmupRead += readThisTime;

                    if (warmupRead < AudioLoopConfig::FRAMES_PER_UPDATE && retry < 3) {
                        std::this_thread::sleep_for(std::chrono::microseconds(500));
                    }
                }
                // DISCARD warmup audio - do NOT send to circular buffer
                // Buffer was pre-initialized with 100ms offset, so callback reads silence during warmup
            }
        }
    }
}

// Shared timing control
namespace TimingOps {
    struct LoopTimer {
        std::chrono::steady_clock::time_point absoluteStartTime;
        int iterationCount;

        LoopTimer() : absoluteStartTime(std::chrono::steady_clock::now()), iterationCount(0) {}

        void sleepToMaintain60Hz() {
            iterationCount++;
            auto now = std::chrono::steady_clock::now();
            auto elapsedUs = std::chrono::duration_cast<std::chrono::microseconds>(
                now - absoluteStartTime).count();
            auto targetUs = static_cast<long long>(iterationCount * AudioLoopConfig::UPDATE_INTERVAL * 1000000);
            auto sleepUs = targetUs - elapsedUs;

            if (sleepUs > 0) {
                std::this_thread::sleep_for(std::chrono::microseconds(sleepUs));
            }
        }
    };
}

// Audio source abstraction - the ONLY difference between modes
class IAudioSource {
public:
    virtual ~IAudioSource() = default;
    virtual bool generateAudio(std::vector<float>& buffer, int frames) = 0;
    virtual void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle) = 0;
};

// Sine wave audio source
class SineAudioSource : public IAudioSource {
private:
    EngineSimHandle handle;
    const EngineSimAPI& api;
    double currentPhase;

public:
    SineAudioSource(EngineSimHandle h, const EngineSimAPI& a)
        : handle(h), api(a), currentPhase(0.0) {}

    bool generateAudio(std::vector<float>& buffer, int frames) override {
        EngineSimStats stats = {};
        api.GetStats(handle, &stats);

        double frequency = (stats.currentRPM / 600.0) * 100.0;

        for (int i = 0; i < frames; i++) {
            double phaseIncrement = (2.0 * M_PI * frequency) / AudioLoopConfig::SAMPLE_RATE;
            currentPhase += phaseIncrement;

            float sample = static_cast<float>(std::sin(currentPhase) * 0.9);
            buffer[i * 2] = sample;
            buffer[i * 2 + 1] = sample;
        }

        return true;
    }

    void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle) override {
        if (interactive) {
            double frequency = (stats.currentRPM / 600.0) * 100.0;
            std::cout << "\r[" << std::fixed << std::setprecision(0) << std::setw(4) << stats.currentRPM << " RPM] ";
            std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(throttle * 100) << "%] ";
            std::cout << "[Frequency: " << std::setw(4) << static_cast<int>(frequency) << " Hz] ";
            std::cout << std::flush;
        } else {
            static int lastProgress = 0;
            int progress = static_cast<int>(currentTime * 100 / duration);
            if (progress != lastProgress && progress % 10 == 0) {
                double frequency = (stats.currentRPM / 600.0) * 100.0;
                std::cout << "  Progress: " << progress << "% | RPM: " << static_cast<int>(stats.currentRPM)
                          << " | Frequency: " << static_cast<int>(frequency) << " Hz\r" << std::flush;
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

    bool generateAudio(std::vector<float>& buffer, int frames) override {
        // Read from synthesizer with retry
        int totalRead = 0;

        api.ReadAudioBuffer(handle, buffer.data(), frames, &totalRead);

        if (totalRead < frames) {
            std::this_thread::sleep_for(std::chrono::microseconds(500));
            int additionalRead = 0;
            api.ReadAudioBuffer(handle, buffer.data() + totalRead * 2, frames - totalRead, &additionalRead);
            if (additionalRead > 0) {
                totalRead += additionalRead;
            }
        }

        return totalRead > 0;
    }

    void displayProgress(double currentTime, double duration, bool interactive, const EngineSimStats& stats, double throttle) override {
        if (interactive) {
            std::cout << "\r[" << std::fixed << std::setprecision(0) << std::setw(4) << stats.currentRPM << " RPM] ";
            std::cout << "[Throttle: " << std::setw(3) << static_cast<int>(throttle * 100) << "%] ";
            std::cout << "[Flow: " << std::setprecision(2) << stats.exhaustFlow << " m3/s] ";
            std::cout << std::flush;
        } else {
            static int lastProgress = 0;
            int progress = static_cast<int>(currentTime * 100 / duration);
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% (" << static_cast<int>(currentTime * AudioLoopConfig::SAMPLE_RATE) << " frames)\r" << std::flush;
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
    AudioPlayer* audioPlayer)
{
    double currentTime = 0.0;
    TimingOps::LoopTimer timer;

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

    // Main loop
    std::cout << "\nStarting main loop...\n";

    while ((!args.interactive && currentTime < args.duration) || (args.interactive && g_running.load())) {
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
                        // Toggle ignition - only on initial press, not repeat
                        {
                            static bool ignitionState = true;  // Start enabled by default
                            ignitionState = !ignitionState;
                            api.SetIgnition(handle, ignitionState ? 1 : 0);
                            std::cout << "Ignition " << (ignitionState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 's':
                        // Toggle starter motor - only on initial press, not repeat
                        {
                            static bool starterState = false;
                            starterState = !starterState;
                            api.SetStarterMotor(handle, starterState ? 1 : 0);
                            std::cout << "Starter motor " << (starterState ? "enabled" : "disabled") << "\n";
                        }
                        break;
                    case 65:  // UP arrow (macOS) - conflicts with 'A', but arrow takes precedence
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case 66:  // DOWN arrow (macOS)
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case 'k': case 'K':  // Alternative UP key
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        baselineLoad = interactiveLoad;
                        break;
                    case 'j': case 'J':  // Alternative DOWN key
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

        // Update engine
        api.SetThrottle(handle, throttle);
        api.Update(handle, AudioLoopConfig::UPDATE_INTERVAL);

        // Get current stats after Update for current iteration values
        EngineSimStats stats = {};
        api.GetStats(handle, &stats);

        // Generate audio (ONLY DIFFERENCE between modes)
        if (audioPlayer) {
            // Use cursor-chasing to determine how many frames to write
            // This maintains 100ms lead and prevents buffer underruns/overruns
            int framesToWrite = audioPlayer->calculateCursorChasingSamples(AudioLoopConfig::FRAMES_PER_UPDATE);

            if (framesToWrite > 0) {
                std::vector<float> audioBuffer(framesToWrite * 2);
                if (audioSource.generateAudio(audioBuffer, framesToWrite)) {
                    audioPlayer->addToCircularBuffer(audioBuffer.data(), framesToWrite);
                }
            }
        }

        currentTime += AudioLoopConfig::UPDATE_INTERVAL;

        // Display progress - pass throttle for display
        audioSource.displayProgress(currentTime, args.duration, args.interactive, stats, throttle);

        // 60Hz timing control
        timer.sleepToMaintain60Hz();
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
    config.audioBufferSize = 96000;
    config.simulationFrequency = 10000;
    config.fluidSimulationSteps = 8;
    config.targetSynthesizerLatency = 0.02;
    config.volume = 1.0f;
    config.convolutionLevel = 0.5f;
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
    std::cout << "[Configuration loaded: " << configPath << "]\n";

    // Start audio thread
    result = g_engineAPI.StartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        std::cerr << "ERROR: Failed to start audio thread\n";
        g_engineAPI.Destroy(handle);
        return 1;
    }
    std::cout << "[Audio thread started]\n";

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

        // Pre-fill buffer (common for both modes)
        BufferOps::preFillCircularBuffer(audioPlayer);
        audioPlayer->start();
        std::cout << "[Audio playback enabled]\n";
    }

    // Warmup (common for both modes)
    WarmupOps::runWarmup(handle, g_engineAPI, audioPlayer, args.playAudio);

    // Reset buffer after warmup (common for both modes)
    if (audioPlayer) {
        BufferOps::resetAndRePrefillBuffer(audioPlayer);
    }

    // Create appropriate audio source - THE ONLY DIFFERENCE
    std::unique_ptr<IAudioSource> audioSource;
    if (args.sineMode) {
        std::cout << "Mode: SINE TEST\n";
        audioSource = std::make_unique<SineAudioSource>(handle, g_engineAPI);
    } else {
        std::cout << "Mode: REAL ENGINE\n";
        audioSource = std::make_unique<EngineAudioSource>(handle, g_engineAPI);
    }

    // Run unified loop - SAME CODE FOR BOTH MODES
    int exitCode = runUnifiedAudioLoop(handle, g_engineAPI, *audioSource, args, audioPlayer);

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
