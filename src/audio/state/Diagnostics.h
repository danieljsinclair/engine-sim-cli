// Diagnostics.h - Audio performance and timing diagnostics
// SRP: Single responsibility - manages only diagnostic metrics
// OCP: New diagnostic types can be added without modifying existing code
// DIP: High-level modules depend on this abstraction

#ifndef DIAGNOSTICS_H
#define DIAGNOSTICS_H

#include <atomic>
#include <chrono>

/**
 * Diagnostics - Audio performance and timing diagnostics
 *
 * Responsibilities:
 * - Track rendering timing metrics
 * - Monitor frame budget usage
 * - Track headroom for buffer health
 * - Provide performance metrics for monitoring
 * - Thread-safe metric collection
 *
 * SRP: Only manages diagnostics, not audio or buffer state
 */
struct Diagnostics {
    /**
     * Time measurement type
     */
    using Clock = std::chrono::high_resolution_clock;
    using TimePoint = std::chrono::time_point<Clock>;
    using Duration = std::chrono::microseconds;

    /**
     * Initialize with default values
     */
    Diagnostics()
        : lastRenderMs(0.0)
        , lastHeadroomMs(0.0)
        , lastBudgetPct(0.0)
        , lastFrameBudgetPct(0.0)
        , totalFramesRendered(0)
    {}

    /**
     * Last render time in milliseconds
     * - Measured in audio callback
     * - Used for real-time performance monitoring
     */
    std::atomic<double> lastRenderMs;

    /**
     * Last headroom time in milliseconds
     * - Time between render request and buffer depletion
     * - Should be around 16ms for healthy cursor-chasing
     * - Values < 10ms indicate potential underrun
     * - Values > 20ms indicate buffer overfill
     */
    std::atomic<double> lastHeadroomMs;

    /**
     * Last render time budget percentage used
     * - renderTime / 16ms budget per callback
     * - Values > 80% indicate performance concern
     * - Used for system load monitoring
     */
    std::atomic<double> lastBudgetPct;

    /**
     * Last frame count requested vs available
     * - framesNeeded / framesAvailable (when > 1.0)
     * - Used to detect frame starvation
     */
    std::atomic<double> lastFrameBudgetPct;

    /**
     * Total number of frames rendered
     * - Monitored for throughput calculation
     * - Reset on re-initialization
     */
    std::atomic<int64_t> totalFramesRendered;

    /**
     * Record render time
     * @param renderTimeMs Time taken for this render in milliseconds
     * @param framesRendered Number of frames rendered in this call
     */
    void recordRender(double renderTimeMs, int framesRendered) {
        auto now = Clock::now();

        lastRenderMs.store(renderTimeMs);

        // Calculate headroom (assumes ~16ms callback interval)
        lastHeadroomMs.store(16.0 - renderTimeMs);

        // Calculate budget percentage
        double budgetPct = (renderTimeMs / 16.0) * 100.0;
        lastBudgetPct.store(budgetPct);

        // Calculate frame budget (simplified estimate)
        double frameBudgetPct = 0.0;
        if (renderTimeMs > 0.0) {
            frameBudgetPct = static_cast<double>(framesRendered) / (renderTimeMs * 44100.0 / 1000.0) * 100.0;
        }
        lastFrameBudgetPct.store(frameBudgetPct);

        totalFramesRendered.fetch_add(framesRendered);
    }

    /**
     * Reset all diagnostic counters
     * Useful for cleanup or test scenarios
     */
    void reset() {
        lastRenderMs.store(0.0);
        lastHeadroomMs.store(0.0);
        lastBudgetPct.store(0.0);
        lastFrameBudgetPct.store(0.0);
        totalFramesRendered.store(0);
    }

    /**
     * Get current diagnostic snapshot
     * @return Copy of current diagnostic state
     */
    struct Snapshot {
        double lastRenderMs;
        double lastHeadroomMs;
        double lastBudgetPct;
        double lastFrameBudgetPct;
        int64_t totalFramesRendered;

        Snapshot()
            : lastRenderMs(0.0)
            , lastHeadroomMs(0.0)
            , lastBudgetPct(0.0)
            , lastFrameBudgetPct(0.0)
            , totalFramesRendered(0)
        {}
    };

    /**
     * Get current diagnostic snapshot
     * Thread-safe read of all diagnostic metrics
     * @return Snapshot of current diagnostic state
     */
    Snapshot getSnapshot() const {
        Snapshot snapshot;
        snapshot.lastRenderMs = lastRenderMs.load();
        snapshot.lastHeadroomMs = lastHeadroomMs.load();
        snapshot.lastBudgetPct = lastBudgetPct.load();
        snapshot.lastFrameBudgetPct = lastFrameBudgetPct.load();
        snapshot.totalFramesRendered = totalFramesRendered.load();
        return snapshot;
    }
};

#endif // DIAGNOSTICS_H
