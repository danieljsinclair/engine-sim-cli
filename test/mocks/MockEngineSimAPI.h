// MockEngineSimAPI.h - Controllable mock for EngineSimAPI in tests
//
// EngineSimAPI is a concrete struct with inline methods forwarding to C bridge
// functions. It has no virtual methods and no data members. We create a
// parallel mock struct that inherits from EngineSimAPI and hides methods
// with same-signature methods. This allows:
//   - Passing MockEngineSimAPI& where EngineSimAPI& is expected (inheritance)
//   - Passing MockEngineSimAPI* where EngineSimAPI* is expected (inheritance)
//   - Non-virtual dispatch through the base pointer still calls base methods
//     UNLESS we use the mock directly (not through base pointer)
//
// CRITICAL: Since EngineSimAPI methods are non-virtual, calling through a
// base-class pointer will dispatch to EngineSimAPI, NOT MockEngineSimAPI.
// This means:
//   - For SyncPullStrategy (stores engineAPI_ pointer from AudioStrategyConfig):
//     We must use the mock directly, not through base pointer.
//     The strategy calls engineAPI_->RenderOnDemand() which dispatches
//     statically to EngineSimAPI::RenderOnDemand -> EngineSimRenderOnDemand.
//     This calls the REAL bridge function. Our mock can't intercept this.
//   - For ThreadedStrategy::updateSimulation (takes const EngineSimAPI& api):
//     api.Update() dispatches statically to EngineSimAPI::Update.
//
// SOLUTION: Instead of trying to intercept non-virtual calls, we test the
// BEHAVIOR by using the real bridge API with the --sine engine. The bridge
// records its own state. For unit-level mocking, we use a global interceptor
// pattern: the mock replaces the engineAPI_ pointer with itself,
// and since SyncPullStrategy calls through the pointer, the static dispatch
// goes to EngineSimAPI::RenderOnDemand which calls EngineSimRenderOnDemand().
//
// Alternative: We create the mock to NOT inherit from EngineSimAPI but instead
// provide compatible methods, and use reinterpret_cast for the pointer.
// This is safe because EngineSimAPI has NO data members and NO virtual table.

#ifndef MOCK_ENGINE_SIM_API_H
#define MOCK_ENGINE_SIM_API_H

#include "simulator/engine_sim_bridge.h"
#include "simulator/engine_sim_loader.h"
#include "common/ILogging.h"

#include <atomic>
#include <cstring>

namespace mock_api {

struct MockState {
    // Controls what Update() returns (when called through mock)
    std::atomic<int> updateCallCount{0};
    std::atomic<double> lastDeltaTime{0.0};
    std::atomic<EngineSimResult> updateResult{ESIM_SUCCESS};

    // Controls what RenderOnDemand() returns (when called through mock)
    std::atomic<int> renderOnDemandCallCount{0};
    std::atomic<int> framesToReport{0};
    std::atomic<EngineSimResult> renderOnDemandResult{ESIM_SUCCESS};

    // Controls what ReadAudioBuffer() returns (when called through mock)
    std::atomic<int> readAudioBufferCallCount{0};
    std::atomic<int> readFramesToReport{0};
    std::atomic<EngineSimResult> readAudioBufferResult{ESIM_SUCCESS};

    // Controls what GetStats() returns
    std::atomic<double> mockRPM{800.0};
    std::atomic<double> mockLoad{0.0};
    std::atomic<double> mockExhaustFlow{0.0};

    void reset() {
        updateCallCount.store(0);
        lastDeltaTime.store(0.0);
        updateResult.store(ESIM_SUCCESS);
        renderOnDemandCallCount.store(0);
        framesToReport.store(0);
        renderOnDemandResult.store(ESIM_SUCCESS);
        readAudioBufferCallCount.store(0);
        readFramesToReport.store(0);
        readAudioBufferResult.store(ESIM_SUCCESS);
        mockRPM.store(800.0);
        mockLoad.store(0.0);
        mockExhaustFlow.store(0.0);
    }
};

// Singleton state so free functions can access it
inline MockState& state() {
    static MockState s;
    return s;
}

} // namespace mock_api

// ============================================================================
// MockEngineSimAPI - Inherits from EngineSimAPI for type compatibility
//
// IMPORTANT: Non-virtual methods mean base-class pointer calls go to
// EngineSimAPI, not MockEngineSimAPI. To use the mock for intercepted calls,
// call directly on the MockEngineSimAPI object (not through base pointer).
//
// For tests that need to intercept calls through context->engineAPI:
// Use mock_api::globalMockAPI() which returns a MockEngineSimAPI that can
// be cast to EngineSimAPI* for pointer assignment.
//
// For ThreadedStrategy::updateSimulation(const EngineSimAPI& api):
// Pass MockEngineSimAPI directly. api.Update() calls EngineSimAPI::Update
// (static dispatch). To record the deltaTime, we use a global interceptor
// registered via EngineSimUpdate redirect.
// ============================================================================

struct MockEngineSimAPI : public EngineSimAPI {
    // Hide base class Update to record calls
    EngineSimResult Update(EngineSimHandle h, double dt) const {
        mock_api::state().updateCallCount.fetch_add(1);
        mock_api::state().lastDeltaTime.store(dt);
        return mock_api::state().updateResult.load();
    }

    // Hide base class RenderOnDemand to record calls and produce controllable output
    EngineSimResult RenderOnDemand(EngineSimHandle h, float* buf, int32_t frames, int32_t* written) const {
        mock_api::state().renderOnDemandCallCount.fetch_add(1);
        int toReport = mock_api::state().framesToReport.load();
        if (toReport > 0 && toReport <= frames) {
            // Fill with a recognizable non-silence pattern
            for (int i = 0; i < toReport * 2; ++i) {
                buf[i] = (i % 2 == 0) ? 0.1f : -0.1f;
            }
            if (written) *written = toReport;
        } else {
            if (written) *written = 0;
        }
        return mock_api::state().renderOnDemandResult.load();
    }

    // Hide base class ReadAudioBuffer
    EngineSimResult ReadAudioBuffer(EngineSimHandle h, float* buf, int32_t frames, int32_t* read) const {
        mock_api::state().readAudioBufferCallCount.fetch_add(1);
        int toReport = mock_api::state().readFramesToReport.load();
        if (toReport > 0) {
            for (int i = 0; i < toReport * 2; ++i) {
                buf[i] = 0.5f;
            }
            if (read) *read = toReport;
        } else {
            if (read) *read = 0;
        }
        return mock_api::state().readAudioBufferResult.load();
    }

    // Hide base class GetStats
    EngineSimResult GetStats(EngineSimHandle h, EngineSimStats* s) const {
        if (s) {
            std::memset(s, 0, sizeof(EngineSimStats));
            s->currentRPM = mock_api::state().mockRPM.load();
            s->currentLoad = mock_api::state().mockLoad.load();
            s->exhaustFlow = mock_api::state().mockExhaustFlow.load();
        }
        return ESIM_SUCCESS;
    }
};

#endif // MOCK_ENGINE_SIM_API_H
