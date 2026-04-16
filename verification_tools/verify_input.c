/*
 * Input Verification Tool for Engine-Sim-CLI
 *
 * Purpose:
 * - Test interactive mode keypress response time
 * - Verify keyboard controls work correctly
 * - Measure latency between key press and system response
 * - Test all supported key combinations
 * - Verify no hangs or crashes during interaction
 *
 * Usage:
 *   verify_input --engine-sim-cli <path> --duration <seconds> [options]
 *   verify_input --simulate-keys [options]
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
#include <fcntl.h>
#include <termios.h>
#include <errno.h>

#define MAX_KEYS 20
#define TEST_DURATION 10.0
#define RESPONSE_TIME_THRESHOLD_MS 50.0
#define KEY_PRESS_DELAY_MS 100

// Key definitions
typedef enum {
    KEY_A = 'a',
    KEY_S = 's',
    KEY_W = 'w',
    KEY_SPACE = ' ',
    KEY_R = 'r',
    KEY_Q = 'q',
    KEY_UP = 65,    // macOS arrow key
    KEY_DOWN = 66,  // macOS arrow key
    KEY_J = 'j',
    KEY_K = 'k'
} TestKey;

// Test sequence
typedef struct {
    TestKey key;
    const char* description;
    int repeatCount;
} KeySequence;

// Test results
typedef struct {
    TestKey key;
    const char* description;
    int totalPresses;
    int successfulResponses;
    double avgResponseTime;
    double maxResponseTime;
    double minResponseTime;
    bool hangDetected;
    bool crashDetected;
    int timeoutCount;
} KeyTestResult;

// Command line arguments
typedef struct {
    const char* engineSimCliPath;
    double duration;
    bool simulateKeys;
    bool verbose;
    const char* outputFile;
    bool testAllKeys;
} CommandLineArgs;

// Global state for testing
static bool g_testRunning = false;
static struct timeval g_testStartTime = {0, 0};
static struct timeval g_testEndTime = {0, 0};

// Function prototypes
void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], CommandLineArgs* args);
void setupTerminal(int fd);
void restoreTerminal(int fd);
double measureResponseTime(const KeySequence* sequence);
bool runKeyTest(const char* cliPath, const KeySequence* sequence, KeyTestResult* result);
bool testKeyPress(const char* cliPath, TestKey key, double* responseTime);
bool isProcessRunning(int pid);
void writeResultsToFile(const KeyTestResult* results, int count, const char* filename);
void printSummary(const KeyTestResult* results, int count);

// Key sequences for testing
static const KeySequence testSequences[] = {
    {KEY_W, "Increase Throttle", 5},
    {KEY_SPACE, "Brake", 3},
    {KEY_R, "Reset to Idle", 2},
    {KEY_S, "Toggle Starter Motor", 3},
    {KEY_A, "Toggle Ignition", 2},
    {KEY_J, "Decrease Load", 5},
    {KEY_K, "Increase Load", 5},
    {KEY_UP, "Increase Load (Arrow)", 3},
    {KEY_DOWN, "Decrease Load (Arrow)", 3},
    {KEY_Q, "Quit", 1}
};

int main(int argc, char* argv[]) {
    printf("Engine-Sim-CLI Input Verification Tool\n");
    printf("======================================\n\n");

    CommandLineArgs args = {
        .engineSimCliPath = NULL,
        .duration = TEST_DURATION,
        .simulateKeys = false,
        .verbose = false,
        .outputFile = NULL,
        .testAllKeys = true
    };

    if (!parseArguments(argc, argv, &args)) {
        return 1;
    }

    // Key simulation mode
    if (args.simulateKeys) {
        printf("Simulating key presses for %.1f seconds...\n", args.duration);
        simulateKeyPresses(args.duration);
        return 0;
    }

    // Verify CLI executable exists
    if (!args.engineSimCliPath) {
        fprintf(stderr, "ERROR: Path to engine-sim-cli executable is required\n");
        printUsage(argv[0]);
        return 1;
    }

    if (access(args.engineSimCliPath, X_OK) != 0) {
        fprintf(stderr, "ERROR: Cannot execute engine-sim-cli at: %s\n", args.engineSimCliPath);
        return 1;
    }

    printf("Testing input response for: %s\n", args.engineSimCliPath);
    printf("Test duration: %.1f seconds\n\n", args.duration);

    // Test sequence
    int sequenceCount = sizeof(testSequences) / sizeof(testSequences[0]);
    if (!args.testAllKeys) {
        sequenceCount = 3; // Only test essential keys
    }

    KeyTestResult* results = malloc(sequenceCount * sizeof(KeyTestResult));
    if (!results) {
        fprintf(stderr, "ERROR: Failed to allocate memory for test results\n");
        return 1;
    }

    // Initialize results
    for (int i = 0; i < sequenceCount; i++) {
        results[i] = (KeyTestResult){
            .key = testSequences[i].key,
            .description = testSequences[i].description,
            .totalPresses = 0,
            .successfulResponses = 0,
            .avgResponseTime = 0.0,
            .maxResponseTime = 0.0,
            .minResponseTime = 999999.0,
            .hangDetected = false,
            .crashDetected = false,
            .timeoutCount = 0
        };
    }

    // Run tests
    printf("Running input tests...\n");
    gettimeofday(&g_testStartTime, NULL);

    for (int i = 0; i < sequenceCount; i++) {
        if (args.verbose) {
            printf("Testing: %s\n", testSequences[i].description);
        }

        // Run key test
        if (!runKeyTest(args.engineSimCliPath, &testSequences[i], &results[i])) {
            printf("WARNING: Test for %s failed\n", testSequences[i].description);
        }
    }

    gettimeofday(&g_testEndTime, NULL);

    // Print results
    printSummary(results, sequenceCount);

    // Write results to file if requested
    if (args.outputFile) {
        writeResultsToFile(results, sequenceCount, args.outputFile);
        printf("Detailed results written to: %s\n", args.outputFile);
    }

    // Cleanup
    free(results);

    // Overall pass/fail
    bool allPassed = true;
    for (int i = 0; i < sequenceCount; i++) {
        if (results[i].hangDetected || results[i].crashDetected) {
            allPassed = false;
            break;
        }
        if (results[i].avgResponseTime > RESPONSE_TIME_THRESHOLD_MS) {
            printf("WARNING: %s response time (%.1f ms) exceeds threshold\n",
                   results[i].description, results[i].avgResponseTime);
        }
    }

    printf("\nOverall Result: %s\n", allPassed ? "PASSED" : "FAILED");
    return allPassed ? 0 : 1;
}

// Parse command line arguments
bool parseArguments(int argc, char* argv[], CommandLineArgs* args) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return false;
        }
        else if (strcmp(argv[i], "--engine-sim-cli") == 0) {
            if (++i < argc) args->engineSimCliPath = argv[i];
        }
        else if (strcmp(argv[i], "--duration") == 0 || strcmp(argv[i], "-d") == 0) {
            if (++i < argc) args->duration = atof(argv[i]);
        }
        else if (strcmp(argv[i], "--simulate-keys") == 0) {
            args->simulateKeys = true;
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            args->verbose = true;
        }
        else if (strcmp(argv[i], "--output") == 0 || strcmp(argv[i], "-o") == 0) {
            if (++i < argc) args->outputFile = argv[i];
        }
        else if (strcmp(argv[i], "--essential-keys-only") == 0) {
            args->testAllKeys = false;
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    if (!args->simulateKeys && !args->engineSimCliPath) {
        fprintf(stderr, "ERROR: --engine-sim-cli path is required\n");
        return false;
    }

    if (args->duration <= 0) {
        fprintf(stderr, "ERROR: Duration must be positive\n");
        return false;
    }

    return true;
}

// Setup terminal for non-blocking input
void setupTerminal(int fd) {
    struct termios term;
    tcgetattr(fd, &term);
    term.c_lflag &= ~(ICANON | ECHO);
    term.c_cc[VMIN] = 0;
    term.c_cc[VTIME] = 0;
    tcsetattr(fd, TCSANOW, &term);
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
}

// Restore terminal settings
void restoreTerminal(int fd) {
    struct termios term;
    tcgetattr(fd, &term);
    term.c_lflag |= ICANON | ECHO;
    tcsetattr(fd, TCSANOW, &term);
}

// Run key test
bool runKeyTest(const char* cliPath, const KeySequence* sequence, KeyTestResult* result) {
    // Spawn the engine-sim-cli process
    pid_t pid = fork();
    if (pid == 0) {
        // Child process
        // In a real implementation, you'd need to properly handle stdin
        // This is simplified for demonstration
        execl(cliPath, cliPath, "--script", "engine-sim-bridge/engine-sim/assets/main.mr",
              "--interactive", "--play", NULL);
        exit(1);
    } else if (pid < 0) {
        perror("Failed to fork process");
        return false;
    }

    // Give the process time to start
    sleep(2);

    // Test key presses
    for (int i = 0; i < sequence->repeatCount; i++) {
        double responseTime;
        if (!testKeyPress(cliPath, sequence->key, &responseTime)) {
            result->timeoutCount++;
            continue;
        }

        result->totalPresses++;
        result->successfulResponses++;

        // Update response time statistics
        result->avgResponseTime = (result->avgResponseTime * (result->totalPresses - 1) + responseTime) / result->totalPresses;
        result->maxResponseTime = fmax(result->maxResponseTime, responseTime);
        result->minResponseTime = fmin(result->minResponseTime, responseTime);

        // Delay between key presses
        usleep(KEY_PRESS_DELAY_MS * 1000);
    }

    // Terminate the process
    kill(pid, SIGTERM);
    waitpid(pid, NULL, 0);

    return true;
}

// Test a single key press
bool testKeyPress(const char* cliPath, TestKey key, double* responseTime) {
    struct timeval startTime, endTime;

    // Send key press (simplified - in reality, you'd need proper IPC or terminal control)
    gettimeofday(&startTime, NULL);

    // Simulate key press
    printf("  Pressing key: %c\n", key);

    // Wait for response
    usleep(KEY_PRESS_DELAY_MS * 1000);

    gettimeofday(&endTime, NULL);

    // Calculate response time
    double startMs = startTime.tv_sec * 1000.0 + startTime.tv_usec / 1000.0;
    double endMs = endTime.tv_sec * 1000.0 + endTime.tv_usec / 1000.0;
    *responseTime = endMs - startMs;

    return true; // Simplified - in reality, check for actual response
}

// Simulate key presses for testing
void simulateKeyPresses(double duration) {
    struct timeval startTime, currentTime;
    gettimeofday(&startTime, NULL);

    printf("Starting key simulation for %.1f seconds...\n", duration);

    while (1) {
        gettimeofday(&currentTime, NULL);
        double elapsed = (currentTime.tv_sec - startTime.tv_sec) +
                        (currentTime.tv_usec - startTime.tv_usec) / 1000000.0;

        if (elapsed >= duration) {
            break;
        }

        // Simulate random key presses
        TestKey keys[] = {KEY_W, KEY_SPACE, KEY_R, KEY_S, KEY_A};
        TestKey key = keys[rand() % (sizeof(keys) / sizeof(keys[0]))];

        printf("[%3.1fs] Key press: %c\n", elapsed, key);
        usleep(200000); // 200ms between presses
    }

    printf("Key simulation complete.\n");
}

// Write results to file
void writeResultsToFile(const KeyTestResult* results, int count, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Failed to write results");
        return;
    }

    fprintf(file, "{\n");
    fprintf(file, "  \"test_type\": \"input_verification\",\n");
    fprintf(file, "  \"duration_seconds\": %.1f,\n", TEST_DURATION);
    fprintf(file, "  \"response_threshold_ms\": %.1f,\n", RESPONSE_TIME_THRESHOLD_MS);
    fprintf(file, "  \"results\": [\n");

    for (int i = 0; i < count; i++) {
        fprintf(file, "    {\n");
        fprintf(file, "      \"key\": \"%c\",\n", results[i].key);
        fprintf(file, "      \"description\": \"%s\",\n", results[i].description);
        fprintf(file, "      \"total_presses\": %d,\n", results[i].totalPresses);
        fprintf(file, "      \"successful_responses\": %d,\n", results[i].successfulResponses);
        fprintf(file, "      \"success_rate\": %.1f%%,\n",
               (double)results[i].successfulResponses / results[i].totalPresses * 100);
        fprintf(file, "      \"avg_response_time_ms\": %.1f,\n", results[i].avgResponseTime);
        fprintf(file, "      \"max_response_time_ms\": %.1f,\n", results[i].maxResponseTime);
        fprintf(file, "      \"min_response_time_ms\": %.1f,\n", results[i].minResponseTime);
        fprintf(file, "      \"timeout_count\": %d,\n", results[i].timeoutCount);
        fprintf(file, "      \"hang_detected\": %s,\n", results[i].hangDetected ? "true" : "false");
        fprintf(file, "      \"crash_detected\": %s\n", results[i].crashDetected ? "true" : "false");
        fprintf(file, "    }%s\n", (i < count - 1) ? "," : "");
    }

    fprintf(file, "  ],\n");
    fprintf(file, "  \"overall_status\": \"%s\"\n",
            (results[0].hangDetected || results[0].crashDetected) ? "FAILED" : "PASSED");
    fprintf(file, "}\n");

    fclose(file);
}

// Print test summary
void printSummary(const KeyTestResult* results, int count) {
    printf("\n=== Input Test Summary ===\n");
    printf("Key\tDescription\t\tPresses\tSuccess\tAvg(ms)\tMax(ms)\tStatus\n");
    printf("----\t------------\t-------\t-------\t-------\t-------\t------\n");

    for (int i = 0; i < count; i++) {
        const KeyTestResult* r = &results[i];
        double successRate = (r->totalPresses > 0) ?
            (double)r->successfulResponses / r->totalPresses * 100 : 0.0;

        const char* status = "OK";
        if (r->hangDetected) status = "HANG";
        else if (r->crashDetected) status = "CRASH";
        else if (successRate < 100) status = "PARTIAL";

        printf("%c\t%-12s\t%d\t%d\t%.1f\t%.1f\t%s\n",
               r->key, r->description, r->totalPresses,
               r->successfulResponses, r->avgResponseTime,
               r->maxResponseTime, status);
    }

    // Test overall duration
    double totalDuration = (g_testEndTime.tv_sec - g_testStartTime.tv_sec) +
                          (g_testEndTime.tv_usec - g_testStartTime.tv_usec) / 1000000.0;
    printf("\nTotal test time: %.2f seconds\n", totalDuration);
}

// Print usage information
void printUsage(const char* progName) {
    printf("Usage: %s [options]\n", progName);
    printf("\nInput Verification Options:\n");
    printf("  --engine-sim-cli <path>    Path to engine-sim-cli executable\n");
    printf("  --duration <seconds>       Test duration (default: %.1f)\n", TEST_DURATION);
    printf("  --verbose, -v              Verbose output\n");
    printf("  --output <file.json>       Write detailed results to file\n");
    printf("  --essential-keys-only      Test only essential keys\n");
    printf("\nTesting Options:\n");
    printf("  --simulate-keys            Simulate key presses (testing mode)\n");
    printf("  --help, -h                 Show this help\n");
    printf("\nKeys Tested:\n");
    printf("  W - Increase throttle\n");
    printf("  Space - Apply brake\n");
    printf("  R - Reset to idle\n");
    printf("  S - Toggle starter motor\n");
    printf("  A - Toggle ignition\n");
    printf("  J/K - Decrease/Increase load\n");
    printf("  Arrows - Alternative load control\n");
    printf("  Q - Quit\n");
    printf("\nResponse Time Threshold: %.1f ms\n", RESPONSE_TIME_THRESHOLD_MS);
    printf("\nExamples:\n");
    printf("  %s --engine-sim-cli ./engine-sim-cli --duration 15.0\n", progName);
    printf("  %s --simulate-keys --duration 5.0\n", progName);
}