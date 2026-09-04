// TelemetryProviderFactory.h - SRP extraction from CLIMain.cpp (S3776).
//
// Builds the live/replay telemetry input provider from CommandLineArgs:
// construction + validated coupling/torque flag wiring + time-slice setters.
// Lifecycle (Initialize) and trace-duration-dependent slicing validation stay
// with the caller (CLIMain) — validation needs the parsed trace duration,
// which only exists after Initialize() opens the file. Pinned by
// test/unit/CLIMainS3776S1820Test.cpp Section B (SLIPLOCK_REFACTOR_EXPOSED).

#ifndef CLI_TELEMETRY_PROVIDER_FACTORY_H
#define CLI_TELEMETRY_PROVIDER_FACTORY_H

#include <functional>
#include <memory>
#include <string>

#include "io/IInputProvider.h"
#include "config/CLIconfig.h"
#include "config/CliException.h"
#include "twin/CouplingModelSelector.h"
#include "twin/EffectiveThrottle.h"
#include "twin/UpstreamTorqueHint.h"
#include "twin/WheelCoupling.h"

class CommandLineArgs;
class ILogging;

// Named detail namespace (S1000): pure parse/validate helpers shared by the
// coupling-flag template below. inline keeps them ODR-safe across TUs now
// they no longer have internal linkage.
namespace telemetry_detail {

// --wheel-coupling: free / pin / torque. Fail-fast on anything else rather
// than silently falling back to FREE (a typo'd mode must never quietly
// re-couple the twin).
inline twin::WheelCouplingMode parseWheelCouplingMode(const std::string& mode) {
    if (mode == "free") return twin::WheelCouplingMode::Free;
    if (mode == "pin") return twin::WheelCouplingMode::Pin;
    if (mode == "torque") return twin::WheelCouplingMode::Torque;
    throw CliException("--wheel-coupling must be 'free', 'pin' or 'torque', got: " + mode);
}

// --coupling-model: clutch-map (smooth governor fallback) / torque-converter
// (default) / legacy (historical bang-bang relief, kept for A/B). Fail-fast
// on a typo, mirroring --wheel-coupling.
inline twin::CouplingModelKind parseCouplingModel(const std::string& model) {
    if (model == "clutch-map") return twin::CouplingModelKind::ClutchMap;
    if (model == "torque-converter") return twin::CouplingModelKind::TorqueConverter;
    if (model == "legacy") return twin::CouplingModelKind::Legacy;
    throw CliException(
        "--coupling-model must be 'clutch-map', 'torque-converter' or 'legacy', got: " + model);
}

// --pin-tau-ms stability-window warning (owner directive: tuning toggles are
// never restricted — warn, don't reject). Returns the warning text when tau
// sits outside the stable window, nullptr when it is fine. tau <= 0 is the
// documented rigid passthrough (OFF) and never warns. Any value is ACCEPTED:
// PinTargetChase clamps tau <= 0 to rigid, and every positive value is a legal
// (if ill-advised) experiment the owner may want to run.
// Empirical map (see engine-sim-bridge docs/architecture/pin-tau-compliance.md):
//   20-50 ms  drivetrain bifurcation (50 ms runs away to ~207 mph / 15.5k rpm)
//   60-1000 ms  stable window (recommended; default 150)
//   >3000 ms  over-damped (15000 ms halves road speed)
inline const char* pinTauWarningText(double tauMs) {
    if (tauMs > 0.0 && tauMs < 60.0) {
        return "--pin-tau-ms 60-1000 is the stable window; below 60 ms the drivetrain can "
               "bifurcate (20-50 ms bench runs ran away to ~207 mph). Continuing with your value.";
    }
    if (tauMs > 3000.0) {
        return "--pin-tau-ms above 3000 ms is over-damped (15000 ms halves road speed on the "
               "bench). 60-1000 ms is the stable window. Continuing with your value.";
    }
    return nullptr;
}

}  // namespace telemetry_detail

// Apply the shared twin coupling flags to a coupling-bearing provider, in the
// historical order (coupling mode, coupling model, tau, torque toggles).
// Template over the concrete provider: live, replay and demo expose the same
// setter surface. The torque toggles are forwarded unconditionally: the
// disabled configs are inert no-ops on the twin (set-disabled is provably
// identical to never-set), so the default path stays byte-identical.
template <typename Provider>
void applyTwinCouplingFlags(Provider& provider, const TwinArgs& twin) {
    provider.setWheelCouplingMode(telemetry_detail::parseWheelCouplingMode(twin.wheelCoupling));
    provider.setCouplingModel(telemetry_detail::parseCouplingModel(twin.couplingModel));
    // Warn-only seam: pinTauWarningText (called at arg-parse time in
    // CLIconfig.cpp processArgs) owns the console warning; every value passes
    // through — tau <= 0 is rigid by PinTargetChase construction.
    provider.setPinTauMs(twin.pinTauMs);
    twin::EffectiveThrottleConfig effectiveThrottle;
    effectiveThrottle.enabled = twin.effectiveThrottle;
    provider.setEffectiveThrottleConfig(effectiveThrottle);
    twin::TorqueInformedGearboxConfig torqueInformedGearbox;
    torqueInformedGearbox.enabled = twin.torqueInformedGearbox;
    provider.setTorqueInformedGearboxConfig(torqueInformedGearbox);
}

// Build the telemetry provider for --live-telemetry (LiveTelemetryProvider on
// std::cin) or --replay-telemetry (ReplayTelemetryProvider on the CSV path).
// Returns nullptr when neither mode is requested (the keyboard/demo path owns
// that construction). All coupling flags are validated here and fail fast
// (CliException) on a typo'd mode/model/tau — the twin reversion bug was
// downstream of a silent fallback, so none is ever silently corrected.
//
// Ordering contract (why the flags live here, before the caller runs
// Initialize): the providers store the coupling/torque configs and re-apply
// them when Initialize() creates the twin, so a pre-Initialize set is
// equivalent to the historical post-Initialize set (see the setters'
// store + re-apply contract on LiveTelemetryProvider.h).
//
// streamDataReady: optional non-blocking readiness probe forwarded to the
// --live-telemetry LiveTelemetryProvider (see its ctor). When injected, the
// provider's row refill returns short instead of parking the loop thread on a
// lagging stdin writer. The CLI injects poll(2) with zero timeout on STDIN
// (fd 0); nullptr keeps the deterministic blocking behaviour the unit tests
// rely on. Ignored by the replay path (file-backed, never parks).
std::unique_ptr<input::IInputProvider> buildTelemetryProvider(
        const CommandLineArgs& args,
        std::function<bool()> streamDataReady = nullptr);

#endif  // CLI_TELEMETRY_PROVIDER_FACTORY_H
