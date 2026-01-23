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

#ifdef __APPLE__
#include <OpenAL/al.h>
#include <OpenAL/alc.h>
#else
#include <AL/al.h>
#include <AL/alc.h>
#endif

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
    uint32_t sampleRate = 48000;
    uint32_t byteRate = 48000 * 2 * 4;
    uint16_t blockAlign = 2 * 4;
    uint16_t bitsPerSample = 32;
    char dataChunkMarker[4] = {'d', 'a', 't', 'a'};
    uint32_t dataSize;
};

// ============================================================================
// OpenAL Audio Player
// ============================================================================

class AudioPlayer {
public:
    AudioPlayer() : device(nullptr), context(nullptr), source(0), buffers{0}, currentBuffer(0) {}

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

        // Convert float to int16 for OpenAL compatibility
        std::vector<int16_t> int16Data(frames * 2);
        for (int i = 0; i < frames * 2; ++i) {
            float sample = std::max(-1.0f, std::min(1.0f, data[i]));
            int16Data[i] = static_cast<int16_t>(sample * 32767.0f);
        }

        // Check how many buffers have been processed
        ALint processed;
        alGetSourcei(source, AL_BUFFERS_PROCESSED, &processed);

        // Unqueue processed buffers
        while (processed > 0) {
            ALuint buf;
            alSourceUnqueueBuffers(source, 1, &buf);
            processed--;
        }

        // Check how many buffers are currently queued
        ALint queued;
        alGetSourcei(source, AL_BUFFERS_QUEUED, &queued);

        // Only queue new buffer if we have space (max 2 buffers)
        if (queued < 2) {
            ALuint buffer = buffers[currentBuffer];
            alBufferData(buffer, AL_FORMAT_STEREO16, int16Data.data(), frames * 2 * sizeof(int16_t), sampleRate);

            ALenum error = alGetError();
            if (error != AL_NO_ERROR) {
                std::cerr << "OpenAL Error buffering data: " << error << "\n";
                return false;
            }

            alSourceQueueBuffers(source, 1, &buffer);

            error = alGetError();
            if (error != AL_NO_ERROR) {
                std::cerr << "OpenAL Error queuing buffers: " << error << "\n";
                return false;
            }

            currentBuffer = (currentBuffer + 1) % 2;
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

private:
    ALCdevice* device;
    ALCcontext* context;
    ALuint source;
    ALuint buffers[2];
    int currentBuffer;
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
    RPMController() : targetRPM(0), kp(2.0), integral(0), ki(0.2), lastError(0), firstUpdate(true) {}

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

        // Allow larger integral term for better control
        integral = std::max(-500.0, std::min(500.0, integral + error * dt));
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
};

void printUsage(const char* progName) {
    std::cout << "Engine Simulator CLI v2.0\n";
    std::cout << "Usage: " << progName << " [options] <engine_config.mr> <output.wav>\n";
    std::cout << "   OR: " << progName << " --script <engine_config.mr> [options] [output.wav]\n\n";
    std::cout << "Options:\n";
    std::cout << "  --script <path>      Path to engine .mr configuration file\n";
    std::cout << "  --rpm <value>        Target RPM to maintain (default: auto)\n";
    std::cout << "  --load <0-100>       Throttle load percentage (default: auto)\n";
    std::cout << "  --interactive        Enable interactive keyboard control\n";
    std::cout << "  --play, --play-audio Play audio to speakers in real-time\n";
    std::cout << "  --duration <seconds> Duration in seconds (default: 3.0)\n";
    std::cout << "  --default-engine     Use default engine from main repo (ignores config file)\n\n";
    std::cout << "Interactive Controls:\n";
    std::cout << "  A                      Toggle ignition on/off (starts ON)\n";
    std::cout << "  S                      Toggle starter motor on/off\n";
    std::cout << "  UP/DOWN Arrows or K/J  Increase/decrease throttle\n";
    std::cout << "  W                      Increase target RPM\n";
    std::cout << "  SPACE                  Apply brake\n";
    std::cout << "  R                      Reset to idle\n";
    std::cout << "  Q/ESC                  Quit\n\n";
    std::cout << "Examples:\n";
    std::cout << "  " << progName << " --script v8_engine.mr --rpm 850 --duration 5\n";
    std::cout << "  " << progName << " --script v8_engine.mr --interactive --play\n";
    std::cout << "  " << progName << " --script engine-sim/assets/main.mr --interactive\n";
    std::cout << "  " << progName << " --default-engine --rpm 2000 --play\n";
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

    // Engine config is required, but output WAV is optional (e.g., for interactive mode)
    if (args.engineConfig == nullptr) {
        std::cerr << "ERROR: Engine configuration file is required\n";
        std::cerr << "       Use --script <path> or provide positional argument\n\n";
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
    const int sampleRate = 48000;
    const int channels = 2;
    const double updateInterval = 1.0 / 60.0;
    const int framesPerUpdate = sampleRate / 60;

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
        std::filesystem::path esPath("engine-sim/es");
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
        std::filesystem::path defaultScriptPath("engine-sim/assets/main.mr");
        std::filesystem::path defaultAssetPath("engine-sim/es/sound-library");

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

    // Start audio thread first
    result = EngineSimStartAudioThread(handle);
    if (result != ESIM_SUCCESS) {
        std::cerr << "WARNING: Failed to start audio thread\n";
    }

    // Auto-enable ignition so the engine can run
    EngineSimSetIgnition(handle, 1);
    if (!args.interactive) {
        std::cout << "[3/5] Ignition enabled (auto)\n";
    } else {
        std::cout << "[3/5] Ignition enabled (ready for start) - Press 'S' for starter motor\n";
    }

    std::cout << "[4/5] Audio thread started\n";

    // Initialize audio player if requested
    AudioPlayer* audioPlayer = nullptr;
    if (args.playAudio) {
        audioPlayer = new AudioPlayer();
        if (!audioPlayer->initialize(sampleRate)) {
            std::cerr << "WARNING: Failed to initialize audio player, continuing without playback\n";
            delete audioPlayer;
            audioPlayer = nullptr;
        } else {
            std::cout << "[4/5] Audio player initialized\n";
        }
    } else {
        std::cout << "[4/5] Audio playback disabled\n";
    }

    // Setup recording buffer
    // In interactive mode without output, we only need a small buffer for real-time playback
    // In non-interactive mode or with output file, we need the full duration buffer
    const int totalFrames = (args.interactive && !args.outputWav) ?
        (sampleRate / 60) :  // Single update buffer for interactive-only mode
        (static_cast<int>(args.duration * sampleRate));
    const int totalSamples = totalFrames * channels;
    std::vector<float> audioBuffer(totalSamples);

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

    if (args.interactive) {
        keyboardInput = new KeyboardInput();
        std::cout << "\nInteractive mode enabled. Press Q to quit.\n\n";
        std::cout << "Interactive Controls:\n";
        std::cout << "  A - Toggle ignition (starts ON)\n";
        std::cout << "  S - Toggle starter motor\n";
        std::cout << "  W - Increase target RPM\n";
        std::cout << "  SPACE - Brake\n";
        std::cout << "  R - Reset to idle\n";
        std::cout << "  J/K or Down/Up - Decrease/Increase load\n";
        std::cout << "  Q/ESC - Quit\n\n";
    }

    // Main simulation loop
    double currentTime = 0.0;
    int framesRendered = 0;
    int lastProgress = 0;
    double throttle = 0.0;

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

            EngineSimSetThrottle(handle, warmupThrottle);
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
            if (currentTime < 1.0) {
                EngineSimSetThrottle(handle, 0.5);  // Medium throttle for initial start
            } else {
                EngineSimSetThrottle(handle, 0.7);  // Higher throttle for combustion development
            }
            currentTime += updateInterval;
        }
    }

