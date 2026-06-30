// SmokeTestHelper.h - Shared utilities for smoke tests
//
// This header provides common functionality for all smoke tests including
// path resolution and CLI execution helpers.

#ifndef SMOKE_TEST_HELPER_H
#define SMOKE_TEST_HELPER_H

#include <string>
#include <unistd.h>
#include <cstdio>
#include <cstring>

// PATH_MAX may not be defined on all systems
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

class SmokeTestHelper {
public:
    // Get the project root directory
    static std::string getProjectRoot() {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::string currentDir(cwd);

            // If we're in a build directory's test/, go up two levels.
            // Handles build/, build-cov/, build-coverage/, etc.
            size_t pos = currentDir.find("/build");
            if (pos != std::string::npos) {
                // Check for /build-X/test/ pattern (two levels up)
                size_t testPos = currentDir.find("/test", pos);
                if (testPos != std::string::npos) {
                    return currentDir.substr(0, pos);
                }
                // Just /build-X/ (one level up)
                return currentDir.substr(0, pos);
            }

            // Assume we're already at project root
            return currentDir;
        }

        // Fallback to relative path
        return "../..";
    }

    // Get the absolute path to the CLI executable. Uses the SAME build
    // directory the tests are running from (build/ for normal, build-cov/
    // for coverage) so instrumented binaries are exercised when present.
    static std::string getCLIPath() {
        char cwd[PATH_MAX];
        if (getcwd(cwd, sizeof(cwd)) != nullptr) {
            std::string currentDir(cwd);
            std::string projectRoot = getProjectRoot();
            // Derive the build dir from cwd (e.g. build-cov, build, build-coverage)
            size_t pos = currentDir.find("/build");
            if (pos != std::string::npos) {
                size_t buildEnd = currentDir.find('/', pos + 1);
                std::string buildDir = (buildEnd != std::string::npos)
                    ? currentDir.substr(pos + 1, buildEnd - pos - 1)
                    : currentDir.substr(pos + 1);
                // If we're in buildDir/test/, the binary is in buildDir/
                return projectRoot + "/" + buildDir + "/engine-sim-cli";
            }
        }
        return getProjectRoot() + "/build/engine-sim-cli";
    }

    // Run the CLI with the given arguments from the project root directory
    // Redirects output to a log file to keep test output clean
    static int runCLI(const std::string& args) {
        std::string projectRoot = getProjectRoot();
        std::string cliPath = getCLIPath();
        std::string logFile = projectRoot + "/build/cli_test_" + std::to_string(getpid()) + ".log";
        std::string command = "cd \"" + projectRoot + "\" && \"" + cliPath + "\" " + args;

        // Always capture the CLI's stdout + stderr to the log file. Tests that
        // pass their own explicit file redirect (e.g. > /dev/null or 2>&1 merged
        // into a file they manage) have their trailing redirect stripped so our
        // append doesn't fight with theirs.
        static const std::string kStderrMerge = "2>&1";
        static const std::string kStdoutRedirect = ">";
        bool endsWithStderrMerge = (args.size() >= kStderrMerge.size()) &&
            (args.compare(args.size() - kStderrMerge.size(), kStderrMerge.size(), kStderrMerge) == 0);
        std::string strippedArgs = endsWithStderrMerge
            ? args.substr(0, args.size() - kStderrMerge.size())
            : args;
        // Rebuild the command without the trailing 2>&1 (if present).
        command = "cd \"" + projectRoot + "\" && \"" + cliPath + "\" " + strippedArgs;
        command += " >> \"" + logFile + "\" 2>&1";

        return system(command.c_str());
    }
};

#endif // SMOKE_TEST_HELPER_H
