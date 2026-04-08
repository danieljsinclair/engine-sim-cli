# ARCH-002: ITelemetryProvider Interface Design and Implementation

**Priority:** P1 - High Priority
**Status:** 🟡 Ready for Implementation
**Assignee:** @tech-architect
**Reviewer:** @test-architect, @product-owner

## Overview

Design and implement ITelemetryProvider interface to separate telemetry concerns from logging. This provides clean abstraction for telemetry data production and consumption, enabling future TMUX presentation and cross-platform telemetry support.

## Problem Statement

Current implementation mixes logging and telemetry concerns. There's no clean abstraction for:
- Telemetry data production (bridge)
- Telemetry data storage
- Telemetry data consumption (presentation)

This violates Single Responsibility Principle and makes it difficult to implement different telemetry consumers (CLI, TMUX, etc.).

## Objectives

1. **Interface Design**: Create `ITelemetryProvider` interface for telemetry data access
2. **Separation of Concerns**: Separate telemetry from logging (ILogging)
3. **Thread-Safe Storage**: In-memory telemetry storage that's thread-safe
4. **Bridge Integration**: Bridge produces telemetry/stats
5. **Future-Ready**: Enable TMUX presentation and cross-platform telemetry

## Acceptance Criteria

### Interface Design
- [ ] Create `ITelemetryProvider` interface with focused methods
- [ ] Define telemetry data structures (stats, events, metrics)
- [ ] Thread-safe API design
- [ ] Clear separation from ILogging interface

### Implementation
- [ ] Implement in-memory telemetry storage
- [ ] Thread-safe operations (atomic or mutex-protected)
- [ ] Performance-conscious design (no blocking on main audio thread)
- [ ] Memory-bounded (unbounded growth prevention)

### Bridge Integration
- [ ] Bridge produces telemetry data via ITelemetryProvider
- [ ] Bridge integration doesn't impact audio performance
- [ ] Telemetry production is optional (can be disabled)
- [ ] Minimal overhead for telemetry collection

### Testing
- [ ] `make test` passes completely
- [ ] Thread safety tests pass
- [ ] Performance tests verify no audio thread impact
- [ ] Memory bounds tests verify no unbounded growth
- [ ] Test architect review passes

### Documentation
- [ ] ITelemetryProvider interface documented
- [ ] TELEMETRY_ARCHITECTURE.md updated
- [ ] Usage examples provided
- [ ] Thread-safety guarantees documented

## Technical Approach

### Interface Design
```cpp
// Telemetry data structures
struct AudioMetrics {
    uint64_t framesRendered;
    uint64_t bufferUnderruns;
    float averageLatencyMs;
    // ... other metrics
};

struct TelemetryEvent {
    std::chrono::system_clock::time_point timestamp;
    std::string category;
    std::string message;
};

// Interface
class ITelemetryProvider {
public:
    virtual ~ITelemetryProvider() = default;

    // Metrics
    virtual void recordMetric(const std::string& name, double value) = 0;
    virtual AudioMetrics getAudioMetrics() const = 0;

    // Events
    virtual void logEvent(const TelemetryEvent& event) = 0;
    virtual std::vector<TelemetryEvent> getRecentEvents(size_t limit = 100) const = 0;

    // Lifecycle
    virtual void reset() = 0;
    virtual bool isEnabled() const = 0;
};
```

### Implementation Approach
- In-memory storage with fixed-size buffers
- Lock-free or minimal-locking design for performance
- Separate queues for events to prevent audio thread blocking
- Optional collection (disabled by default for production)

### Bridge Integration
- Bridge owns ITelemetryProvider instance
- Bridge methods call telemetry recording
- Minimal overhead (function call overhead only when enabled)
- No impact on audio thread when disabled

## Dependencies

- Phase 6: IAudioStrategy consolidation (blocking)
- Bridge refactoring to integrate telemetry

## Risk Assessment

**Medium Risk:**
- Thread-safety bugs in telemetry storage
- Performance impact on audio thread
- Memory exhaustion from unbounded telemetry growth

**Mitigation:**
- Test architect review required for thread-safety
- Performance tests for audio thread impact
- Fixed-size buffers and rotation policies
- Disable telemetry by default in production builds

## Definition of Done

- [ ] All acceptance criteria met
- [ ] `make test` passes completely
- [ ] Performance tests show no audio thread degradation
- [ ] Documentation updated
- [ ] Code review approved by @tech-architect and @test-architect
- [ ] @product-owner final approval

## References

- `/Users/danielsinclair/vscode/escli.refac7/docs/TELEMETRY_ARCHITECTURE.md`
- `/Users/danielsinclair/vscode/escli.refac7/docs/AUDIO_MODULE_ARCHITECTURE.md`

---

**Created:** 2026-04-08
**Last Updated:** 2026-04-08
**Estimate:** 1-2 days
