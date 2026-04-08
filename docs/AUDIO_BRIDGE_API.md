# Audio Bridge API Documentation

## Overview

The Audio Bridge API provides a clean, decoupled interface for audio rendering in the engine simulator CLI. The architecture separates audio strategy implementation from hardware abstraction, allowing for flexible audio rendering across different platforms.

## Architecture

### Core Components

#### 1. IAudioStrategy Interface
```cpp
class IAudioStrategy {
public:
    virtual ~IAudioStrategy() = default;

    // Core strategy methods
    virtual const char* getName() const = 0;
    virtual bool isEnabled() const = 0;
    virtual bool shouldDrainDuringWarmup() const = 0;

    // Audio rendering
    virtual bool render(StrategyContext* context, AudioBufferList* ioData, UInt32 numberFrames) = 0;
    virtual bool AddFrames(StrategyContext* context, float* buffer, int frameCount) = 0;

    // Diagnostics and info
    virtual std::string getDiagnostics() const = 0;
    virtual std::string getProgressDisplay() const = 0;

    // Configuration
    virtual void configure(const AudioStrategyConfig& config) = 0;
    virtual void reset() = 0;
    virtual std::string getModeString() const = 0;
};
```

**Responsibilities:**
- Implements audio rendering logic for a specific strategy
- Manages audio generation and buffer handling
- Provides diagnostic information
- Handles strategy-specific configuration

**Implementations:**
- `SyncPullStrategy`: Lock-step audio generation with on-demand rendering
- `ThreadedStrategy`: Cursor-chasing mode with separate audio thread

#### 2. IAudioHardwareProvider Interface
```cpp
class IAudioHardwareProvider {
public:
    virtual ~IAudioHardwareProvider() = default;

    virtual bool initialize(int sampleRate) = 0;
    virtual void cleanup() = 0;
    virtual bool startPlayback() = 0;
    virtual void stopPlayback() = 0;
    virtual bool isPlaying() const = 0;
    virtual void setVolume(float volume) = 0;

    // Hardware-specific implementations
    virtual AudioUnitContext* createAudioContext() = 0;
    virtual void destroyAudioContext(AudioUnitContext* context) = 0;
};
```

**Responsibilities:**
- Abstracts platform-specific audio hardware
- Manages AudioUnit lifecycle (macOS)
- Provides volume control
- Handles audio context creation/destruction

**Implementations:**
- `CoreAudioHardwareProvider`: macOS implementation using CoreAudio

#### 3. StrategyContext Struct
```cpp
struct StrategyContext {
    AudioState audioState;              // Playback state
    BufferState bufferState;            // Buffer management
    Diagnostics diagnostics;            // Performance metrics
    CircularBuffer* circularBuffer;     // Audio data buffer
    IAudioStrategy* strategy;           // Current strategy
    EngineSimHandle engineHandle;       // Engine simulator
    const EngineSimAPI* engineAPI;     // Engine API
};
```

**Responsibilities:**
- Composes all state needed by audio strategies
- Provides unified context for strategy implementations
- Enables dependency injection

#### 4. StrategyAdapter (Bridge Pattern)
```cpp
class StrategyAdapter : public IAudioRenderer {
public:
    StrategyAdapter(std::unique_ptr<IAudioStrategy> strategy,
                   std::unique_ptr<StrategyContext> context);

    // Bridge IAudioRenderer to IAudioStrategy
    bool render(AudioUnitContext* context, AudioBufferList* ioData, UInt32 numberFrames) override;
    bool AddFrames(void* context, float* buffer, int frameCount) override;
    // ... other methods
};
```

**Responsibilities:**
- Bridges old IAudioRenderer interface to new IAudioStrategy
- Enables gradual migration
- Maintains compatibility with existing code

## Usage Pattern

### 1. Creating Audio Strategy
```cpp
// Via factory
auto strategy = IAudioStrategyFactory::createStrategy(AudioMode::SyncPull, logger);

// Direct instantiation
auto strategy = std::make_unique<SyncPullStrategy>(logger);
```

### 2. Hardware Provider
```cpp
// Create hardware provider
auto hardwareProvider = std::make_unique<CoreAudioHardwareProvider>();

// Initialize
hardwareProvider->initialize(sampleRate);

// Create audio context
auto audioContext = hardwareProvider->createAudioContext();
```

### 3. Strategy Configuration
```cpp
// Configure strategy
AudioStrategyConfig config;
config.sampleRate = sampleRate;
config.channels = 2;  // Stereo
strategy->configure(config);

// Setup strategy context
StrategyContext context;
context.engineHandle = engineHandle;
context.engineAPI = &engineAPI;
context.circularBuffer = audioContext->circularBuffer.get();
```

### 4. Audio Rendering
```cpp
// In audio callback
strategy->render(&context, audioBuffer, numFrames);

// For threaded mode - add frames to buffer
strategy->AddFrames(&context, audioData, numFrames);
```

## Audio Modes

