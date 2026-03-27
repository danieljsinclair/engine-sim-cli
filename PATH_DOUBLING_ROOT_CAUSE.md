# PATH DOUBLING BUG - ROOT CAUSE ANALYSIS

**Date:** 2026-03-26
**Agent:** SOLID PEDANT
**Task:** Investigate why paths are being doubled, causing file loading failures

---

## CONCRETE EXAMPLE FROM USER

**Script says:** `es/sound-library/new/mild_exhaust_reverb.wav`
**Asset base:** `/Users/danielsinclair/vscode/escli.refac7/es`
**Result:** `/Users/danielsinclair/vscode/escli.refac7/es/es/sound-library/...`

The "es" appears **twice** - this is the path doubling bug.

---

## ROOT CAUSE IDENTIFIED

**Location:** `src/SimulationLoop.cpp:182-198`

The CLI's `resolveConfigPath()` function has a **logic error** that causes the asset base path to be **doubled** when the script path already contains the asset directory.

### The Bug

```cpp
// SimulationLoop.cpp:182-198
assetBasePath = "engine-sim-bridge/engine-sim";  // DEFAULT

if (scriptPath.has_parent_path()) {
    std::filesystem::path parentPath = scriptPath.parent_path();
    if (parentPath.filename() == "assets") {
        assetBasePath = parentPath.parent_path().string();
    } else {
        assetBasePath = parentPath.string();  // BUG: Adds parent directory
    }
}
```

### What Happens With The User's Example

**Script path:** `/Users/danielsinclair/vscode/escli.refac7/es/main.mr`

**Step 1:** Extract parent path
```cpp
parentPath = scriptPath.parent_path();
// parentPath = "/Users/danielsinclair/vscode/escli.refac7/es"
```

**Step 2:** Check if parent is "assets"
```cpp
if (parentPath.filename() == "assets") {
    // FALSE - filename is "es", not "assets"
} else {
    assetBasePath = parentPath.string();  // BUG!
    // assetBasePath = "/Users/danielsinclair/vscode/escli.refac7/es"
}
```

**Step 3:** Bridge concatenates with script path
```cpp
// engine_sim_bridge.cpp:173
fullPath = assetBasePath + "/" + filename;
// fullPath = "/Users/danielsinclair/vscode/escli.refac7/es" + "/" + "es/sound-library/..."
// fullPath = "/Users/danielsinclair/vscode/escli.refac7/es/es/sound-library/..."
```

**RESULT:** Path is doubled - "es" appears twice!

---

## WHY THIS HAPPENS

### The Logic Flaw

The CLI code assumes:
1. Script is in `.../assets/main.mr`
2. Parent directory is `.../assets/`
3. Asset base is `.../` (parent of assets)

**BUT** the user's script is in:
1. Script is in `.../es/main.mr`
2. Parent directory is `.../es/`
3. Asset base is set to `.../es/` (WRONG!)

Then the bridge uses this asset base and concatenates with the script's relative path (`es/sound-library/...`), resulting in the doubling.

### The Bridge's Role

**Location:** `engine-sim-bridge/src/engine_sim_bridge.cpp:173`

```cpp
// Relative path - combine with asset base path
fullPath = assetBasePath + "/" + filename;
```

The bridge **blindly concatenates** the asset base path with the filename from the script, assuming:
- Asset base is the parent of the asset directory
- Filename is relative to the asset directory

**BUT** when the CLI sets asset base to `/Users/danielsinclair/vscode/escli.refac7/es` and the script says `es/sound-library/...`, the result is doubled.

---

## ARCHITECTURAL VIOLATIONS

### SRP (Single Responsibility Principle)

**Violation:** Path resolution logic is split between:
1. CLI `resolveConfigPath()` - Determines asset base
2. Bridge `loadImpulseResponses()` - Concatenates paths
3. Both make assumptions about the other's behavior

**Should Be:** Single path resolver that handles all path operations.

### DIP (Dependency Inversion Principle)

**Violation:** High-level modules depend on low-level path concatenation:
- CLI uses string manipulation for paths
- Bridge uses string concatenation for paths
- Both depend on specific directory structure

**Should Be:** Depend on abstraction (`IPathResolver` interface).

### OCP (Open/Closed Principle)

