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

            // If we're in build/test/, go up two levels
            size_t pos = currentDir.find("/build/test");
            if (pos != std::string::npos) {
                return currentDir.substr(0, pos);
            }

            // If we're in build/, go up one level
            pos = currentDir.find("/build");
            if (pos != std::string::npos) {
                return currentDir.substr(0, pos);
            }

            // Assume we're already at project root
            return currentDir;
        }

        // Fallback to relative path
        return "../..";
    }

    // Get the absolute path to the CLI executable
    static std::string getCLIPath() {
        std::string projectRoot = getProjectRoot();
        return projectRoot + "/build/engine-sim-cli";
    }

    // Run the CLI with the given arguments from the project root directory
    // Redirects output to a log file to keep test output clean
    static int runCLI(const std::string& args) {
        std::string projectRoot = getProjectRoot();
        std::string cliPath = getCLIPath();
        std::string logFile = projectRoot + "/build/cli_test_" + std::to_string(getpid()) + ".log";
        std::string command = "cd \"" + projectRoot + "\" && \"" + cliPath + "\" " + args + " >> " + logFile + " 2>&1";
        return system(command.c_str());
    }
};

#endif // SMOKE_TEST_HELPER_H