### Sync-Pull Mode (Default)
- **Strategy**: `SyncPullStrategy`
- **Behavior**: Lock-step audio generation
- **Audio Thread**: Renders directly in audio callback
- **Buffering**: No circular buffer needed
- **Use Case**: Precision timing, minimal latency
- **Pros**: Perfect synchronization, no underruns
- **Cons**: Higher CPU usage during audio callbacks

### Threaded Mode (--threaded)
- **Strategy**: `ThreadedStrategy`
- **Behavior**: Cursor-chasing with circular buffer
- **Audio Thread**: Separate generation thread
- **Buffering**: 2-second circular buffer
- **Use Case**: Lower CPU spikes, smooth playback
- **Pros**: Even CPU load, underrun protection
- **Cons**: Slight latency potential

## Factory Pattern

The `IAudioStrategyFactory` provides centralized strategy creation:

```cpp
class IAudioStrategyFactory {
public:
    static std::unique_ptr<IAudioStrategy> createStrategy(
        AudioMode mode,
        ILogging* logger = nullptr
    );

private:
    static std::unique_ptr<IAudioStrategy> createThreadedStrategy(ILogging* logger);
    static std::unique_ptr<IAudioStrategy> createSyncPullStrategy(ILogging* logger);
};
```

## Migration Path

### Phase 1: Current State
- New IAudioStrategy implemented
- Old IAudioRenderer still used in CLI
- StrategyAdapter bridges the gap

### Phase 2: Gradual Migration
- Directly use IAudioStrategy in CLI
- StrategyAdapter removed
- IAudioRenderer deprecated

### Phase 3: Final State
- Complete migration to IAudioStrategy
- IAudioRenderer removed
- Clean, unified audio architecture

## Platform Support

### macOS
- Implementation: `CoreAudioHardwareProvider`
- Uses CoreAudio AudioUnit
- Hardware volume control
- Sample rate conversion support

### iOS (Placeholder)
- Implementation: `AudioUnitHardwareProvider` (planned)
- CoreAudio on iOS
- Session management

### ESP32 (Placeholder)
- Implementation: `ESP32AudioProvider` (planned)
- ESP-IDF audio interface
- Memory-efficient buffering

## Testing

### Unit Tests
- `IAudioStrategyTest`: Tests all strategy implementations
- `HardwareProviderTest`: Tests hardware abstraction
- `StrategyAdapterTest`: Tests bridge functionality

### Integration Tests
- `IntegrationAudioPlayerTest`: End-to-end audio pipeline
- `SyncPullRendererTest`: Sync-pull mode verification
- `ThreadedRendererTest`: Threaded mode verification

## Performance Considerations

### Sync-Pull Mode
- **Latency**: Minimal (callback level)
- **CPU Usage**: Spikes during rendering
- **Memory**: Low (no buffer needed)
- **Precision**: Perfect timing

### Threaded Mode
- **Latency**: ~100ms (buffer lead time)
- **CPU Usage**: Even distribution
- **Memory**: ~90MB (2-second buffer at 48kHz)
- **Precision**: Good with cursor chasing

## Configuration

### Strategy Selection
```bash
# Default (sync-pull)
./engine-sim-cli --script engine.mr

# Threaded mode
./engine-sim-cli --script engine.mr --threaded
```

### Hardware Configuration
```bash
# Sample rate
./engine-sim-cli --script engine.mr --sim-freq 22050

# Silent mode (full pipeline, no output)
./engine-sim-cli --script engine.mr --silent
```

## Error Handling

### Strategy Errors
- Invalid context: Check null pointers
- Engine API: Verify handle and API
- Buffer underruns: Monitor diagnostics
- Render failures: Check engine status

### Hardware Errors
- Initialization failure: Check sample rate
- Device unavailable: Handle gracefully
- Volume control: Range validation

## Future Enhancements

1. **Platform Support**
   - Windows WASAPI implementation
   - Linux PulseAudio implementation
   - Android OpenSL ES implementation

2. **Advanced Features**
   - Sample rate conversion
   - Channel routing (mono/stereo/5.1)
   - Real-time effects processing

3. **Performance**
   - Asynchronous audio loading
   - Adaptive buffering
   - CPU load monitoring

## Dependencies

### Internal
- `engine_sim_bridge`: Engine simulator API
- `CircularBuffer`: Audio data buffering
- `ILogging`: Logging interface
- `ConsoleLogger`: Implementation

### External
- CoreAudio (macOS/iOS)
- ESP-IDF audio (ESP32)
- Boost (for utilities)

## Design Principles

1. **SOLID Principles**
   - Single Responsibility: Each class has one clear purpose
   - Open/Closed: Extensible without modifying existing code
   - Liskov Substitution: Strategies can be interchanged
   - Interface Segregation: Minimal, focused interfaces
   - Dependency Inversion: High-level modules depend on abstractions

2. **Design Patterns**
   - Strategy Pattern: Different audio rendering strategies
   - Factory Pattern: Centralized object creation
   - Adapter Pattern: Bridge between old and new interfaces
   - Bridge Pattern: Decouple implementation from abstraction

3. **Performance**
   - Zero-copy audio where possible
   - Lock-free operations where safe
   - Efficient memory usage
   - Minimal thread synchronization