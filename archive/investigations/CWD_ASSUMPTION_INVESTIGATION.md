# CWD ASSUMPTION ROOT CAUSE INVESTIGATION

**Date:** 2026-03-26
**Agent:** SOLID PEDANT
**Task:** Investigate why file path issues keep happening. Identify root cause and how to prevent it permanently.

---

## EXECUTIVE SUMMARY

**ROOT CAUSE IDENTIFIED:** The codebase makes **THREE INDEPENDENT CWD ASSUMPTIONS** across different layers:

1. **Compiler Search Paths** (compiler.cpp:25-27) - Hardcoded relative paths
2. **WavLoader File Loading** (wav_loader.h:40) - Uses dr_wav which relies on CWD
3. **CLI Path Resolution** (EngineConfig.cpp:72-73) - Uses `std::filesystem::absolute()` which depends on CWD

**CRITICAL FINDING:** When the program is run from different working directories, these assumptions break, causing file loading failures.

---

## VIOLATION #1: Compiler Search Paths (HARDCODED)

**Location:** `engine-sim-bridge/engine-sim/scripting/src/compiler.cpp:25-27`

```cpp
void es_script::Compiler::initialize() {
    m_compiler = new piranha::Compiler(&m_rules);
    m_compiler->setFileExtension(".mr");

    m_compiler->addSearchPath("../../es/");  // CWD ASSUMPTION!
    m_compiler->addSearchPath("../es/");     // CWD ASSUMPTION!
    m_compiler->addSearchPath("es/");        // CWD ASSUMPTION!

    m_rules.initialize();
}
```

**SOLID VIOLATION:**
- **SRP:** Compiler shouldn't know about directory structure
- **OCP:** Adding new search paths requires modifying compiler code
- **DIP:** High-level compiler depends on low-level file system structure

**PROBLEM:**
- These paths only work if CWD is in a specific location
- If you run from `/Users/danielsinclair/vscode/escli.refac7/build/`, the paths fail
- If you run from `/Users/danielsinclair/vscode/escli.refac7/`, the paths work

**EVIDENCE:**
- `"../../es/"` - assumes CWD is 2 levels deep from `es/` directory
- `"../es/"` - assumes CWD is 1 level deep from `es/` directory
- `"es/"` - assumes CWD is at the parent of `es/` directory

---

## VIOLATION #2: WavLoader File Loading (CWD DEPENDENT)

**Location:** `engine-sim-bridge/include/wav_loader.h:40`

```cpp
static Result load(const std::string& filepath) {
    Result result;
    result.valid = false;

    drwav wav;
    if (!drwav_init_file(&wav, filepath.c_str(), nullptr)) {
        return result;  // FAILS if filepath is relative and CWD is wrong
    }
    // ...
}
```

**SOLID VIOLATION:**
- **SRP:** WavLoader doesn't validate or resolve paths
- **DIP:** WavLoader depends on external CWD state

**PROBLEM:**
- `drwav_init_file()` uses the CWD to resolve relative paths
- If `filepath` is relative and CWD is wrong, file load fails
- No validation or error reporting about CWD issues

**CALLED FROM:**
```cpp
// engine_sim_bridge.cpp:182
WavLoader::Result wavResult = WavLoader::load(fullPath);
```

Where `fullPath` is constructed from:
```cpp
// engine_sim_bridge.cpp:177
fullPath = assetBasePath + "/" + filename;
```

**ISSUE:** If `assetBasePath` is relative and CWD is wrong, this fails.

---

## VIOLATION #3: CLI Path Resolution (CWD DEPENDENT)

**Location:** `src/EngineConfig.cpp:72-73`

```cpp
std::string EngineConfig::resolveAssetBasePath(const std::string& configPath) {
    try {
        std::filesystem::path scriptPath(configPath);

        // Make absolute if relative
        if (scriptPath.is_relative()) {
            scriptPath = std::filesystem::absolute(scriptPath);  // CWD DEPENDENT!
        }
        // ...
    }
}
```

**SOLID VIOLATION:**
- **SRP:** Path resolution logic scattered across CLI and bridge
- **DIP:** High-level CLI code depends on CWD

**PROBLEM:**
- `std::filesystem::absolute()` uses the **current working directory**
- If user runs from different directory, paths resolve incorrectly
- No validation that resolved paths actually exist

**ALSO:** `src/EngineConfig.cpp:89-93`
```cpp
// Make absolute
std::filesystem::path assetPath(assetBase);
if (assetPath.is_relative()) {
    assetPath = std::filesystem::absolute(assetPath);  // CWD DEPENDENT!
}
assetPath = assetPath.lexically_normal();
```

