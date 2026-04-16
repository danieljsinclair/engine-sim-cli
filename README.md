# engine-sim-cli

Command-line interface for [engine-sim](https://github.com/danieljsinclair/engine-sim) audio generation.

This tool allows you to generate high-quality engine audio from command line with real-time playback, WAV export, and professional-grade audio quality.

## Features

- ✅ **Real-time Audio Playback** - Hear engine sounds as they're generated (macOS)
- ✅ **WAV Export** - Render high-quality engine audio to WAV files
- ✅ **Sine Wave Test Mode** - Test audio pipeline with clean sine waves
- ✅ **Engine Configuration Support** - Load any .mr engine configuration file
- ✅ **Real-time Statistics** - Monitor RPM, load, exhaust flow, etc.
- ✅ **Crackle-Free Audio** - Professional-quality audio output
- ✅ **Audio Strategies** - Choose between sync-pull (on-demand) or threaded (buffered) rendering

## Quick Start

### Clone, Build, Run

```bash
# Clone the repository (includes engine-sim-bridge submodule)
git clone --recursive https://github.com/danieljsinclair/engine-sim-cli.git
cd engine-sim-cli

# Build the application
make

# Run with default engine (real-time playback)
./build/engine-sim-cli --default-engine --rpm 2000 --play

# Run with sine wave test
./build/engine-sim-cli --sine --rpm 2000 --play

# Export audio to WAV file
./build/engine-sim-cli --default-engine --rpm 3000 --output engine.wav --duration 5
```

### Prerequisites

- **CMake** 3.20 or higher
- **C++17** compatible compiler
- **macOS:** AudioUnit framework (built-in)
- **Linux:** OpenAL (libopenal-dev)
- **Threads:** pthreads (usually included with compiler)

### macOS Installation

```bash
# Install build tools
brew install cmake

# Install audio libraries (for Linux)
sudo apt-get install libopenal-dev cmake build-essential
```

## Usage

### Real-time Playback (macOS)

```bash
# Play default engine at 2000 RPM
./build/engine-sim-cli --default-engine --rpm 2000 --play --duration 10

# Play sine wave test at 2000 RPM
./build/engine-sim-cli --sine --rpm 2000 --play --duration 10

# Use custom engine configuration
./build/engine-sim-cli --config path/to/engine.mr --rpm 3000 --play --duration 5

# Interactive mode with keyboard control
./build/engine-sim-cli --default-engine --interactive --play
```

### WAV Export (All platforms)

```bash
# Export to WAV file
./build/engine-sim-cli --default-engine --rpm 2000 --output engine.wav --duration 5

# Export with custom config
./build/engine-sim-cli --config assets/engines/chevrolet/engine_03_for_e1.mr --rpm 3000 --output my_engine.wav --duration 10
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
| `--interactive` | Enable keyboard control | - |
| `--silent` | Run full audio pipeline at zero volume | - |
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

## How It Works

The CLI tool provides a clean command-line interface to the engine-sim audio generation library:

1. **Initialization** - Set up simulator with appropriate audio parameters
2. **Configuration** - Load engine .mr file or use default
3. **Audio Pipeline** - Process engine simulation through audio synthesizer
4. **Output** - Either stream to audio hardware (macOS AudioUnit) or write to WAV file

### Audio Strategies

**threaded (default):** Pre-fills audio buffer, runs physics in separate thread
- ✅ Reliable, GUI model
- ✅ May work better on some platforms

**sync-pull:** Renders on-demand in audio callback
- ✅ Simpler, synchronous
- ✅ Default

## Engine Configurations

This tool uses the same .mr format as the main engine-sim project. You can find example configurations in [engine-sim repository](https://github.com/danieljsinclair/engine-sim).

### V8 Engines

See [V8_ENGINE_GUIDE.md](V8_ENGINE_GUIDE.md) for detailed instructions on running V8 engine configurations like Ferrari F136 and GM LS.

## Dependencies

The CLI tool depends on the [engine-sim-bridge](https://github.com/danieljsinclair/engine-sim-bridge) submodule which wraps the core engine-sim library:

- **engine-sim** - Core engine simulation library
- **macOS:** AudioUnit framework (built-in)
- **Linux:** OpenAL (libopenal-dev)

The bridge submodule handles all audio rendering and engine simulation logic. The CLI simply provides configuration, presentation, and user interaction layers.

## License

See LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.

## Technical Notes

- Audio on macOS uses AudioUnit for real-time playback
- Linux uses OpenAL for audio output
- WAV export works on all platforms
- The audio system has been thoroughly tested and optimized
- See [V8_ENGINE_GUIDE.md](V8_ENGINE_GUIDE.md) for V8 engine configuration details
