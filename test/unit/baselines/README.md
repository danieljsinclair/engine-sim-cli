# Audio Regression Test Baselines

This directory contains baseline data files for audio regression tests.

## Purpose

These baseline files capture the exact output of the ThreadedRenderer implementation
before any refactoring. They ensure that any changes to the audio rendering code
produce identical output to the original implementation.

## Files

- `threaded_renderer_baseline.dat` - Normal rendering scenario
- `threaded_renderer_baseline_wrap.dat` - Wrap-around read scenario
- `threaded_renderer_baseline_underrun.dat` - Buffer underrun scenario

## Usage

The regression tests in `AudioRegressionTest.cpp` compare current renderer output
against these baseline files. If refactoring changes the output, the comparison
test will fail until the output matches exactly.

## Regenerating Baselines

If you need to regenerate the baseline files (e.g., after an intentional change
to audio output behavior), run:

```bash
cd build
./test/unit/unit_tests --gtest_filter="AudioRegression*.Capture*"
```

Then copy the generated `.dat` files from the build directory to this directory.

## Format

Baseline files are binary files containing:
- Metadata (frame count, buffer size, pointers, counters)
- Audio sample data (float arrays)

See `AudioRegressionTest.cpp` for the exact format.