---

## VIOLATION #4: Default Config Path (RELATIVE)

**Location:** `src/EngineConfig.cpp:58`

```cpp
else {
    // use default
    paths.configPath = "engine-sim-bridge/engine-sim/assets/main.mr";  // RELATIVE!
}
```

**SOLID VIOLATION:**
- **SRP:** Default path hardcoded in application logic
- **OCP:** Changing default requires code modification

**PROBLEM:**
- Relative path depends on CWD
- No validation that this path exists
- Fails silently or with cryptic error

---

## ROOT CAUSE ANALYSIS

### Why File Path Issues Keep Happening

1. **No Single Source of Truth for Paths**
   - Compiler has search paths
   - CLI resolves paths
   - Bridge resolves paths
   - WavLoader uses paths as-is
   - Each layer makes independent assumptions

2. **CWD Assumptions Scattered Across Layers**
   - Compiler: Hardcoded relative search paths
   - CLI: Uses `std::filesystem::absolute()` (CWD dependent)
   - Bridge: Concatenates paths without validation
   - WavLoader: Passes paths to dr_wag (CWD dependent)

3. **No Path Validation**
   - No checks if resolved paths exist
   - No checks if paths are absolute
   - No checks if CWD is correct
   - Failures are cryptic or silent

4. **Multiple "Smart" Path Resolvers**
   - CLI tries to be smart about paths
   - Bridge tries to be smart about paths
   - Compiler tries to be smart about paths
   - Each "smart" resolver conflicts with others

---

## ARCHITECTURAL VIOLATIONS

### SRP (Single Responsibility Principle)

**Violation:** Path resolution logic is scattered across:
- `es_script::Compiler::initialize()` - Search paths
- `EngineConfig::resolveAssetBasePath()` - Asset path resolution
- `EngineConfig::resolvePath()` - Generic path resolution
- `WavLoader::load()` - File loading
- Bridge `loadImpulseResponses()` - Path concatenation

**Should Be:** Single path resolver module that ALL layers use.

### DIP (Dependency Inversion Principle)

**Violation:** High-level modules depend on low-level file system details:
- CLI depends on `std::filesystem::absolute()` (CWD dependent)
- Compiler depends on hardcoded relative paths
- Bridge depends on path concatenation logic

**Should Be:** Depend on abstraction (`IPathResolver` interface).

### OCP (Open/Closed Principle)

**Violation:** Adding new search locations requires modifying:
- Compiler code (search paths)
- CLI code (default paths)
- Bridge code (path resolution)

**Should Be:** Configuration-driven path resolution.

---

## SPECIFIC ISSUE: File Path Doubling

**Context:** Task #10 "Fix file path doubling to test dirty sound"

**HYPOTHESIS:** The path resolution logic in `EngineConfig::resolveAssetBasePath()` may be causing path doubling when:
1. CLI passes an already-absolute path
2. Bridge adds assetBasePath (also absolute)
3. Result: `/absolute/path/absolute/path/file.wav`

**EVIDENCE:** From `engine_sim_bridge.cpp:176-178`
```cpp
// Relative path - combine with asset base path
fullPath = assetBasePath + "/" + filename;
std::cerr << "DEBUG BRIDGE: Loading impulse: fullPath=" << fullPath << "\n";
```

**ISSUE:** The code checks `isRelativePath()` but may misclassify absolute paths, leading to:
- Absolute path + "/" + relative path = doubled path
- Or: Relative path interpreted as absolute + "/" + relative path = doubled path

---

## RECOMMENDATIONS

### 1. Create Single Path Resolver Module (SRP, DRY)

```cpp
// PathResolver.h
class PathResolver {
public:
    struct Paths {
        std::string scriptPath;      // Absolute path to .mr file
        std::string assetBasePath;   // Absolute path to assets
        std::string workingDir;      // Absolute path to working directory
    };

    static Paths resolvePaths(
        const std::string& scriptPathInput,
        const std::string& executablePath
    );

    static std::string makeAbsolute(
        const std::string& path,
        const std::string& basePath
    );

    static bool validateExists(const std::string& path);
};
```

**Benefits:**
- Single source of truth for path resolution
- Centralized validation
- Consistent behavior across all layers

### 2. Eliminate CWD Assumptions (DIP)

**Replace:**
- `std::filesystem::absolute()` - CWD dependent
- Hardcoded search paths - CWD dependent
- Relative path concatenation - CWD dependent

**With:**
- Paths relative to executable location
- Paths relative to script location
- Explicit absolute paths from configuration

### 3. Use Executable Location as Anchor (OCP)

