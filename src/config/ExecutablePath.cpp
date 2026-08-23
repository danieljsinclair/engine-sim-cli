#include "config/ExecutablePath.h"

#include <array>
#include <cstring>
#include <filesystem>
#include <string>
#include <string_view>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(__linux__)
#include <unistd.h>
#include <limits.h>
#elif defined(_WIN32)
#include <windows.h>
#endif

namespace cli {

namespace {

// Maximum number of parent levels probed when walking up from the executable
// directory. Covers the deepest nested build layout in the tree (build/test/).
constexpr int MaxParentWalkDepth = 8;

// Parent directory of `path` (no trailing slash), or "" at the filesystem root.
// Takes a view: every caller passes an existing string and the result is a fresh
// std::string, so no ownership needs to change hands.
std::string parentDir(std::string_view path) {
    std::string parent;

    // pos == npos: no separator, so there is no parent — "" (the initial value).
    if (const auto pos = path.find_last_of('/'); pos == 0) {
        parent = "/";
    }
    else if (pos != std::string_view::npos) {
        parent = std::string(path.substr(0, pos));
    }

    return parent;
}

// exists() via the error_code overload: the throwing overload raises
// filesystem_error on EACCES/ELOOP instead of reporting "absent", which would
// escape these noexcept resolvers as a terminate. A probe that cannot be
// answered counts as "not found", exactly as before.
bool pathExists(const std::filesystem::path& candidate) noexcept {
    std::error_code ec;
    return std::filesystem::exists(candidate, ec);
}

std::string joinPath(const std::string& dir, const std::string& rel) {
    if (dir.empty()) return rel;
    if (dir.back() != '/') {
        std::string out = dir;
        out.push_back('/');
        out += rel;
        return out;
    }
    return dir + rel;
}

} // namespace

std::string ExecutablePath::directory() noexcept {
    std::string result;

#if defined(__APPLE__)
    std::array<char, PATH_MAX> buf{};
    // Buffer too small: `size` would hold the required length, but a larger
    // stack buffer cannot be substituted here, so bail out gracefully ("").
    if (auto size = static_cast<uint32_t>(buf.size());
        _NSGetExecutablePath(buf.data(), &size) == 0) {
        // buf is NUL-terminated by _NSGetExecutablePath, so the view can be built
        // straight from the pointer — no owning std::string temporary needed.
        result = parentDir(buf.data());
    }
#elif defined(__linux__)
    std::array<char, PATH_MAX> buf{};
    if (const ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
        len > 0) {
        buf[static_cast<size_t>(len)] = '\0';
        // NUL written above, so the view reads directly from the buffer.
        result = parentDir(buf.data());
    }
#elif defined(_WIN32)
    std::array<wchar_t, MAX_PATH> buf{};
    const DWORD len = GetModuleFileNameW(nullptr, buf.data(),
                                         static_cast<DWORD>(buf.size()));
    if (len != 0) {
        const int n = WideCharToMultiByte(CP_UTF8, 0, buf.data(), static_cast<int>(len),
                                          nullptr, 0, nullptr, nullptr);
        if (n > 0) {
            std::string out(static_cast<size_t>(n), '\0');
            WideCharToMultiByte(CP_UTF8, 0, buf.data(), static_cast<int>(len),
                                out.data(), n, nullptr, nullptr);
            result = parentDir(out);
        }
    }
#endif

    return result;
}

std::string ExecutablePath::resolveResource(
    const std::string& relativeResourcePath) noexcept
{
    const std::string exeDir = directory();
    std::string resolved;

    // 1. Walk up from the executable directory: <dir>/<relativeResourcePath>.
    //    Handles nested build dirs (build/, build/test/), so both the released
    //    binary and the test binary find the resource shipped under the project
    //    root regardless of CWD.
    //
    //    A while loop, not a for: the walk advances by *reassigning* dir to its
    //    parent, which is loop state the for-header cannot express as an update
    //    expression. Depth is bounded explicitly alongside it.
    std::string dir = exeDir;
    int depth = 0;
    while (resolved.empty() && !dir.empty() && depth < MaxParentWalkDepth) {
        if (const std::string candidate = joinPath(dir, relativeResourcePath);
            pathExists(candidate)) {
            resolved = candidate;
        }
        else {
            dir = parentDir(dir);
            ++depth;
        }
    }

    // 2. Current working directory fallback (backward-compatible behaviour).
    if (resolved.empty()) {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            if (const std::string candidate = joinPath(cwd.string(), relativeResourcePath);
                pathExists(candidate)) {
                resolved = candidate;
            }
        }
    }

    // 3. Nothing found: compose the install-relative best-effort path so the
    //    caller produces a clear "not found" error rather than an empty path.
    if (resolved.empty()) {
        resolved = exeDir.empty() ? relativeResourcePath
                                  : joinPath(exeDir, relativeResourcePath);
    }

    return resolved;
}

} // namespace cli
