#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>

double estimateFrequency(const std::vector<int16_t>& samples, int sampleRate) {
    // Convert to mono
    std::vector<double> mono(samples.size() / 2);
    for (size_t i = 0; i < mono.size(); ++i) {
        mono[i] = (samples[i * 2] + samples[i * 2 + 1]) / 2.0;
    }

    // Count zero crossings
    int zeroCrossings = 0;
    bool wasPositive = mono[0] > 0;
    for (size_t i = 1; i < mono.size(); ++i) {
        bool isPositive = mono[i] > 0;
        if (isPositive != wasPositive) {
            zeroCrossings++;
            wasPositive = isPositive;
        }
    }

    // Frequency = (zeroCrossings / 2) * sampleRate / sampleCount
    std::cout << "Zero crossings: " << zeroCrossings << std::endl;
    std::cout << "Sample count: " << mono.size() << std::endl;

    if (zeroCrossings < 4) {
        return 0.0;
    }

    return (zeroCrossings / 2.0) * sampleRate / mono.size();
}

int main() {
    // Simulate SineWaveSimulator output at 800 RPM
    // Expected frequency = 800 / 6.0 = 133.33 Hz
    // For 4410 frames at 44100 Hz, we should see ~13.4 cycles
    // Expected zero crossings = 13.4 * 2 = ~27 crossings

    std::vector<int16_t> testSignal;
    double expectedFrequency = 800.0 / 6.0;  // 133.33 Hz
    int frames = 4410;

    std::cout << "Expected frequency: " << expectedFrequency << " Hz" << std::endl;
    std::cout << "Expected zero crossings: " << (int)(expectedFrequency * frames / 44100.0 * 2.0) << std::endl;

    // Generate a simple sine wave
    for (int i = 0; i < frames; ++i) {
        double t = static_cast<double>(i) / 44100.0;
        double value = std::sin(2.0 * M_PI * expectedFrequency * t);
        int16_t sample = static_cast<int16_t>(value * 16000.0);

        testSignal.push_back(sample);
        testSignal.push_back(sample);
    }

    double estimated = estimateFrequency(testSignal, 44100);
    std::cout << "Estimated frequency: " << estimated << " Hz" << std::endl;

    double difference = std::abs(estimated - expectedFrequency);
    double tolerance = expectedFrequency * 0.10;
    std::cout << "Difference: " << difference << " Hz" << std::endl;
    std::cout << "Tolerance (10%): " << tolerance << " Hz" << std::endl;

    if (difference <= tolerance) {
        std::cout << "PASS: Frequency matches expected value" << std::endl;
    } else {
        std::cout << "FAIL: Frequency does NOT match expected value" << std::endl;
    }

    return 0;
}
