#include "config/ExecutablePath.h"

#include <cstring>
#include <filesystem>
#include <string>

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

// Parent directory of `path` (no trailing slash), or "" at the filesystem root.
std::string parentDir(const std::string& path) {
    const auto pos = path.find_last_of('/');
    if (pos == std::string::npos) return "";
    if (pos == 0) return "/";
    return path.substr(0, pos);
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
#if defined(__APPLE__)
    std::vector<char> buf(PATH_MAX);
    uint32_t size = static_cast<uint32_t>(buf.size());
    if (_NSGetExecutablePath(buf.data(), &size) != 0) {
        // Buffer too small: size now holds the required length. Retry with a
        // larger stack buffer is not possible, so bail out gracefully.
        return "";
    }
    return parentDir(std::string(buf.data()));
#elif defined(__linux__)
    std::vector<char> buf(PATH_MAX);
    ssize_t len = readlink("/proc/self/exe", buf.data(), buf.size() - 1);
    if (len <= 0) return "";
    buf[static_cast<size_t>(len)] = '\0';
    return parentDir(std::string(buf.data()));
#elif defined(_WIN32)
    wchar_t buf[MAX_PATH];
    DWORD len = GetModuleFileNameW(nullptr, buf, MAX_PATH);
    if (len == 0) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(len),
                                nullptr, 0, nullptr, nullptr);
    if (n <= 0) return "";
    std::string out(static_cast<size_t>(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, buf, static_cast<int>(len),
                        out.data(), n, nullptr, nullptr);
    return parentDir(out);
#else
    return "";
#endif
}

std::string ExecutablePath::resolveResource(
    const std::string& relativeResourcePath) noexcept
{
    const std::string exeDir = directory();

    // 1. Walk up from the executable directory: <dir>/<relativeResourcePath>.
    //    Handles nested build dirs (build/, build/test/), so both the released
    //    binary and the test binary find the resource shipped under the project
    //    root regardless of CWD.
    if (!exeDir.empty()) {
        std::string dir = exeDir;
        for (int depth = 0; depth < 8 && !dir.empty(); ++depth) {
            const std::string candidate = joinPath(dir, relativeResourcePath);
            std::error_code ec;
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
            dir = parentDir(dir);
        }
    }

    // 2. Current working directory fallback (backward-compatible behaviour).
    {
        std::error_code ec;
        const std::filesystem::path cwd = std::filesystem::current_path(ec);
        if (!ec) {
            const std::string candidate = joinPath(cwd.string(), relativeResourcePath);
            if (std::filesystem::exists(candidate, ec)) {
                return candidate;
            }
        }
    }

    // Nothing found: return the install-relative best-effort path so the caller
    // produces a clear "not found" error rather than an empty path.
    if (!exeDir.empty()) {
        return joinPath(exeDir, relativeResourcePath);
    }
    return relativeResourcePath;
}

} // namespace cli
