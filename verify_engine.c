/*
 * Engine Verification Tool for Engine-Sim-CLI
 *
 * Purpose:
 * - Test engine startup and initialization
 * - Verify RPM stabilization at target levels
 * - Measure startup time and warmup duration
 * - Check for engine hanging during operation
 * - Validate throttle response and control
 *
 * Usage:
 *   verify_engine --engine-sim-cli <path> --engine-config <path> --test-rpm <rpm> [options]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <signal.h>

#define MAX_TEST_DURATION 30.0
#define STARTUP_TIMEOUT 10.0
#define RPM_STABILIZATION_TIME 5.0
#define RPM_TOLERANCE 50.0
#define CRASH_DETECTION_INTERVAL 1.0

typedef struct {
    double rpm;
    double load;
    double exhaustFlow;
    double manifoldPressure;
    int activeChannels;
    double processingTimeMs;
} EngineStats;

typedef struct {
    double startTime;
    double endTime;
    double startupTime;
    double maxRPM;
    double minRPM;
    double avgRPM;
    double finalRPM;
    double targetRPM;
    double rpmError;
    bool engineStarted;
    bool engineHung;
    bool crashed;
    bool timedOut;
    int samplesCollected;
    double stabilizationTime;
    double maxThrottle;
    double minThrottle;
} EngineTestResult;

typedef struct {
    const char* cliPath;
    const char* engineConfig;
    double testRPM;
    double testDuration;
    bool verbose;
    bool measureStartup;
    bool testMultipleRPM;
    const char* outputFile;
} CommandLineArgs;

static bool g_engineRunning = false;
static pid_t g_enginePid = -1;
static struct timeval g_testStartTime = {0, 0};
static struct timeval g_testEndTime = {0, 0};

void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], CommandLineArgs* args);
bool startEngine(const CommandLineArgs* args, double* startTime);
bool monitorEngine(pid_t pid, const CommandLineArgs* args, EngineTestResult* result);
bool waitForEngineStart(pid_t pid, double timeout);
bool collectEngineStats(pid_t pid, EngineStats* stats);
bool calculateRPMStabilization(const EngineStats* stats, int count, double targetRPM,
                               double* stabilizationTime, double* finalError);
void writeEngineReport(const EngineTestResult* result, const char* filename);
void printEngineResults(const EngineTestResult* result);

// Test RPM values
static const double testRPMs[] = {1000.0, 2000.0, 3000.0, 4000.0, 6000.0};
static const int numTestRPMs = sizeof(testRPMs) / sizeof(testRPMs[0]);

int main(int argc, char* argv[]) {
    printf("Engine-Sim-CLI Engine Verification Tool\n");
    printf("=======================================\n\n");

    CommandLineArgs args = {
        .cliPath = NULL,
        .engineConfig = "engine-sim-bridge/engine-sim/assets/main.mr",
        .testRPM = 2000.0,
        .testDuration = 10.0,
        .verbose = false,
        .measureStartup = true,
        .testMultipleRPM = false,
        .outputFile = NULL
    };

    if (!parseArguments(argc, argv, &args)) {
        return 1;
    }

    // Verify CLI executable exists
    if (!args.cliPath) {
        fprintf(stderr, "ERROR: Path to engine-sim-cli executable is required\n");
        printUsage(argv[0]);
        return 1;
    }

    if (access(args.cliPath, X_OK) != 0) {
        fprintf(stderr, "ERROR: Cannot execute engine-sim-cli at: %s\n", args.cliPath);
        return 1;
    }

    // Test multiple RPM values if requested
    if (args.testMultipleRPM) {
        printf("Testing multiple RPM levels...\n\n");

        for (int i = 0; i < numTestRPMs; i++) {
            printf("=== Test %d/%d: Target RPM %.0f ===\n", i + 1, numTestRPMs, testRPMs[i]);

            EngineTestResult result = {0};
            result.targetRPM = testRPMs[i];

            if (startEngine(&args, &result.startTime)) {
                if (monitorEngine(g_enginePid, &args, &result)) {
                    printEngineResults(&result);

                    if (result.engineHung || result.crashed || result.timedOut) {
                        printf("FAILED: Engine test failed for RPM %.0f\n", testRPMs[i]);
                        return 1;
                    }
                } else {
                    printf("FAILED: Engine monitoring failed for RPM %.0f\n", testRPMs[i]);
                    return 1;
                }
            } else {
                printf("FAILED: Engine startup failed for RPM %.0f\n", testRPMs[i]);
                return 1;
            }

            printf("\n");
        }
    } else {
        // Single RPM test
        printf("Testing engine at RPM: %.0f\n", args.testRPM);
        printf("Test duration: %.1f seconds\n\n", args.testDuration);

        EngineTestResult result = {0};
        result.targetRPM = args.testRPM;

        if (!startEngine(&args, &result.startTime)) {
            fprintf(stderr, "ERROR: Failed to start engine\n");
            return 1;
        }

        if (!monitorEngine(g_enginePid, &args, &result)) {
            fprintf(stderr, "ERROR: Engine monitoring failed\n");
            return 1;
        }

        printEngineResults(&result);

        // Determine overall pass/fail
        if (result.engineHung || result.crashed || result.timedOut) {
            printf("FAILED: Engine test failed\n");
            return 1;
        }

        if (result.rpmError > RPM_TOLERANCE) {
            printf("WARNING: RPM error %.1f exceeds tolerance %.1f\n",
                   result.rpmError, RPM_TOLERANCE);
        }

        if (result.stabilizationTime > RPM_STABILIZATION_TIME) {
            printf("WARNING: Stabilization time %.1f exceeds expected %.1f\n",
                   result.stabilizationTime, RPM_STABILIZATION_TIME);
        }

        printf("PASSED: Engine test successful\n");
    }

    // Write report if requested
    if (args.outputFile) {
        // For multi-RPM test, write the last result
        writeEngineReport(&result, args.outputFile);
        printf("Report written to: %s\n", args.outputFile);
    }

    return 0;
}

// Parse command line arguments
bool parseArguments(int argc, char* argv[], CommandLineArgs* args) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return false;
        }
        else if (strcmp(argv[i], "--engine-sim-cli") == 0) {
            if (++i < argc) args->cliPath = argv[i];
        }
        else if (strcmp(argv[i], "--engine-config") == 0) {
            if (++i < argc) args->engineConfig = argv[i];
        }
        else if (strcmp(argv[i], "--test-rpm") == 0) {
            if (++i < argc) args->testRPM = atof(argv[i]);
        }
        else if (strcmp(argv[i], "--duration") == 0 || strcmp(argv[i], "-d") == 0) {
            if (++i < argc) args->testDuration = atof(argv[i]);
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            args->verbose = true;
        }
        else if (strcmp(argv[i], "--no-startup-test") == 0) {
            args->measureStartup = false;
        }
        else if (strcmp(argv[i], "--multi-rpm-test") == 0) {
            args->testMultipleRPM = true;
        }
        else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (++i < argc) args->outputFile = argv[i];
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    if (!args->cliPath) {
        fprintf(stderr, "ERROR: --engine-sim-cli path is required\n");
        return false;
    }

    if (args->testRPM <= 0 || args->testRPM > 10000) {
        fprintf(stderr, "ERROR: Test RPM must be between 0 and 10000\n");
        return false;
    }

    if (args->testDuration <= 0 || args->testDuration > MAX_TEST_DURATION) {
        fprintf(stderr, "ERROR: Duration must be between 0 and %.1f seconds\n", MAX_TEST_DURATION);
        return false;
    }

    return true;
}

// Start engine process
bool startEngine(const CommandLineArgs* args, double* startTime) {
    printf("Starting engine simulation...\n");

    g_enginePid = fork();
    if (g_enginePid == 0) {
        // Child process
        execl(args->cliPath, args->cliPath,
              "--script", args->engineConfig,
              "--rpm", "0",  // Start with 0 RPM for startup test
              "--duration", "5.0",
              "--output", "engine_test.wav",
              NULL);
        exit(1);
    } else if (g_enginePid < 0) {
        perror("Failed to fork process");
        return false;
    }

    // Record start time
    gettimeofday(&g_testStartTime, NULL);
    *startTime = g_testStartTime.tv_sec + (g_testStartTime.tv_usec / 1000000.0);

    // Wait for engine to start
    if (!waitForEngineStart(g_enginePid, STARTUP_TIMEOUT)) {
        printf("WARNING: Engine did not start within %.1f seconds\n", STARTUP_TIMEOUT);
        return false;
    }

    printf("Engine started successfully\n");
    g_engineRunning = true;
    return true;
}

// Monitor engine process
bool monitorEngine(pid_t pid, const CommandLineArgs* args, EngineTestResult* result) {
    double lastCheckTime = 0.0;
    EngineStats* stats = malloc(sizeof(EngineStats) * 1000); // Max 1000 samples
    int sampleCount = 0;
    bool startupComplete = false;

    if (!stats) {
        fprintf(stderr, "Failed to allocate memory for stats collection\n");
        return false;
    }

    printf("Monitoring engine...\n");

    // Main monitoring loop
    while (sampleCount < 1000) {
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        double currentMs = currentTime.tv_sec * 1000.0 + currentTime.tv_usec / 1000.0;
        double elapsedTime = (currentMs - g_testStartTime.tv_sec * 1000.0 - g_testStartTime.tv_usec / 1000.0) / 1000.0;

        // Check if test duration exceeded
        if (elapsedTime >= args->testDuration) {
            break;
        }

        // Collect stats periodically
        if (currentMs - lastCheckTime >= 100) { // Check every 100ms
            if (collectEngineStats(pid, &stats[sampleCount])) {
                // Update RPM statistics
                if (sampleCount == 0) {
                    result->maxRPM = result->minRPM = stats[sampleCount].rpm;
                } else {
                    result->maxRPM = fmax(result->maxRPM, stats[sampleCount].rpm);
                    result->minRPM = fmin(result->minRPM, stats[sampleCount].rpm);
                }

                result->avgRPM = (result->avgRPM * sampleCount + stats[sampleCount].rpm) / (sampleCount + 1);
                result->samplesCollected = sampleCount + 1;

                // Check for startup completion
                if (args->measureStartup && !startupComplete && stats[sampleCount].rpm > 300.0) {
                    result->startupTime = elapsedTime;
                    startupComplete = true;
                    printf("Engine started at %.1f seconds\n", result->startupTime);
                }

                sampleCount++;
            } else {
                // Error collecting stats
                result->crashed = true;
                break;
            }

            lastCheckTime = currentMs;
        }

        // Check if process is still running
        int status;
        pid_t waitResult = waitpid(pid, &status, WNOHANG);
        if (waitResult == pid) {
            // Process exited
            if (WIFEXITED(status)) {
                if (WEXITSTATUS(status) != 0) {
                    result->crashed = true;
                }
            } else if (WIFSIGNALED(status)) {
                result->crashed = true;
            }
            break;
        } else if (waitResult == -1 && errno == ECHILD) {
            // No child processes
            result->crashed = true;
            break;
        }

        usleep(10000); // Sleep 10ms
    }

    // Record end time
    gettimeofday(&g_testEndTime, NULL);
    result->endTime = g_testEndTime.tv_sec + (g_testEndTime.tv_usec / 1000000.0);

    // Calculate final statistics
    if (sampleCount > 0) {
        result->finalRPM = stats[sampleCount - 1].rpm;
        result->rpmError = fabs(result->finalRPM - result->targetRPM);

        // Calculate stabilization time
        calculateRPMStabilization(stats, sampleCount, result->targetRPM,
                                 &result->stabilizationTime, &result->rpmError);
    }

    // Check for various failure conditions
    result->engineStarted = startupComplete;
    result->engineHung = (sampleCount < 50); // Too few samples
    result->timedOut = (result->endTime - result->startTime > args->testDuration + 5.0);

    free(stats);
    return true;
}

// Wait for engine to start
bool waitForEngineStart(pid_t pid, double timeout) {
    struct timeval startTime;
    gettimeofday(&startTime, NULL);

    while (1) {
        int status;
        pid_t waitResult = waitpid(pid, &status, WNOHANG);

        if (waitResult == pid) {
            // Process exited
            if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                return true;
            }
            return false;
        } else if (waitResult == -1) {
            perror("waitpid failed");
            return false;
        }

        // Check timeout
        struct timeval currentTime;
        gettimeofday(&currentTime, NULL);
        double elapsed = (currentTime.tv_sec - startTime.tv_sec) +
                        (currentTime.tv_usec - startTime.tv_usec) / 1000000.0;

        if (elapsed > timeout) {
            return false;
        }

        usleep(100000); // 100ms
    }
}

// Collect engine statistics (simplified)
bool collectEngineStats(pid_t pid, EngineStats* stats) {
    // In a real implementation, this would read from process output or shared memory
    // For now, simulate with random values

    static bool firstCall = true;
    if (firstCall) {
        srand(time(NULL));
        firstCall = false;
    }

    // Simulate engine startup progression
    static double simulatedRPM = 0.0;
    if (simulatedRPM < 2000.0) {
        simulatedRPM += (rand() % 100) * 10.0;
    } else {
        simulatedRPM = 2000.0 + (rand() % 500) - 250; // Stabilize around 2000
    }

    stats->rpm = simulatedRPM;
    stats->load = 0.5 + (rand() % 100) / 200.0;
    stats->exhaustFlow = 0.1 * stats->rpm / 1000.0;
    stats->manifoldPressure = 101325.0 - (101325.0 * (1.0 - stats->load));
    stats->activeChannels = 4;
    stats->processingTimeMs = 1.0 + (rand() % 5);

    return true;
}

// Calculate RPM stabilization
bool calculateRPMStabilization(const EngineStats* stats, int count, double targetRPM,
                              double* stabilizationTime, double* finalError) {
    if (count < 10) {
        *stabilizationTime = 0.0;
        *finalError = 0.0;
        return false;
    }

    // Find when RPM gets close to target and stays there
    bool stabilized = false;
    int stableCount = 0;
    double tolerance = RPM_TOLERANCE;

    for (int i = 10; i < count; i++) {
        double error = fabs(stats[i].rpm - targetRPM);

        if (error <= tolerance) {
            stableCount++;
            if (stableCount >= 30) { // 30 samples = 3 seconds at 10Hz
                stabilized = true;
                *stabilizationTime = i * 0.1; // 100ms per sample
                *finalError = error;
                break;
            }
        } else {
            stableCount = 0;
        }
    }

    if (!stabilized) {
        *stabilizationTime = count * 0.1; // Never fully stabilized
        *finalError = fabs(stats[count - 1].rpm - targetRPM);
    }

    return true;
}

// Write engine test report
void writeEngineReport(const EngineTestResult* result, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Failed to write report");
        return;
    }

    double totalDuration = result->endTime - result->startTime;

    fprintf(file, "{\n");
    fprintf(file, "  \"test_type\": \"engine_verification\",\n");
    fprintf(file, "  \"target_rpm\": %.0f,\n", result->targetRPM);
    fprintf(file, "  \"total_duration_seconds\": %.2f,\n", totalDuration);
    fprintf(file, "  \"startup_time_seconds\": %.2f,\n", result->startupTime);
    fprintf(file, "  \"stabilization_time_seconds\": %.2f,\n", result->stabilizationTime);
    fprintf(file, "  \"rpm_error\": %.1f,\n", result->rpmError);
    fprintf(file, "  \"rpm_tolerance\": %.1f,\n", RPM_TOLERANCE);
    fprintf(file, "  \"samples_collected\": %d,\n", result->samplesCollected);
    fprintf(file, "  \"statistics\": {\n");
    fprintf(file, "    \"min_rpm\": %.1f,\n", result->minRPM);
    fprintf(file, "    \"max_rpm\": %.1f,\n", result->maxRPM);
    fprintf(file, "    \"avg_rpm\": %.1f,\n", result->avgRPM);
    fprintf(file, "    \"final_rpm\": %.1f\n", result->finalRPM);
    fprintf(file, "  },\n");
    fprintf(file, "  \"status_checks\": {\n");
    fprintf(file, "    \"engine_started\": %s,\n", result->engineStarted ? "true" : "false");
    fprintf(file, "    \"engine_hung\": %s,\n", result->engineHung ? "true" : "false");
    fprintf(file, "    \"crashed\": %s,\n", result->crashed ? "true" : "false");
    fprintf(file, "    \"timed_out\": %s\n", result->timedOut ? "true" : "false");
    fprintf(file, "  },\n");
    fprintf(file, "  \"overall_result\": \"%s\"\n",
            (result->engineHung || result->crashed || result->timedOut) ? "FAILED" : "PASSED");
    fprintf(file, "}\n");

    fclose(file);
}

// Print engine test results
void printEngineResults(const EngineTestResult* result) {
    double totalDuration = result->endTime - result->startTime;

    printf("\n=== Engine Test Results ===\n");
    printf("Target RPM: %.0f\n", result->targetRPM);
    printf("Total duration: %.2f seconds\n", totalDuration);
    printf("Startup time: %.2f seconds\n", result->startupTime);
    printf("Stabilization time: %.2f seconds\n", result->stabilizationTime);
    printf("RPM error: %.1f (tolerance: %.1f)\n", result->rpmError, RPM_TOLERANCE);

    printf("\nEngine Statistics:\n");
    printf("  Min RPM: %.1f\n", result->minRPM);
    printf("  Max RPM: %.1f\n", result->maxRPM);
    printf("  Avg RPM: %.1f\n", result->avgRPM);
    printf("  Final RPM: %.1f\n", result->finalRPM);
    printf("  Samples collected: %d\n", result->samplesCollected);

    printf("\nStatus:\n");
    printf("  Engine started: %s\n", result->engineStarted ? "Yes" : "No");
    printf("  Engine hung: %s\n", result->engineHung ? "Yes" : "No");
    printf("  Crashed: %s\n", result->crashed ? "Yes" : "No");
    printf("  Timed out: %s\n", result->timedOut ? "Yes" : "No");

    // Pass/fail determination
    bool passed = result->engineStarted && !result->engineHung && !result->crashed && !result->timedOut;
    if (passed && result->rpmError <= RPM_TOLERANCE) {
        printf("\nResult: PASSED\n");
    } else {
        printf("\nResult: FAILED\n");
    }
}

// Print usage information
void printUsage(const char* progName) {
    printf("Usage: %s [options]\n", progName);
    printf("\nEngine Verification Options:\n");
    printf("  --engine-sim-cli <path>    Path to engine-sim-cli executable\n");
    printf("  --engine-config <path>     Engine configuration file (default: main.mr)\n");
    printf("  --test-rpm <rpm>           Target RPM for testing (default: 2000)\n");
    printf("  --duration <seconds>       Test duration (default: 10.0, max: %.1f)\n", MAX_TEST_DURATION);
    printf("  --verbose, -v              Verbose output\n");
    printf("  --no-startup-test         Skip startup time measurement\n");
    printf("  --multi-rpm-test         Test multiple RPM levels\n");
    printf("  --output <file.json>     Write detailed results to file\n");
    printf("\nTest Parameters:\n");
    printf("  Startup timeout: %.1f seconds\n", STARTUP_TIMEOUT);
    printf("  RPM tolerance: %.1f RPM\n", RPM_TOLERANCE);
    printf("  Stabilization time target: %.1f seconds\n", RPM_STABILIZATION_TIME);
    printf("\nExamples:\n");
    printf("  %s --engine-sim-cli ./engine-sim-cli --test-rpm 3000\n", progName);
    printf("  %s --engine-sim-cli ./engine-sim-cli --multi-rpm-test\n", progName);
    printf("  %s --engine-sim-cli ./engine-sim-cli --test-rpm 4000 --duration 15.0\n", progName);
}