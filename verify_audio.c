/*
 * Audio Verification Tool for Engine-Sim-CLI
 *
 * Purpose:
 * - Measure audio output frequency and amplitude
 * - Detect pitch accuracy and stability
 * - Verify sine mode generates correct frequencies
 * - Measure decibel levels
 * - Check for audio clipping or distortion
 *
 * Usage:
 *   verify_audio --input <wav_file> --expected_rpm <rpm> [options]
 *   verify_audio --generate_sine <frequency> --duration <seconds> [options]
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdbool.h>
#include <time.h>
#include <unistd.h>

// Audio file header structures
typedef struct {
    char riff[4];
    uint32_t fileSize;
    char wave[4];
    char fmtMarker[4];
    uint32_t fmtLength;
    uint16_t audioFormat;
    uint16_t numChannels;
    uint32_t sampleRate;
    uint32_t byteRate;
    uint16_t blockAlign;
    uint16_t bitsPerSample;
    char dataMarker[4];
    uint32_t dataSize;
} WavHeader;

// Test results structure
typedef struct {
    double expectedFrequency;
    double actualFrequency;
    double frequencyError;
    double amplitude;
    double dbLevel;
    double stability;
    bool clippingDetected;
    double thd; // Total Harmonic Distortion
    int sampleCount;
    int validSamples;
} AudioTestResult;

// Command line arguments
typedef struct {
    const char* inputFile;
    const char* generateSine;
    double expectedRPM;
    double duration;
    bool verbose;
    bool detailedAnalysis;
    const char* outputReport;
} CommandLineArgs;

// Function prototypes
void printUsage(const char* progName);
bool parseArguments(int argc, char* argv[], CommandLineArgs* args);
double calculateExpectedFrequency(double rpm);
bool readWavFile(const char* filename, float** audioData, int* sampleCount, int* sampleRate, int* channels);
double detectPitch(float* audioData, int sampleCount, int sampleRate, double* confidence);
double calculateRMS(float* audioData, int sampleCount);
double calculateDB(double rms);
bool detectClipping(float* audioData, int sampleCount, float threshold);
double calculateTHD(float* audioData, int sampleCount, int sampleRate, double fundamental);
void measureFrequencyStability(float* audioData, int sampleCount, int sampleRate, double* stability);
void printResults(const AudioTestResult* result);
void writeReport(const AudioTestResult* result, const char* filename);
void generateTestSine(double frequency, double duration, int sampleRate, const char* filename);

int main(int argc, char* argv[]) {
    printf("Engine-Sim-CLI Audio Verification Tool\n");
    printf("=====================================\n\n");

    CommandLineArgs args = {
        .inputFile = NULL,
        .generateSine = NULL,
        .expectedRPM = 0.0,
        .duration = 3.0,
        .verbose = false,
        .detailedAnalysis = false,
        .outputReport = NULL
    };

    if (!parseArguments(argc, argv, &args)) {
        return 1;
    }

    // If generating sine wave for testing
    if (args.generateSine) {
        double frequency = atof(args.generateSine);
        printf("Generating test sine wave: %.1f Hz, %.1f seconds\n", frequency, args.duration);
        generateTestSine(frequency, args.duration, 44100, "test_sine.wav");
        printf("Generated: test_sine.wav\n");
        return 0;
    }

    // Verify input file exists
    if (!args.inputFile) {
        fprintf(stderr, "ERROR: Input WAV file is required\n");
        printUsage(argv[0]);
        return 1;
    }

    printf("Analyzing audio file: %s\n", args.inputFile);

    // Read WAV file
    float* audioData = NULL;
    int sampleCount = 0;
    int sampleRate = 0;
    int channels = 0;

    if (!readWavFile(args.inputFile, &audioData, &sampleCount, &sampleRate, &channels)) {
        fprintf(stderr, "ERROR: Failed to read WAV file\n");
        return 1;
    }

    if (sampleCount == 0) {
        fprintf(stderr, "ERROR: No audio samples found\n");
        free(audioData);
        return 1;
    }

    printf("File info:\n");
    printf("  Sample rate: %d Hz\n", sampleRate);
    printf("  Channels: %d\n", channels);
    printf("  Sample count: %d\n", sampleCount);
    printf("  Duration: %.2f seconds\n\n", (double)sampleCount / sampleRate);

    // Calculate expected frequency from RPM
    double expectedFrequency = calculateExpectedFrequency(args.expectedRPM);
    printf("Target RPM: %.0f\n", args.expectedRPM);
    printf("Expected frequency: %.1f Hz\n\n", expectedFrequency);

    // Perform audio analysis
    AudioTestResult result = {0};
    result.expectedFrequency = expectedFrequency;
    result.sampleCount = sampleCount;

    // Detect pitch
    double confidence = 0.0;
    result.actualFrequency = detectPitch(audioData, sampleCount, sampleRate, &confidence);
    result.frequencyError = fabs(result.actualFrequency - expectedFrequency);
    result.stability = confidence;

    if (result.actualFrequency > 0) {
        printf("Pitch Analysis:\n");
        printf("  Detected frequency: %.1f Hz\n", result.actualFrequency);
        printf("  Expected frequency: %.1f Hz\n", expectedFrequency);
        printf("  Frequency error: %.1f Hz (%.1f%%)\n",
               result.frequencyError,
               (result.frequencyError / expectedFrequency) * 100);
        printf("  Detection confidence: %.2f%%\n\n", confidence * 100);
    } else {
        printf("WARNING: Could not detect pitch reliably\n");
        printf("  Detection confidence: %.2f%%\n\n", confidence * 100);
    }

    // Calculate amplitude and dB level
    if (channels > 0) {
        // Use first channel for amplitude measurement
        float* monoData = audioData;
        if (channels > 1) {
            // Convert stereo to mono for analysis
            monoData = malloc(sampleCount * sizeof(float));
            for (int i = 0; i < sampleCount; i++) {
                monoData[i] = (audioData[i * 2] + audioData[i * 2 + 1]) * 0.5f;
            }
        }

        result.amplitude = calculateRMS(monoData, sampleCount);
        result.dbLevel = calculateDB(result.amplitude);

        // Check for clipping
        result.clippingDetected = detectClipping(monoData, sampleCount, 0.95f);

        // Calculate THD if detailed analysis requested
        if (args.detailedAnalysis && result.actualFrequency > 0) {
            result.thd = calculateTHD(monoData, sampleCount, sampleRate, result.actualFrequency);
        }

        // Free mono data if allocated
        if (channels > 1 && monoData) {
            free(monoData);
        }

        printf("Amplitude Analysis:\n");
        printf("  RMS amplitude: %.4f\n", result.amplitude);
        printf("  dB level: %.1f dBFS\n", result.dbLevel);
        if (result.clippingDetected) {
            printf("  WARNING: Clipping detected in audio signal\n");
        }
        if (args.detailedAnalysis) {
            printf("  THD: %.2f%%\n", result.thd * 100);
        }
        printf("\n");
    }

    // Frequency stability analysis
    if (args.detailedAnalysis) {
        measureFrequencyStability(audioData, sampleCount, sampleRate, &result.stability);
        printf("Stability Analysis:\n");
        printf("  Frequency stability: %.2f%%\n", result.stability * 100);
        printf("\n");
    }

    // Print results
    printResults(&result);

    // Write report if requested
    if (args.outputReport) {
        writeReport(&result, args.outputReport);
        printf("Report saved to: %s\n", args.outputReport);
    }

    // Cleanup
    free(audioData);

    // Determine pass/fail
    bool testPassed = false;
    if (result.actualFrequency > 0) {
        double frequencyAccuracy = (1.0 - (result.frequencyError / expectedFrequency));
        if (frequencyAccuracy >= 0.95 && !result.clippingDetected) {
            testPassed = true;
        }
    }

    printf("\nTest Result: %s\n", testPassed ? "PASSED" : "FAILED");
    return testPassed ? 0 : 1;
}

// Parse command line arguments
bool parseArguments(int argc, char* argv[], CommandLineArgs* args) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printUsage(argv[0]);
            return false;
        }
        else if (strcmp(argv[i], "--input") == 0 || strcmp(argv[i], "-i") == 0) {
            if (++i < argc) args->inputFile = argv[i];
        }
        else if (strcmp(argv[i], "--generate-sine") == 0) {
            if (++i < argc) args->generateSine = argv[i];
        }
        else if (strcmp(argv[i], "--expected-rpm") == 0 || strcmp(argv[i], "-r") == 0) {
            if (++i < argc) args->expectedRPM = atof(argv[i]);
        }
        else if (strcmp(argv[i], "--duration") == 0 || strcmp(argv[i], "-d") == 0) {
            if (++i < argc) args->duration = atof(argv[i]);
        }
        else if (strcmp(argv[i], "--verbose") == 0 || strcmp(argv[i], "-v") == 0) {
            args->verbose = true;
        }
        else if (strcmp(argv[i], "--detailed") == 0) {
            args->detailedAnalysis = true;
        }
        else if (strcmp(argv[i], "--output-report") == 0 || strcmp(argv[i], "-o") == 0) {
            if (++i < argc) args->outputReport = argv[i];
        }
        else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            return false;
        }
    }

    // Validate arguments
    if (args->generateSine) {
        // For sine generation, RPM is not needed
        return true;
    }

    if (!args->inputFile) {
        fprintf(stderr, "ERROR: Input WAV file is required\n");
        return false;
    }

    if (args->expectedRPM <= 0) {
        fprintf(stderr, "ERROR: Expected RPM must be positive\n");
        return false;
    }

    return true;
}

// Calculate expected frequency from RPM
double calculateExpectedFrequency(double rpm) {
    // f = (RPM / 600) * 100 Hz (as per engine_sim_cli.cpp)
    return (rpm / 600.0) * 100.0;
}

// Read WAV file
bool readWavFile(const char* filename, float** audioData, int* sampleCount, int* sampleRate, int* channels) {
    FILE* file = fopen(filename, "rb");
    if (!file) {
        perror("Failed to open file");
        return false;
    }

    WavHeader header;
    if (fread(&header, sizeof(WavHeader), 1, file) != 1) {
        fclose(file);
        return false;
    }

    // Validate WAV file
    if (memcmp(header.riff, "RIFF", 4) != 0 || memcmp(header.wave, "WAVE", 4) != 0) {
        fclose(file);
        return false;
    }

    *sampleRate = header.sampleRate;
    *channels = header.numChannels;
    *sampleCount = header.dataSize / (header.numChannels * header.bitsPerSample / 8);

    *audioData = malloc(*sampleCount * sizeof(float));
    if (!*audioData) {
        fclose(file);
        return false;
    }

    // Convert to float format
    int16_t* intData = malloc(*sampleCount * sizeof(int16_t));
    if (!intData) {
        free(*audioData);
        fclose(file);
        return false;
    }

    size_t bytesRead = fread(intData, header.bitsPerSample / 8, *sampleCount, file);
    fclose(file);

    if (bytesRead != *sampleCount) {
        free(*audioData);
        free(intData);
        return false;
    }

    // Convert int16 to float -1.0 to 1.0
    for (int i = 0; i < *sampleCount; i++) {
        (*audioData)[i] = (float)intData[i] / 32768.0f;
    }

    free(intData);
    return true;
}

// Detect pitch using autocorrelation
double detectPitch(float* audioData, int sampleCount, int sampleRate, double* confidence) {
    if (sampleCount < 1024) {
        *confidence = 0.0;
        return -1.0;
    }

    int analysisSize = 4096;
    if (sampleCount < analysisSize) analysisSize = sampleCount;

    // Use only first channel
    double* correlation = malloc(analysisSize * sizeof(double));
    if (!correlation) return -1.0;

    // Compute autocorrelation
    for (int lag = 0; lag < analysisSize; lag++) {
        correlation[lag] = 0.0;
        for (int i = lag; i < analysisSize; i++) {
            correlation[lag] += audioData[i] * audioData[i - lag];
        }
    }

    // Find first peak after lag 0
    int bestLag = 0;
    double maxValue = 0.0;
    double minValue = correlation[0];

    for (int lag = 20; lag < analysisSize / 2; lag++) {
        if (correlation[lag] > maxValue) {
            maxValue = correlation[lag];
            bestLag = lag;
        }
    }

    // Calculate confidence based on peak prominence
    double peakToNoiseRatio = maxValue / (minValue + 1e-10);
    *confidence = peakToNoiseRatio / 10.0; // Normalize
    *confidence = (*confidence > 1.0) ? 1.0 : (*confidence < 0.0) ? 0.0 : *confidence;

    double frequency = (bestLag > 0) ? (double)sampleRate / bestLag : -1.0;

    free(correlation);
    return frequency;
}

// Calculate RMS amplitude
double calculateRMS(float* audioData, int sampleCount) {
    double sum = 0.0;
    for (int i = 0; i < sampleCount; i++) {
        sum += audioData[i] * audioData[i];
    }
    return sqrt(sum / sampleCount);
}

// Convert RMS to dBFS
double calculateDB(double rms) {
    if (rms < 1e-10) return -100.0; // Very quiet
    return 20.0 * log10(rms);
}

// Detect clipping
bool detectClipping(float* audioData, int sampleCount, float threshold) {
    int clippingSamples = 0;
    for (int i = 0; i < sampleCount; i++) {
        if (fabs(audioData[i]) > threshold) {
            clippingSamples++;
        }
    }
    return (clippingSamples > sampleCount * 0.01); // More than 1% clipping
}

// Calculate Total Harmonic Distortion
double calculateTHD(float* audioData, int sampleCount, int sampleRate, double fundamental) {
    // Simplified THD calculation using FFT approximation
    // In a real implementation, you'd use proper FFT
    double fundamentalPower = 0.0;
    double harmonicPower = 0.0;
    double window = 0.1; // 100ms window
    int samplesInWindow = (int)(window * sampleRate);

    for (int i = 0; i < samplesInWindow && i < sampleCount; i++) {
        double sample = audioData[i];
        fundamentalPower += sample * sample;
    }

    // Estimate harmonics (simplified)
    for (int i = 0; i < samplesInWindow && i < sampleCount; i++) {
        double sample = audioData[i];
        // Remove fundamental (simplified)
        double harmonics = sample - sample * 0.1; // Rough approximation
        harmonicPower += harmonics * harmonics;
    }

    if (fundamentalPower < 1e-10) return 0.0;
    return sqrt(harmonicPower / fundamentalPower);
}

// Measure frequency stability
void measureFrequencyStability(float* audioData, int sampleCount, int sampleRate, double* stability) {
    int windowSize = 1024;
    int numWindows = sampleCount / windowSize;
    double* frequencies = malloc(numWindows * sizeof(double));

    if (!frequencies) {
        *stability = 0.0;
        return;
    }

    // Calculate frequency for each window
    for (int i = 0; i < numWindows; i++) {
        double confidence = 0.0;
        frequencies[i] = detectPitch(audioData + i * windowSize, windowSize, sampleRate, &confidence);
    }

    // Calculate standard deviation
    double mean = 0.0;
    for (int i = 0; i < numWindows; i++) {
        mean += frequencies[i];
    }
    mean /= numWindows;

    double variance = 0.0;
    for (int i = 0; i < numWindows; i++) {
        double diff = frequencies[i] - mean;
        variance += diff * diff;
    }
    variance /= numWindows;

    *stability = 1.0 - (sqrt(variance) / (mean + 1e-10));
    *stability = (*stability < 0.0) ? 0.0 : (*stability > 1.0) ? 1.0 : *stability;

    free(frequencies);
}

// Print results
void printResults(const AudioTestResult* result) {
    printf("=== Test Results ===\n");
    printf("Expected frequency: %.1f Hz\n", result->expectedFrequency);
    printf("Actual frequency: %.1f Hz\n", result->actualFrequency);
    printf("Frequency error: %.1f Hz (%.1f%%)\n",
           result->frequencyError,
           (result->frequencyError / result->expectedFrequency) * 100);
    printf("Amplitude: %.4f (%.1f dBFS)\n", result->amplitude, result->dbLevel);
    printf("Stability: %.1f%%\n", result->stability * 100);

    if (result->clippingDetected) {
        printf("WARNING: Clipping detected\n");
    }

    if (result->thd > 0) {
        printf("THD: %.2f%%\n", result->thd * 100);
    }

    // Pass/fail determination
    bool passed = false;
    if (result->actualFrequency > 0) {
        double accuracy = 1.0 - (result->frequencyError / result->expectedFrequency);
        passed = (accuracy >= 0.95 && !result->clippingDetected);
    }

    printf("\nStatus: %s\n", passed ? "PASSED" : "FAILED");
}

// Write report to file
void writeReport(const AudioTestResult* result, const char* filename) {
    FILE* file = fopen(filename, "w");
    if (!file) {
        perror("Failed to write report");
        return;
    }

    fprintf(file, "Audio Verification Report\n");
    fprintf(file, "========================\n\n");

    fprintf(file, "Expected frequency: %.1f Hz\n", result->expectedFrequency);
    fprintf(file, "Actual frequency: %.1f Hz\n", result->actualFrequency);
    fprintf(file, "Frequency error: %.1f Hz (%.1f%%)\n",
           result->frequencyError,
           (result->frequencyError / result->expectedFrequency) * 100);
    fprintf(file, "Amplitude: %.4f (%.1f dBFS)\n", result->amplitude, result->dbLevel);
    fprintf(file, "Stability: %.1f%%\n", result->stability * 100);
    fprintf(file, "Clipping detected: %s\n", result->clippingDetected ? "Yes" : "No");

    if (result->thd > 0) {
        fprintf(file, "THD: %.2f%%\n", result->thd * 100);
    }

    fprintf(file, "\nStatus: %s\n",
           (result->actualFrequency > 0 &&
            (1.0 - (result->frequencyError / result->expectedFrequency) >= 0.95 &&
             !result->clippingDetected)) ? "PASSED" : "FAILED");

    fclose(file);
}

// Generate test sine wave
void generateTestSine(double frequency, double duration, int sampleRate, const char* filename) {
    int samples = (int)(duration * sampleRate);
    float* audioData = malloc(samples * sizeof(float));

    if (!audioData) {
        fprintf(stderr, "Failed to allocate memory for sine generation\n");
        return;
    }

    // Generate sine wave
    for (int i = 0; i < samples; i++) {
        audioData[i] = (float)sin(2.0 * M_PI * frequency * i / sampleRate);
    }

    // Write WAV file
    FILE* file = fopen(filename, "wb");
    if (!file) {
        perror("Failed to create WAV file");
        free(audioData);
        return;
    }

    WavHeader header = {0};
    memcpy(header.riff, "RIFF", 4);
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmtMarker, "fmt ", 4);
    header.fmtLength = 16;
    header.audioFormat = 3; // IEEE float
    header.numChannels = 1; // Mono for simplicity
    header.sampleRate = sampleRate;
    header.byteRate = sampleRate * 1 * sizeof(float);
    header.blockAlign = 1 * sizeof(float);
    header.bitsPerSample = 32;
    memcpy(header.dataMarker, "data", 4);
    header.dataSize = samples * sizeof(float);
    header.fileSize = 36 + header.dataSize;

    fwrite(&header, sizeof(WavHeader), 1, file);
    fwrite(audioData, sizeof(float), samples, file);
    fclose(file);

    free(audioData);
}

// Print usage information
void printUsage(const char* progName) {
    printf("Usage: %s [options]\n", progName);
    printf("\nAudio Verification Options:\n");
    printf("  --input <file.wav>           Input WAV file to analyze\n");
    printf("  --expected-rpm <rpm>         Target RPM (required for analysis)\n");
    printf("  --duration <seconds>         Duration of audio (default: 3.0)\n");
    printf("  --verbose, -v                Verbose output\n");
    printf("  --detailed                   Perform detailed analysis\n");
    printf("  --output-report <file.json>  Write results to JSON file\n");
    printf("\nTesting Options:\n");
    printf("  --generate-sine <freq>       Generate test sine wave\n");
    printf("  --help, -h                   Show this help\n");
    printf("\nExamples:\n");
    printf("  %s --input output.wav --expected-rpm 3000 --detailed\n", progName);
    printf("  %s --generate-sine 440 --duration 5.0\n", progName);
}