**Violation:** To fix the bug, you must modify CLI code OR add special cases.

**Should Be:** Configuration-driven path resolution that doesn't require code changes for different directory structures.

---

## THE FIX

### Immediate Fix (Stop the Bleeding)

**Location:** `src/SimulationLoop.cpp:186-190`

**Current (BUGGY):**
```cpp
if (parentPath.filename() == "assets") {
    assetBasePath = parentPath.parent_path().string();
} else {
    assetBasePath = parentPath.string();  // BUG!
}
```

**Fixed:**
```cpp
if (parentPath.filename() == "assets") {
    assetBasePath = parentPath.parent_path().string();
} else if (parentPath.filename() == "es") {
    // Handle es/ directory structure
    assetBasePath = parentPath.parent_path().string();
} else {
    // Default: use parent directory
    assetBasePath = parentPath.string();
}
```

**This fixes the immediate bug but is still a band-aid.**

### Proper Fix (Architectural)

**Create:** `PathResolver` module that handles all path operations consistently.

```cpp
// PathResolver.h
class PathResolver {
public:
    static std::string resolveAssetBase(
        const std::string& scriptPath
    ) {
        std::filesystem::path script(scriptPath);

        // Find the asset root by looking for known directories
        auto path = script.parent_path();
        while (path.has_parent_path()) {
            std::string dirname = path.filename().string();

            // Known asset root directories
            if (dirname == "es" || dirname == "assets" || dirname == "engine-sim") {
                return path.parent_path().string();
            }

            path = path.parent_path();
        }

        // Fallback: use script's parent directory
        return script.parent_path().string();
    }

    static std::string resolveWavPath(
        const std::string& assetBase,
        const std::string& filename
    ) {
        // Don't double paths!
        // If filename is already absolute, use as-is
        if (!filename.empty() && (filename[0] == '/' || filename[0] == '~')) {
            return filename;
        }

        // If filename starts with a known directory, don't add asset base
        if (filename.find("es/") == 0 || filename.find("assets/") == 0) {
            // Filename is relative to project root, not asset base
            // Use executable directory to resolve
            std::string exeDir = GetExecutableDir();
            return exeDir + "/../" + filename;
        }

        // Default: concatenate with asset base
        return assetBase + "/" + filename;
    }
};
```

**Benefits:**
1. Single source of truth for path resolution
2. No path doubling
3. Consistent behavior across all layers
4. Easy to test and maintain

---

## PREVENTION: How to Stop This Class of Bug

### 1. Centralize All Path Resolution

**Rule:** ALL path operations go through `PathResolver`.

**Enforcement:**
- Ban direct string concatenation for paths
- Ban `std::filesystem::absolute()` in application code
- Use `PathResolver` for all path operations

### 2. Use Path Normalization

**After any path operation:**
```cpp
std::filesystem::path path(resolvedPath);
path = path.lexically_normal();  // Remove redundant separators, resolve ..
resolvedPath = path.string();
```

### 3. Add Path Validation

**Before using any path:**
```cpp
if (!std::filesystem::exists(resolvedPath)) {
    std::cerr << "ERROR: Path does not exist: " << resolvedPath << "\n";
    std::cerr << "       (original: " << originalPath << ")\n";
    return ERROR;
}
```

### 4. Add Unit Tests

```cpp
TEST(PathResolver, NoPathDoubling) {
    std::string assetBase = "/Users/danielsinclair/vscode/escli.refac7/es";
    std::string filename = "es/sound-library/new/mild_exhaust_reverb.wav";

    std::string resolved = PathResolver::resolveWavPath(assetBase, filename);

    // Should NOT double the "es" directory
    EXPECT_FALSE(resolved.find("/es/es/") != std::string::npos);
}
```

---

## ADDITIONAL VIOLATION: Broken Bridge Path Resolution

**User Feedback:** "This is really shitty as well"

**Location:** `engine-sim-bridge/src/engine_sim_bridge.cpp:376-396`

The bridge has its own path resolution logic that **CONFLICTS** with the CLI:

