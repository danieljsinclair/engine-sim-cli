# Build Scripts

Helper scripts for development and CI.

## `bootstrap.sh`

One-command fresh-clone setup. Installs dependencies and builds.

```bash
./scripts/bootstrap.sh
```

**What it does:**
1. Validates macOS
2. Installs CMake, Bison, Flex via Homebrew
3. Runs `make`

## `test-build.sh`

Full build validation suite. Use after making changes or before submitting PRs.

```bash
./scripts/test-build.sh
```

**What it tests:**
- CMake configure succeeds without Boost
- Bison/Flex dependency warnings appear when tools missing
- Compiler flag compatibility (handles old/new Clang differences)
- Full build completes
- Binary runs and generates valid WAV output
- Default engine configuration loads correctly

The script exits with code 0 on success, non-zero on any failure. All output is logged to `build/cmake.log` and `build/build.log` for debugging.
