// MockAudioUnit.cpp - Implementation of mock audio capture for integration testing

#include "MockAudioUnit.h"
#include <algorithm>
#include <cmath>
#include <cstring>

// ============================================================================
// MockAudioUnit Implementation
// ============================================================================

MockAudioUnit::MockAudioUnit()
    : m_autoFillBuffer(true)
    , m_lastRenderTime(std::chrono::milliseconds(0))
{
}

void MockAudioUnit::simulateRender(float* buffer, int frames) {
    auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()
    );

    std::chrono::milliseconds interval(0);
    if (m_lastRenderTime.count() > 0) {
        interval = now - m_lastRenderTime;
    }

    std::lock_guard<std::mutex> lock(m_mutex);

    // Store render call for verification
    RenderCall call;
    call.framesRequested = frames;
    call.framesActuallyWritten = frames;
    call.timestamp = now;
    call.callbackIntervalMs = static_cast<double>(interval.count());

    // Copy samples for verification
    if (buffer && frames > 0) {
        call.samples.assign(buffer, buffer + frames * 2); // Stereo
    }

    m_renderCalls.push_back(call);
    m_lastRenderTime = now;

    // Call custom handler if set
    if (m_renderHandler) {
        m_renderHandler(buffer, frames);
    } else if (m_autoFillBuffer) {
        // Auto-fill with silence if buffer is zero
        // This helps tests that don't pre-fill the buffer
        bool allZero = true;
        for (int i = 0; i < frames * 2; ++i) {
            if (std::abs(buffer[i]) > 0.0001f) {
                allZero = false;
                break;
            }
        }

        if (allZero) {
            std::memset(buffer, 0, frames * 2 * sizeof(float));
        }
    }
}

void MockAudioUnit::clearRenderCalls() {
    std::lock_guard<std::mutex> lock(m_mutex);
    m_renderCalls.clear();
    m_lastRenderTime = std::chrono::milliseconds(0);
}

size_t MockAudioUnit::getRenderCallCount() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_renderCalls.size();
}

std::vector<MockAudioUnit::RenderCall> MockAudioUnit::getRenderCalls() const {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_renderCalls;
}

bool MockAudioUnit::hasExpectedSampleCount(int expectedCallCount) const {
    return getRenderCallCount() == static_cast<size_t>(expectedCallCount);
}

bool MockAudioUnit::samplesWithinTolerance(const std::vector<float>& expected, float tolerance) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.empty()) {
        return expected.empty();
    }

    // Flatten all samples from all render calls
    std::vector<float> allSamples;
    for (const auto& call : m_renderCalls) {
        allSamples.insert(allSamples.end(), call.samples.begin(), call.samples.end());
    }

    if (allSamples.size() != expected.size()) {
        return false;
    }

    // Check each sample within tolerance
    for (size_t i = 0; i < expected.size(); ++i) {
        float diff = std::abs(allSamples[i] - expected[i]);
        if (diff > tolerance) {
            return false;
        }
    }

    return true;
}

bool MockAudioUnit::noUnderrunsDetected() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Check if any render call wrote fewer frames than requested
    for (const auto& call : m_renderCalls) {
        if (call.framesActuallyWritten < call.framesRequested) {
            return false;
        }
    }

    return true;
}

bool MockAudioUnit::allFramesWritten(int expectedFramesPerCall) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    for (const auto& call : m_renderCalls) {
        if (call.framesActuallyWritten != expectedFramesPerCall) {
            return false;
        }
    }

    return true;
}

bool MockAudioUnit::getLatestRenderCall(RenderCall& outCall) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.empty()) {
        return false;
    }

    outCall = m_renderCalls.back();
    return true;
}

bool MockAudioUnit::getRenderCall(size_t index, RenderCall& outCall) const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (index >= m_renderCalls.size()) {
        return false;
    }

    outCall = m_renderCalls[index];
    return true;
}

double MockAudioUnit::getAverageFramesWritten() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.empty()) {
        return 0.0;
    }

    double total = 0.0;
    for (const auto& call : m_renderCalls) {
        total += call.framesActuallyWritten;
    }

    return total / m_renderCalls.size();
}

int MockAudioUnit::getTotalFramesWritten() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    int total = 0;
    for (const auto& call : m_renderCalls) {
        total += call.framesActuallyWritten;
    }

    return total;
}

double MockAudioUnit::getAverageCallbackInterval() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.size() < 2) {
        return 0.0;
    }

    double total = 0.0;
    int count = 0;

    for (size_t i = 1; i < m_renderCalls.size(); ++i) {
        total += m_renderCalls[i].callbackIntervalMs;
        count++;
    }

    return total / count;
}

void MockAudioUnit::setAutoFillBuffer(bool enable) {
    m_autoFillBuffer = enable;
}

bool MockAudioUnit::getAutoFillBuffer() const {
    return m_autoFillBuffer;
}

void MockAudioUnit::setRenderHandler(RenderHandler handler) {
    m_renderHandler = handler;
}

std::chrono::milliseconds MockAudioUnit::getFirstRenderTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.empty()) {
        return std::chrono::milliseconds(0);
    }

    return m_renderCalls.front().timestamp;
}

std::chrono::milliseconds MockAudioUnit::getLastRenderTime() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.empty()) {
        return std::chrono::milliseconds(0);
    }

    return m_renderCalls.back().timestamp;
}

double MockAudioUnit::getTotalRenderDurationMs() const {
    std::lock_guard<std::mutex> lock(m_mutex);

    if (m_renderCalls.size() < 2) {
        return 0.0;
    }

    auto first = m_renderCalls.front().timestamp;
    auto last = m_renderCalls.back().timestamp;

    return static_cast<double>((last - first).count());
}
