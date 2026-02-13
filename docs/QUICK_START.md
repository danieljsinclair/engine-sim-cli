# Quick Start Guide for Engine-Sim-CLI Verification System

This guide will help you quickly set up and run the verification system for the engine-sim-cli project.

## Prerequisites

1. **Engine-Sim-CLI Build**: Ensure you have a compiled `engine-sim-cli` executable
2. **Xcode Command Line Tools**: Required for building verification tools
3. **Git**: For version control (if using CI workflows)

## Quick Setup

### 1. Build the Engine-Sim-CLI
```bash
mkdir -p build
cd build
cmake ..
make -j$(sysctl -n hw.ncpu)
cd ..
```

### 2. Build Verification Tools
```bash
# Enter verification tools directory
cd verification_tools

# Build all verification tools
make all

# Make scripts executable
make scripts
```

### 3. Run Basic Tests
```bash
# Quick test (critical tests only)
make test-quick

# Full test suite
make test
```

## Common Verification Commands

### Quick Verification
```bash
# Test basic functionality
./verification_system.sh --mode binary --engine-sim-cli ../engine-sim-cli
./verification_system.sh --mode config --engine-sim-cli ../engine-sim-cli
./verification_system.sh --mode engine --engine-sim-cli ../engine-sim-cli --duration 5
```

### Component-Specific Tests
```bash
# Test audio output
./verification_system.sh --mode audio --engine-sim-cli ../engine-sim-cli --test-rpm 2000

# Test input response
./verification_system.sh --mode input --engine-sim-cli ../engine-sim-cli --duration 10

# Test engine startup
./verification_system.sh --mode engine --engine-sim-cli ../engine-sim-cli --test-rpm 3000
```

### CI-Friendly Commands
```bash
# Quick CI run
./ci_verification.sh --quick

# Full CI run with parallel tests
./ci_verification.sh --parallel --engine-sim-cli ../engine-sim-cli
```

## Understanding Test Results

### Pass/Fail Indicators
- **Green**: Test passed ✅
- **Red**: Test failed ❌
- **Yellow**: Warning or partial failure ⚠️

### Common Test Results

1. **Binary Verification**:
   - Checks if executable exists and has proper permissions
   - Verifies library dependencies

2. **Configuration Verification**:
   - Validates engine configuration files
   - Checks for required sound assets

3. **Engine Startup Test**:
   - Measures startup time
   - Verifies RPM stabilization
   - Checks for hangs or crashes

4. **Audio Output Test**:
   - Verifies frequency accuracy
   - Checks amplitude levels
   - Detects clipping or distortion

5. **Input Response Test**:
   - Measures keypress response time
   - Verifies all controls work
   - Checks for hangs

## Troubleshooting

### Common Issues

1. **"engine-sim-cli not found"**
   ```bash
   # Make sure you're in the correct directory
   ls -la engine-sim-cli

   # Or specify the correct path
   ./verification_system.sh --engine-sim-cli /path/to/engine-sim-cli
   ```

2. **"Verification tools not found"**
   ```bash
   # Build verification tools first
   cd verification_tools
   make all
   ```

3. **"Test timeout"**
   ```bash
   # Increase timeout
   ./verification_system.sh --timeout 60 --engine-sim-cli ../engine-sim-cli
   ```

4. **"Missing libraries"**
   ```bash
   # On macOS, ensure Xcode command line tools are installed
   xcode-select --install
   ```

### Debug Mode
Enable verbose output to see detailed information:
```bash
./verification_system.sh --verbose --engine-sim-cli ../engine-sim-cli
```

## Integration with Development Workflow

### After Each Build
```bash
# Quick check
make test-quick

# If quick tests pass, run full suite
make test
```

### Before Committing
```bash
# Run focused tests on your changes
./verification_system.sh --mode audio --engine-sim-cli ../engine-sim-cli
./verification_system.sh --mode engine --engine-sim-cli ../engine-sim-cli
```

### After Major Changes
```bash
# Full verification with detailed reporting
./verification_system.sh --engine-sim-cli ../engine-sim-cli --verbose --output-dir verification_results
```

## Continuous Integration

The verification system is set up to work with GitHub Actions. The CI workflow:

1. **Runs on**: Push, PR, and daily schedule
2. **Tests**:
   - Quick verification
   - Full verification
   - Audio-focused tests
   - Interactive-focused tests
3. **Artifacts**: Saves test results for review

## Getting Help

- **Documentation**: See `VERIFICATION_README.md` for detailed information
- **Configuration**: Check `test_cases.json` for test parameters
- **Issues**: Report bugs with error messages and test outputs

## Next Steps

1. Run the quick tests to verify everything works
2. Customize `test_cases.json` for your specific needs
3. Integrate verification commands into your build process
4. Set up CI integration for automated testing

---

*Remember: The verification system is designed to catch regressions early. Run tests regularly to ensure your changes don't break existing functionality.*