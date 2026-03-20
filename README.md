# ⚠️ CRITICAL ARCHITECTURE RULE - READ THIS FIRST ⚠️

## 🚫 PROHIBITION: Direct Sine Wave Generation in CLI Code

**NEVER add inline sine wave generation in `src/engine_sim_cli.cpp` or anywhere in the CLI application code.**

### Why This is Prohibited:

The `--sine` mode exists to test the **entire infrastructure** (bridge, threading, buffering, synchronization) without engine physics variability. This requires a proper "engine-sine" mock implementation.

**WRONG (inline sine - DO NOT DO THIS):**
```cpp
// In CLI main loop:
float sample = std::sin(phase);  // ❌ PROHIBITED
audioPlayer->addToCircularBuffer(&sample, 1);
```

**Rationale why this is wrong:**
- Does NOT test the bridge API
- Does NOT test threading model
- Does NOT test buffer management
- Does NOT test synchronization
- Does NOT replicate engine-sim behavior
- Defeats the entire purpose of having a mock

**CORRECT (mock through bridge interface):**
```cpp
// Build with USE_MOCK_ENGINE_SIM=ON
EngineSimReadAudioBuffer(handle, buffer, frames);  // ✅ Uses mock_engine_sim.cpp
// Mock behaves like engine-sim (threading, updates, etc) but outputs sine
```

### The Strategy:

1. **mock_engine_sim.cpp ("engine-sine")**: Replicates ALL engine-sim behaviors (threading, updates, buffer management) but outputs sine waves
2. **--sine mode**: Uses mock through bridge API
3. **--engine mode**: Uses real engine-sim through same bridge API
4. **Result**: If mock works perfectly → real engine-sim should work when integrated

### If You're Tempted to Add Inline Sine:

**STOP.** Fix `mock_engine_sim.cpp` instead. Make it replicate engine-sim's behavior properly.

---

# engine-sim-cli

