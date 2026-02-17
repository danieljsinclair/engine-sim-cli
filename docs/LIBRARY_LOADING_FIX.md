# Library Loading Fix Report

## Issue Summary
The CLI executable failed to load `libenginesim.dylib` with dlopen() due to incorrect path resolution. The error showed malformed paths like '/System/Volumes/Preboot/Cryptexes/OS/...' indicating the library wasn't found in the expected location.

## Investigation

### 1. Library Locations
- **Mock library**: `libenginesim-mock.dylib` was located at project root directory
- **Real library**: `libenginesim.dylib` was being built to `build/engine-sim-bridge/` directory
- **CLI executable**: Located at `build/engine-sim-cli`

### 2. Problem Root Cause
The CLI loader code expects libraries in the same directory as the executable, but:
- The post-build comment in CMakeLists.txt was incorrect - stating libraries were already in the right place
- No actual copy mechanism existed to move libraries from their build locations to the executable directory
- dlopen() was looking for `build/libenginesim.dylib` but the file was at `build/engine-sim-bridge/libenginesim.dylib`

### 3. Solution Implemented
Modified `CMakeLists.txt` post-build section to explicitly copy both libraries:

```cmake
# Post-build: Ensure dylib libraries are available in build directory
# The CLI uses dlopen() to load libenginesim.dylib and libenginesim-mock.dylib
# from the same directory as the executable
if(APPLE)
    add_custom_command(TARGET engine-sim-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Engine-sim CLI built successfully"
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_BINARY_DIR}/engine-sim-bridge/libenginesim.dylib ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E copy ${CMAKE_SOURCE_DIR}/libenginesim-mock.dylib ${CMAKE_BINARY_DIR}
        COMMAND ${CMAKE_COMMAND} -E echo "Dylib libraries copied to: ${CMAKE_BINARY_DIR}"
        COMMENT "Build complete - dylibs ready for runtime loading"
    )
endif()
```

## Testing Results

### Sine Mode (--sine)
- ✅ Library loads successfully: `[Library loaded: build/libenginesim-mock.dylib]`
- ✅ Audio playback works
- ✅ No underruns or crackles detected

### Real Engine Mode (--script)
- ✅ Library loads successfully: `[Library loaded: build/libenginesim.dylib]`
- ✅ Engine simulation runs with real synthesizer
- ✅ Audio playback works
- ✅ No underruns detected

## Key Insights
1. **CMake TARGET_FILE generator expression** can be tricky with symlinks - explicit paths were more reliable
2. **Build artifacts** need to be explicitly copied when using dlopen() instead of install rules
3. **Both library variants** (mock and real) need the same treatment
4. **Post-build commands** in CMake run after all targets are built, ensuring dependencies are available

## Files Changed
- `/Users/danielsinclair/vscode/engine-sim-cli/CMakeLists.txt` - Added explicit library copying in POST_BUILD command

## Impact
- CLI now works in both sine mode and real engine mode
- Library loading is now reliable across rebuilds
- No more path resolution errors
- Both modes can be tested for V8 performance and underruns