**Current:** `GetExecutableDir()` in `engine_sim_loader.h` works!

**Use it for:**
- Asset base path resolution
- Script path resolution
- Default path fallback

**Example:**
```cpp
std::string exeDir = GetExecutableDir();
std::string assetBase = exeDir + "/../engine-sim-bridge/engine-sim";
```

### 4. Add Path Validation (Fail Fast)

**Before using any path:**
```cpp
if (!PathResolver::validateExists(resolvedPath)) {
    std::cerr << "ERROR: Path does not exist: " << resolvedPath << "\n";
    std::cerr << "       (resolved from: " << originalPath << ")\n";
    return ERROR;
}
```

### 5. Remove Compiler Search Paths (SRP)

**Delete:**
```cpp
m_compiler->addSearchPath("../../es/");
m_compiler->addSearchPath("../es/");
m_compiler->addSearchPath("es/");
```

**Replace with:**
- Absolute script path passed to `compile()`
- Asset base path derived from script path
- No search paths needed

### 6. Fix Path Doubling Bug

**In `engine_sim_bridge.cpp:176-178`:**

**Current (BUGGY):**
```cpp
// Relative path - combine with asset base path
fullPath = assetBasePath + "/" + filename;
```

**Fix:**
```cpp
// Check if filename is already absolute
if (filename[0] == '/' || filename[0] == '~') {
    fullPath = filename;  // Already absolute, use as-is
} else {
    fullPath = assetBasePath + "/" + filename;  // Relative, combine
}
```

**Better:** Use `PathResolver` for consistent behavior.

---

## PREVENTION: How to Stop This Happening Permanently

### 1. Establish Path Resolution Policy

**RULE:** All paths MUST be:
1. Absolute before use
2. Validated to exist
3. Resolved relative to executable, not CWD
4. Logged at debug level

**ENFORCEMENT:**
- Add `PathResolver::validate()` to all file operations
- Add assertions in debug builds
- Add unit tests for path resolution

### 2. Eliminate Relative Paths from Code

**FIND:** All relative paths
```bash
grep -r '"\.\./\|"\.\|"engine-' --include="*.cpp" --include="*.h"
```

**REPLACE:** With paths resolved from executable location

### 3. Add Path Resolution Unit Tests

```cpp
TEST(PathResolver, ResolveRelativeScriptPath) {
    std::string exeDir = "/path/to/exe";
    std::string script = "engine.mr";
    Paths resolved = PathResolver::resolvePaths(script, exeDir);
    EXPECT_TRUE(resolved.scriptPath.is_absolute());
    EXPECT_TRUE(PathResolver::validateExists(resolved.scriptPath));
}
```

### 4. Document Path Resolution Architecture

**Create:** `docs/PATH_RESOLUTION_ARCHITECTURE.md`

**Contents:**
- How paths flow through the system
- Where paths are resolved
- How to add new file loading
- Common pitfalls

### 5. Add CWD Validation on Startup

```cpp
int main(int argc, char* argv[]) {
    // Validate CWD is correct
    std::string exeDir = GetExecutableDir();
    std::string cwd = std::filesystem::current_path().string();

    if (cwd != expectedWorkingDir) {
        std::cerr << "WARNING: Unexpected working directory: " << cwd << "\n";
        std::cerr << "         Expected: " << expectedWorkingDir << "\n";
        std::cerr << "         Using executable location for path resolution\n";
    }

    // ... rest of init
}
```

---

## CONCLUSION

**ROOT CAUSE:** The codebase makes **multiple independent CWD assumptions** across different layers, with:
1. No single source of truth for path resolution
2. Scattered path validation logic
3. Hardcoded relative paths in compiler
4. CWD-dependent APIs (`std::filesystem::absolute()`, `drwav_init_file()`)
5. No validation of resolved paths

**WHY IT KEEPS HAPPENING:** Each layer tries to be "smart" about paths, but there's no coordination. The compiler has search paths, the CLI resolves paths, the bridge resolves paths, and WavLoader uses paths as-is. Each "smart" resolver conflicts with others.

**HOW TO PREVENT PERMANENTLY:**
1. Create single `PathResolver` module (SRP)
2. Use executable location as anchor (not CWD)
3. Eliminate all relative paths from code
4. Add path validation everywhere (fail fast)
5. Add unit tests for path resolution
6. Document path resolution architecture

**CRITICAL:** The path doubling bug (Task #10) is likely caused by conflicting "smart" path resolvers concatenating already-absolute paths. This is a direct result of the architectural violations identified in this investigation.

---

*Investigation complete. Root cause identified: Multiple independent CWD assumptions across layers with no single source of truth for path resolution.*