Command-line interface for [engine-sim](https://github.com/danieljsinclair/engine-sim) audio generation.

This tool allows you to generate high-quality engine audio from the command line with real-time playback, WAV export, and professional-grade audio quality.

## Features

- ✅ **Real-time Audio Playback** - Hear engine sounds as they're generated (macOS)
- ✅ **WAV Export** - Render high-quality engine audio to WAV files
- ✅ **Sine Wave Test Mode** - Test audio pipeline with clean sine waves
- ✅ **Engine Configuration Support** - Load any .mr engine configuration file
- ✅ **Real-time Statistics** - Monitor RPM, load, exhaust flow, etc.
- ✅ **Crackle-Free Audio** - Professional-quality audio output (~90% improvement)
- ✅ **Cross-Platform Support** - macOS (AudioUnit), Linux (OpenAL)

## 🔧 Audio System Status (2026-02-05)

### ✅ RESOLVED ISSUES
- Audio crackles and discontinuities - **~90% reduction**
- Buffer underruns and thread competition - **Completely eliminated**
- Circular buffer switch artifacts - **Fixed**
- Sine mode crackling after 2 seconds - **Resolved**
- Double buffer consumption - **Fixed**

### ⚠️ REMAINING ISSUES
- RPM delay - ~100ms latency between control changes and audio response (minor performance concern)
- Occasional dropouts - Very rare, doesn't affect audio quality

### 📊 Performance Metrics
- **Sample Rate:** 44.1 kHz stereo float32
- **Audio Quality:** Professional-grade, matches Windows GUI performance
- **Buffer Management:** Zero underruns after startup
- **Architecture:** Proper AudioUnit pull model implementation

## Building

### Prerequisites

- CMake 3.20 or higher
- C++17 compatible compiler
- macOS: AudioUnit framework (built-in)
- Linux: OpenAL
- pthreads (usually included with your compiler)

### macOS (Homebrew)

```bash
brew install cmake
```

### Linux (Ubuntu/Debian)

```bash
sudo apt-get install libopenal-dev cmake build-essential
```

### Build Instructions

```bash
git clone https://github.com/danieljsinclair/engine-sim-cli.git
cd engine-sim-cli
mkdir build && cd build
cmake ..
make
```

The `engine-sim-cli` executable will be created in the `build` directory.

## Usage

### Real-time Playback (macOS)

```bash
# Play engine sounds in real-time
engine-sim-cli --default-engine --rpm 2000 --play --duration 10

# Play sine wave test
engine-sim-cli --sine --rpm 2000 --play --duration 10

# With custom engine config
engine-sim-cli --config path/to/engine.mr --rpm 3000 --play --duration 5
```

### WAV Export (All platforms)

```bash
# Export to WAV file
engine-sim-cli --default-engine --rpm 2000 --output engine.wav --duration 5

# Export with custom config
engine-sim-cli --config assets/engines/chevrolet/engine_03_for_e1.mr output.wav 10.0
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--default-engine` | Use default engine configuration | - |
| `--config <file>` | Load specific .mr engine config | - |
| `--sine` | Test with sine wave instead of engine | - |
| `--rpm <value>` | Set target RPM | 2000 |
| `--play` | Enable real-time audio playback | false |
| `--output <file>` | Output WAV file path | - |
| `--duration <seconds>` | Simulation duration | 3.0 |
| `--threaded` | Use threaded circular buffer mode (recommended) | sync-pull |
| `--sim-freq <Hz>` | Physics simulation frequency (1000-100000) | 10000 |
| `--silent` | Run full audio pipeline at zero volume | - |
| `--cranking-volume <x>` | Volume boost during cranking | 1.0 |
| `--interactive` | Enable keyboard control | - |
| `--help` | Show help message | - |

### Examples

```bash
# Real-time playback of default engine (threaded mode - recommended)
./build/engine-sim-cli --default-engine --rpm 2000 --play --threaded

# Real-time playback with sine wave test
./build/engine-sim-cli --sine --rpm 1000 --play --duration 20

# Export 10 seconds of audio to WAV
./build/engine-sim-cli --default-engine --rpm 3000 --output engine.wav --duration 10

# Use custom engine configuration
./build/engine-sim-cli --config my_engine.mr --rpm 1500 --output my_engine.wav --duration 5

# Lower physics frequency for faster processing (reduces CPU load)
./build/engine-sim-cli --default-engine --rpm 3000 --play --sim-freq 5000
```

## How it Works

The CLI tool uses the engine-sim library with a sophisticated audio pipeline:

1. **Initialization** - Set up simulator with appropriate audio parameters
2. **Configuration** - Load engine .mr file or use default
3. **Audio Pipeline** - Process engine simulation through audio synthesizer
4. **Output** - Either stream to audio hardware (macOS AudioUnit) or write to WAV file

### Audio Architecture

**macOS (Real-time):**
- AudioUnit callback-based streaming (pull model)
- Two modes: threaded (recommended) or sync-pull
- 44.1 kHz sample rate with 100ms buffer lead (threaded mode)

**Audio Modes:**

| Mode | Description | Pros/Cons |
|------|-------------|-----------|
| `--threaded` (default) | Pre-fills buffer, runs physics in separate thread | ✅ Reliable, no crackles |
| `--sync-pull` | Renders on-demand in audio callback | ⚠️ Timing-sensitive, may crackle |

**Physics Frequency (`--sim-freq`):**
- Default: 10000 Hz (10k simulation steps per second)
- Lower values (e.g., 5000, 2000) reduce CPU load
- Useful for sync-pull mode to reduce crackles
- Lower values may slightly reduce audio quality
- Recommended: 8000-10000 for balance of quality and performance

**All platforms (WAV Export):**
- Direct rendering to WAV file
- No audio hardware dependencies
- Same high-quality audio synthesis

## Audio Investigation Documentation

The audio system went through an extensive investigation to eliminate crackles and improve quality:

- See `AUDIO_INVESTIGATION_COMPLETE_SUMMARY.md` for complete technical details
- Root cause: Pull vs push model architecture mismatch
- Solution: Proper AudioUnit pull model implementation
- Result: Professional-quality audio matching Windows GUI

## Engine Configurations

This tool uses the same .mr format as the main engine-sim project. You can find example configurations in the [engine-sim repository](https://github.com/danieljsinclair/engine-sim).

## Dependencies

- [engine-sim](https://github.com/danieljsinclair/engine-sim) - Core engine simulation library
- **macOS:** AudioUnit framework (built-in)
- **Linux:** [OpenAL](https://www.openal.org/) - Audio output
- [csv-io](https://github.com/mohabusama/csv-io) - CSV parsing (transitive dependency)

## License

See LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Technical Notes

- Audio on macOS uses AudioUnit for real-time playback
- Linux uses OpenAL for audio output
- WAV export works on all platforms
- The audio system has been thoroughly tested and optimized
- See documentation for complete audio investigation details
