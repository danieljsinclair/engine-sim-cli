// MockDataSimulator.cpp - Implementation of mock simulator for integration testing

#include "MockDataSimulator.h"
#include <cstring>

// ============================================================================
// MockDataSimulatorContext Implementation
// ============================================================================

MockDataSimulatorContext::MockDataSimulatorContext() {
    // Initialize stats to fixed known values
    std::memset(&m_stats, 0, sizeof(m_stats));
    m_stats.currentRPM = FIXED_RPM_AT_IDLE;
    m_stats.currentLoad = FIXED_LOAD_AT_IDLE;

    // Reserve buffer space
    m_audioBuffer.resize(AUDIO_BUFFER_SIZE, 0);
    m_conversionBuffer.resize(4096 * 2, 0); // Max frames * 2 channels
}

void MockDataSimulatorContext::initialize(const EngineSimConfig* config) {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (config) {
        m_config = *config;
    } else {
        // Use defaults
        m_config.sampleRate = SAMPLE_RATE;
        m_config.audioBufferSize = static_cast<int32_t>(AUDIO_BUFFER_SIZE);
    }

    // Reset state
    reset();

    m_lastError.clear();
}

EngineSimResult MockDataSimulatorContext::renderAudio(float* buffer, int32_t frames, int32_t* outSamplesWritten) {
    if (!buffer || frames <= 0) {
        m_lastError = "Invalid parameters to renderAudio";
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Convert int16 buffer to float output
    int framesAvailable = std::min(frames, static_cast<int32_t>((m_audioBuffer.size() - m_readPosition) / 2));

    if (framesAvailable == 0) {
        // Buffer underrun - fill with silence
        std::memset(buffer, 0, frames * 2 * sizeof(float));
        if (outSamplesWritten) {
            *outSamplesWritten = 0;
        }
        return ESIM_SUCCESS;
    }

    // Convert samples from int16 to float [-1.0, 1.0]
    int samplesWritten = 0;
    for (int i = 0; i < framesAvailable; ++i) {
        for (int ch = 0; ch < 2; ++ch) {
            size_t idx = m_readPosition + (i * 2) + ch;
            int16_t sample = (idx < m_audioBuffer.size()) ? m_audioBuffer[idx] : 0;
            buffer[samplesWritten++] = int16ToFloat(sample);
        }
    }

    // Update read position
    size_t newPos = m_readPosition + samplesWritten;
    if (newPos >= m_audioBuffer.size()) {
        newPos %= m_audioBuffer.size(); // Wrap around
    }
    m_readPosition.store(newPos, std::memory_order_release);

    if (outSamplesWritten) {
        *outSamplesWritten = samplesWritten / 2; // Frames, not samples
    }

    return ESIM_SUCCESS;
}

EngineSimResult MockDataSimulatorContext::renderOnDemand(float* buffer, int32_t frames, int32_t* outSamplesWritten) {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Generate audio on-demand (for sync-pull mode)
    // This simulates synthesizer generating audio as requested

    int framesToGenerate = frames;
    int samplesGenerated = 0;

    for (int i = 0; i < framesToGenerate; ++i) {
        int frame = m_currentFrame + i;
        for (int ch = 0; ch < 2; ++ch) {
            int16_t sample = generateFixedSample(frame, ch);
            buffer[samplesGenerated++] = int16ToFloat(sample);
        }
    }

    // Update current frame counter
    m_currentFrame += framesToGenerate;

    if (outSamplesWritten) {
        *outSamplesWritten = samplesGenerated / 2; // Convert samples to frames
    }

    return ESIM_SUCCESS;
}

EngineSimResult MockDataSimulatorContext::setThrottle(double position) {
    if (position < 0.0 || position > 1.0) {
        m_lastError = "Throttle position out of range";
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    m_throttle.store(position, std::memory_order_relaxed);

    // Update RPM based on throttle (deterministic mapping)
    // RPM = 800 + throttle * 5200
    double newRPM = FIXED_RPM_AT_IDLE + position * (MAX_RPM - FIXED_RPM_AT_IDLE);
    m_stats.currentRPM = newRPM;
    m_stats.currentLoad = position;

    return ESIM_SUCCESS;
}

EngineSimResult MockDataSimulatorContext::setSpeedControl(double position) {
    return setThrottle(position); // Delegates to throttle
}

const char* MockDataSimulatorContext::getLastError() {
    return m_lastError.c_str();
}

void MockDataSimulatorContext::reset() {
    std::lock_guard<std::mutex> lock(m_mutex);

    m_writePosition = 0;
    m_readPosition = 0;
    m_currentFrame = 0;
    m_throttle = 0.0;
    m_speedControl = 0.0;
    m_audioThreadRunning = false;

    // Clear buffer
    std::fill(m_audioBuffer.begin(), m_audioBuffer.end(), 0);

    // Reset stats
    std::memset(&m_stats, 0, sizeof(m_stats));
    m_stats.currentRPM = FIXED_RPM_AT_IDLE;
    m_stats.currentLoad = FIXED_LOAD_AT_IDLE;

    m_lastError.clear();
}

int16_t MockDataSimulatorContext::generateFixedSample(int frame, int channel) const {
    // Generate fixed known values for deterministic testing
    // Pattern: left channel = 1000 + frame, right channel = 2000 + frame
    // This is different from ThreadedRenderer baseline (0, 1 pattern) to distinguish tests

    int base = (channel == 0) ? FIXED_LEFT_BASE : FIXED_RIGHT_BASE;
    return static_cast<int16_t>(base + frame);
}

float MockDataSimulatorContext::int16ToFloat(int16_t sample) const {
    constexpr float scale = 1.0f / 32768.0f;
    return static_cast<float>(sample) * scale;
}

// ============================================================================
// MockDataSimulator Implementation
// ============================================================================

MockDataSimulator::MockDataSimulator() {
    m_context = std::make_unique<MockDataSimulatorContext>();

    // Initialize with default config
    EngineSimConfig defaultConfig;
    defaultConfig.sampleRate = 48000;  // MockDataSimulatorContext::SAMPLE_RATE
    defaultConfig.audioBufferSize = 96000;  // MockDataSimulatorContext::AUDIO_BUFFER_SIZE
    m_context->initialize(&defaultConfig);
}

MockDataSimulator::~MockDataSimulator() = default;

void MockDataSimulator::initialize(const EngineSimConfig* config) {
    m_context->initialize(config);
}

EngineSimResult MockDataSimulatorContext::update(double deltaTime) {
    (void)deltaTime; // Not needed for mock
    return ESIM_SUCCESS;
}

EngineSimResult MockDataSimulatorContext::startAudioThread() {
    m_audioThreadRunning.store(true, std::memory_order_release);
    return ESIM_SUCCESS;
}

EngineSimResult MockDataSimulatorContext::getStats(EngineSimStats* outStats) {
    if (!outStats) {
        return ESIM_ERROR_INVALID_PARAMETER;
    }

    std::lock_guard<std::mutex> lock(m_mutex);
    *outStats = m_stats;

    return ESIM_SUCCESS;
}
