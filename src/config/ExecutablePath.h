#pragma once

#include <string>

namespace cli {

/**
 * Cross-platform helper to locate the running executable and resolve resource
 * paths relative to it (instead of the caller's CWD/PWD).
 *
 * Presets and other shipped assets live relative to the binary's install
 * location, so the CLI must resolve them against the executable, not the
 * current working directory. This lets the binary run from any directory.
 *
 * Executable directory resolution, in order of preference:
 *   - macOS:   _NSGetExecutablePath
 *   - Linux:   readlink("/proc/self/exe")
 *   - Windows: GetModuleFileNameW
 *   - fallback: empty string (caller falls back to PWD).
 */
class ExecutablePath {
public:
    /// Absolute directory containing the running executable (no trailing
    /// slash), or "" if it cannot be determined.
    [[nodiscard]] static std::string directory() noexcept;

    /// Resolve a resource path that is shipped relative to the executable's
    /// *install root* (e.g. "engine-sim-bridge/preset/").
    ///
    /// Search order (first existing directory/file wins):
    ///   1. Walk up from the executable's directory trying
    ///      <dir>/<relativeResourcePath> at each level (handles nested build
    ///      dirs such as build/ and build/test/).
    ///   2. The current working directory: <cwd>/<relativeResourcePath>.
    ///
    /// @return absolute path to the first existing resource, or a best-effort
    ///         composed path (still install/PWD-relative) if none is found.
    [[nodiscard]] static std::string resolveResource(
        const std::string& relativeResourcePath) noexcept;
};

} // namespace cli
