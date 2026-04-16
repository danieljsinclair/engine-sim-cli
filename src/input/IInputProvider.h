// IInputProvider.h - Input provider interface
// Abstracts input source: keyboard, upstream (ODB2/VirtualICE), etc.
// OCP: New input providers can be added without modifying existing code
// DI: Provider is injected into simulation loop

#ifndef I_INPUT_PROVIDER_H
#define I_INPUT_PROVIDER_H

#include <string>
#include <memory>
#include <optional>

namespace input {

// ============================================================================
// EngineInput - What the simulation receives from input sources
// Only external inputs - simulator handles its own state (starter, etc.)
// ============================================================================

struct EngineInput {
    double throttle;        // 0.0 - 1.0 (from keyboard/upstream)
    bool ignition;         // true = on (from keyboard/upstream)
    bool starterMotor;     // true = engaged (from keyboard/upstream)
    // Simulator auto-disengages when RPM > threshold
};

// ============================================================================
// IInputProvider Interface
// Implemented by: KeyboardInputProvider, UpstreamProvider (ODB2/VirtualICE)
// ============================================================================

class IInputProvider {
public:
    virtual ~IInputProvider() = default;
    
    // ========================================================================
    // Lifecycle
    // ========================================================================
    
    virtual bool Initialize() = 0;
    virtual void Shutdown() = 0;
    virtual bool IsConnected() const = 0;
    
    // ========================================================================
    // Input Queries (called from simulation thread)
    // ========================================================================

    /**
     * Poll for input and return current engine inputs.
     * Returns nullopt to signal loop termination.
     * Combines Update + ShouldContinue + GetEngineInput into one call.
     */
    virtual std::optional<EngineInput> OnUpdateSimulation(double dt) = 0;

    virtual EngineInput GetEngineInput() const = 0;
    virtual double GetThrottle() const = 0;
    virtual bool GetIgnition() const = 0;
    virtual bool GetStarterSwitch() const = 0;

    virtual void Update(double dt) = 0;

    // Loop control - input knows whether to continue
    virtual bool ShouldContinue() const = 0;
    
    virtual std::string GetProviderName() const = 0;
    virtual std::string GetLastError() const = 0;
};

} // namespace input

#endif // I_INPUT_PROVIDER_H
