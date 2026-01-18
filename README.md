# engine-sim-cli

Command-line interface for [engine-sim](https://github.com/danieljsinclair/engine-sim) audio generation.

This tool allows you to generate engine audio from the command line by running the engine simulator and rendering the output to WAV files.

## Features

- Load engine configurations from .mr files
- Render high-quality engine audio to WAV format
- Configurable duration and sample rate
- Real-time statistics reporting (RPM, load, exhaust flow, etc.)
- Simple, intuitive command-line interface

## Building

### Prerequisites

- CMake 3.20 or higher
- C++17 compatible compiler
- OpenAL
- pthreads (usually included with your compiler)

### macOS (Homebrew)

```bash
brew install openal-soft cmake
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

```bash
engine-sim-cli <engine_config.mr> <output.wav> [duration_seconds]
```

### Arguments

- `engine_config.mr` - Path to the engine configuration file (.mr format)
- `output.wav` - Path where the WAV file will be written
- `duration_seconds` - (Optional) Duration in seconds (default: 3.0)

### Examples

```bash
# Generate 5 seconds of audio from an engine config
engine-sim-cli assets/engines/chevrolet/engine_03_for_e1.mr output.wav 5.0

# Generate 3 seconds of audio
engine-sim-cli assets/engines/atg-video-1/01_honda_trx520.mr honda.wav 3.0
```

### Notes

- Engine configs must use absolute paths or be relative to the current directory
- The output is a 32-bit float WAV file at 48kHz stereo
- The simulator automatically ramps up throttle smoothly over the first 0.5 seconds

## How it Works

The CLI tool uses the engine-sim library to:

1. Initialize the simulator with appropriate audio settings
2. Load an engine configuration from a .mr file
3. Start the audio processing thread
4. Simulate the engine physics and render audio in real-time
5. Write the rendered audio to a WAV file

## Engine Configurations

This tool uses the same .mr format as the main engine-sim project. You can find example configurations in the [engine-sim repository](https://github.com/danieljsinclair/engine-sim).

## Dependencies

- [engine-sim](https://github.com/danieljsinclair/engine-sim) - Core engine simulation library
- [OpenAL](https://www.openal.org/) - Audio output
- [csv-io](https://github.com/mohabusama/csv-io) - CSV parsing (transitive dependency)

## License

See LICENSE file for details.

## Contributing

Contributions are welcome! Please feel free to submit a Pull Request.