```cpp
// Determine asset base path
std::string resolvedAssetPath;
if (assetBasePath != nullptr && strlen(assetBasePath) > 0) {
    resolvedAssetPath = assetBasePath;
    std::cerr << "DEBUG BRIDGE: Using provided assetBasePath: " << resolvedAssetPath << "\n";
} else {
    // Default: derive from script path (go up to assets/ directory)
    std::string scriptPathStr(scriptPath);
    size_t assetsPos = scriptPathStr.find("/assets/");
    if (assetsPos != std::string::npos) {
        // Extract path up to and including /assets/
        resolvedAssetPath = scriptPathStr.substr(0, assetsPos + 8); // +8 for "/assets/"
    } else {
         // Default: use script's directory
        size_t lastSlash = scriptPathStr.find_last_of('/');
        if (lastSlash != std::string::npos) {
            resolvedAssetPath = scriptPathStr.substr(0, lastSlash);
        } else {
            resolvedAssetPath = ".";  // Current directory
        }
    }
}
```

**SOLID VIOLATION: DRY (Don't Repeat Yourself)**

**The Problem:**
1. CLI has path resolution logic in `SimulationLoop.cpp:171-205`
2. Bridge has path resolution logic in `engine_sim_bridge.cpp:376-396`
3. **Both make different assumptions**
4. **Both use different algorithms**
5. **Both can be wrong in different ways**

**Specific Issues:**
- Bridge looks for `/assets/` in script path
- CLI looks for parent directory named `assets`
- Bridge uses string searching (`find("/assets/")`)
- CLI uses filesystem operations (`parent_path().filename()`)
- Bridge defaults to script's directory if no `/assets/` found
- CLI defaults to parent directory if no `assets` parent found

**Result:** When CLI passes an "incorrect" asset base path, the bridge uses it anyway, causing the path doubling bug.

**WHY THIS IS REALLY SHITTY:**
- Two different path resolvers that don't agree
- No validation that CLI's asset base is correct
- No validation that bridge's resolution is correct
- Debug output shows "Using provided assetBasePath" even if it's wrong
- Silent failures when paths don't exist

**SOLID VIOLATIONS:**
- **DRY:** Path resolution logic duplicated
- **SRP:** Both CLI and bridge responsible for path resolution
- **OCP:** To fix path bugs, must modify both CLI and bridge
- **DIP:** Both layers depend on low-level string manipulation

**THE FIX:**
Bridge should either:
1. **Trust CLI completely** - Use provided asset base without fallback
2. **Validate CLI's asset base** - Check if it exists, error if not
3. **Reject relative paths** - Require absolute paths from CLI

**Current behavior (broken):**
- Bridge accepts CLI's asset base without validation
- Bridge has fallback logic that conflicts with CLI
- No error when paths don't exist until file load fails

**Proper behavior:**
```cpp
// Validate provided asset base
if (assetBasePath != nullptr && strlen(assetBasePath) > 0) {
    // Verify it's an absolute path
    if (assetBasePath[0] != '/') {
        ctx->setError("Asset base path must be absolute: " + std::string(assetBasePath));
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    // Verify it exists
    if (!std::filesystem::exists(assetBasePath)) {
        ctx->setError("Asset base path does not exist: " + std::string(assetBasePath));
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    resolvedAssetPath = assetBasePath;
} else {
    // Error: CLI must provide asset base
    ctx->setError("Asset base path is required");
    return ESIM_ERROR_INVALID_PARAMETER;
}
```

**This eliminates the bridge's fallback logic and makes CLI responsible for correct path resolution.**

---

## CONCLUSION

**ROOT CAUSE:** The CLI's `resolveConfigPath()` function incorrectly sets the asset base path to the script's parent directory when the parent is not "assets". This causes the bridge to concatenate the asset base with the script's relative path, resulting in path doubling.

**WHY IT KEEPS HAPPENING:** Path resolution logic is split between CLI and bridge, with both making assumptions about the other's behavior. There's no single source of truth for path operations.

**HOW TO FIX PERMANENTLY:**
1. Create centralized `PathResolver` module
2. Eliminate direct path concatenation
3. Add path validation and normalization
4. Add unit tests for path resolution

**CRITICAL:** The immediate fix is to add a check for the "es" directory in `resolveConfigPath()`, but the proper fix is to centralize all path resolution logic.

---

*Investigation complete. Root cause identified: Incorrect asset base path resolution in CLI, causing bridge to double paths when concatenating.*