    currentTime = 0.0;

    while ((!args.interactive && currentTime < args.duration && framesRendered < totalFrames) || (args.interactive && g_running.load())) {
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
                        interactiveTargetRPM += 100;
                        rpmController.setTargetRPM(interactiveTargetRPM);
                        break;
                    case ' ':
                        // Brake
                        interactiveLoad = 0.0;
                        break;
                    case 'r':
                    case 'R':
                        // Reset to idle
                        interactiveTargetRPM = 850;
                        interactiveLoad = 0.2;
                        rpmController.setTargetRPM(interactiveTargetRPM);
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
                        break;
                    case 66:  // DOWN arrow (macOS)
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        break;
                    case 'k':  // Alternative UP key
                    case 'K':
                        interactiveLoad = std::min(1.0, interactiveLoad + 0.05);
                        break;
                    case 'j':  // Alternative DOWN key
                    case 'J':
                        interactiveLoad = std::max(0.0, interactiveLoad - 0.05);
                        break;
                }
                lastKey = key;
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
                          << ", Throttle: " << std::fixed << std::setprecision(3) << throttle
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
        EngineSimSetThrottle(handle, throttle);
        EngineSimUpdate(handle, updateInterval);

        // Render audio
        int framesToRender = framesPerUpdate;

        // In non-interactive mode, check buffer limits
        if (!args.interactive) {
            framesToRender = std::min(framesPerUpdate, totalFrames - framesRendered);
        }

        if (framesToRender > 0) {
            int samplesWritten = 0;
            float* writePtr = audioBuffer.data();

            // For interactive mode without output, always write to start of buffer
            // For non-interactive mode or with output, append to buffer
            if (!args.interactive || args.outputWav) {
                writePtr = audioBuffer.data() + framesRendered * channels;
            }

            result = EngineSimRender(handle, writePtr, framesToRender, &samplesWritten);

            if (result == ESIM_SUCCESS && samplesWritten > 0) {
                // Only track frames if we're saving to file or in non-interactive mode
                if (!args.interactive || args.outputWav) {
                    framesRendered += samplesWritten;
                }

                // Play audio in real-time
                if (audioPlayer) {
                    audioPlayer->playBuffer(writePtr, samplesWritten, sampleRate);
                }
            }
        }

        currentTime += updateInterval;

        // Display progress
        if (args.interactive) {
            displayHUD(stats.currentRPM, throttle, interactiveTargetRPM, stats);
        } else {
            int progress = static_cast<int>(framesRendered * 100 / totalFrames);
            if (progress != lastProgress && progress % 10 == 0) {
                std::cout << "  Progress: " << progress << "% (" << framesRendered << " frames)\r" << std::flush;
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

        std::cout << "Done! Wrote " << framesRendered * channels << " samples\n";
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
    std::cout << "  Engine: " << args.engineConfig << "\n";
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
