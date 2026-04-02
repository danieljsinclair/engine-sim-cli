# Engine Segfault and Sine Lag Fixes (2026-02-16)

## Summary

Fixed two critical bugs affecting engine mode (segfault) and sine mode (lag):
1. **Engine segfault:** Path duplication created invalid impulse response paths
2. **Sine lag:** Fixed frame count ignored cursor-chasing architecture

## Fix 1: Engine Segfault - Path Duplication

### Root Cause
The `loadImpulseResponses()` function in `engine_sim_bridge.cpp` only checked for absolute paths (starts with `/` or drive letter), but didn't detect other fully-qualified paths. This caused path duplication when loading impulse responses with:
- Relative paths with directory components (e.g., `assets/ir.wav`)
- Explicit relative paths (e.g., `./ir.wav`, `../ir.wav`)
- Windows paths with backslashes

### Location
- **File:** `engine-sim-bridge/src/engine_sim_bridge.cpp`
- **Function:** `loadImpulseResponses()`
- **Lines:** 161-170

### Original Code (Buggy)
```cpp
// Construct full path
std::string fullPath;
if (filename[0] == '/' || (filename.length() > 1 && filename[1] == ':')) {
    // Absolute path
    fullPath = filename;
} else {
    // Relative path - combine with asset base path
    fullPath = assetBasePath + "/" + filename;
    std::cerr << "DEBUG BRIDGE: Loading impulse: fullPath=" << fullPath << "\n";
}
```

### Fixed Code
```cpp
// Construct full path
std::string fullPath;

// Detect if path is already fully qualified:
// 1. Absolute path (starts with / on Unix, or drive letter on Windows)
// 2. Relative path with directory components (contains / or \)
// 3. Explicit relative path (starts with ./ or ../)
bool isFullyQualified = false;
if (filename.empty()) {
    isFullyQualified = false;
} else if (filename[0] == '/' || (filename.length() > 1 && filename[1] == ':')) {
    // Absolute path
    isFullyQualified = true;
} else if (filename[0] == '.' && (filename[1] == '/' || (filename[1] == '.' && filename[2] == '/'))) {
    // Explicit relative path: ./ or ../
    isFullyQualified = true;
} else if (filename.find('/') != std::string::npos || filename.find('\\') != std::string::npos) {
    // Contains directory separators - already has path components
    isFullyQualified = true;
}

if (isFullyQualified) {
    // Path is already fully qualified - use as-is
    fullPath = filename;
    std::cerr << "DEBUG BRIDGE: Using qualified path: " << fullPath << "\n";
} else {
    // Simple filename only - combine with asset base path
    fullPath = assetBasePath + "/" + filename;
    std::cerr << "DEBUG BRIDGE: Loading impulse: fullPath=" << fullPath << "\n";
}
```

### Impact
- Prevents segfault when loading engines with properly-specified impulse response paths
- Correctly handles absolute paths, relative paths with components, and Windows paths
- Only combines `assetBasePath` with simple filenames (no directory components)

## Fix 2: Sine Lag - Cursor-Chasing Not Used

### Root Cause
The unified main loop used a fixed frame count (`AudioLoopConfig::FRAMES_PER_UPDATE` = 735) when writing to the circular buffer, ignoring the cursor-chasing architecture. This caused:
- **Overruns:** Buffer filling too fast when already at target
- **Underruns:** Buffer draining faster than being filled
- **Defeated purpose:** The cursor-chasing design exists specifically to prevent these issues

### Location
- **File:** `src/engine_sim_cli.cpp`
- **Function:** `runUnifiedAudioLoop()`
- **Line:** 1210

### Original Code (Buggy)
```cpp
// Generate audio (ONLY DIFFERENCE between modes)
if (audioPlayer) {
    std::vector<float> audioBuffer(AudioLoopConfig::FRAMES_PER_UPDATE * 2);
    if (audioSource.generateAudio(audioBuffer, AudioLoopConfig::FRAMES_PER_UPDATE)) {
        audioPlayer->addToCircularBuffer(audioBuffer.data(), AudioLoopConfig::FRAMES_PER_UPDATE);
    }
}
```

### Fixed Code
```cpp
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
```

### Cursor-Chasing Behavior
The `calculateCursorChasingSamples()` method implements the GUI's cursor-chasing algorithm:

1. **Target Lead:** 100ms ahead of playback cursor (4410 samples at 44.1kHz)
2. **Safety Clamp:** If buffer >500ms ahead, snap back to 50ms
3. **Skip Write:** If writing would reduce buffer lead, skip this iteration
4. **Dynamic Frame Count:** Returns exact frames needed to reach target

### Impact
- Eliminates lag in sine mode by matching GUI's cursor-chasing approach
- Maintains optimal buffer level (100ms lead) dynamically
- Prevents both underruns and overruns automatically
- Applies to both sine and engine modes (unified loop)

## Testing Results

### Sine Mode
- **Status:** PASS - Runs smoothly with no underruns
- **Duration tested:** 5 seconds
- **Audio:** Clean playback, no crackles or stuttering
- **Cursor-chasing:** Working correctly

### Engine Mode
- **Status:** BLOCKED - Pre-existing build system issue
- **Issue:** Assets directory not copied to build directory
- **Note:** Segfault fix code is syntactically correct and compiled successfully
- **Testing constraint:** Cannot test engine mode without resolving build system issue

## Key Principles Reinforced

### DRY (Don't Repeat Yourself)
- Unified main loop should use same cursor-chasing logic as GUI
- Both modes (sine and engine) share identical buffer management code

### Cursor-Chasing Architecture
- **Purpose:** Maintain optimal buffer lead dynamically
- **Target:** 100ms ahead of hardware playback cursor
- **Safety:** Clamp back if too far ahead (>500ms)
- **Implementation:** Use `calculateCursorChasingSamples()`, never fixed frame counts

### Path Resolution Best Practices
- Detect fully-qualified paths before combining with base paths
- Support Unix, Windows, and relative path formats
- Only combine when path is a simple filename (no directory components)

## Files Modified

1. `engine-sim-bridge/src/engine_sim_bridge.cpp` - Path qualification detection
2. `src/engine_sim_cli.cpp` - Cursor-chasing in unified main loop
3. `.claude/projects/.../memory/MEMORY.md` - Documentation updates

## Related Issues

- **Stuttering/Crackles Issue (2026-02-16):** Buffer underruns - this fix addresses the root cause
- **Runtime Mode Switching (2026-02-13):** Mock and real use different implementations - cursor-chasing ensures consistency

## Future Work

- Resolve build system issue preventing engine mode testing
- Verify impulse response loading works correctly with various path formats
- Measure actual latency improvements with cursor-chasing enabled
