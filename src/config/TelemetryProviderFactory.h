// TelemetryProviderFactory.h - SRP extraction from CLIMain.cpp (S3776).
//
// Builds the live/replay telemetry input provider from CommandLineArgs:
// construction + validated coupling/torque flag wiring + time-slice setters.
// Lifecycle (Initialize) and trace-duration-dependent slicing validation stay
// with the caller (CLIMain) — validation needs the parsed trace duration,
// which only exists after Initialize() opens the file. Pinned by
// test/unit/CLIMainS3776S1820Test.cpp Section B (SLIPLOCK_REFACTOR_EXPOSED).
//
// The twin coupling parse+apply RECIPE itself is bridge-side
// (input/TwinCouplingRecipe.h; string->enum resolution in
// twin/CouplingConfig.h — consolidation wave B). This factory stays the CLI's
// construction seam and keeps the thin typed seams below so the CLI fail-fast
// remains CliException with the same messages.

#ifndef CLI_TELEMETRY_PROVIDER_FACTORY_H
#define CLI_TELEMETRY_PROVIDER_FACTORY_H

#include <functional>
#include <memory>
#include <stdexcept>
#include <string>

#include "io/IInputProvider.h"
#include "config/CLIconfig.h"
#include "config/CliException.h"
#include "input/TwinCouplingRecipe.h"
#include "twin/CouplingModelSelector.h"
#include "twin/PinTargetChase.h"
#include "twin/UpstreamTorqueHint.h"
#include "twin/WheelCoupling.h"

class CommandLineArgs;
class ILogging;

// Named detail namespace (S1000): thin CLI seams over the bridge's shared
// twin-coupling recipe (input/TwinCouplingRecipe.h; string->enum resolution
// in twin/CouplingConfig.h). The mapping + fail-fast messages live
// bridge-side; these wrappers keep the CLI's typed CliException surface (the
// bridge throws std::invalid_argument with the identical text; the seam maps
// it, message unchanged). inline keeps them ODR-safe across TUs.
namespace telemetry_detail {

// --wheel-coupling: free / pin / torque. Fail-fast on anything else rather
// than silently falling back to FREE (a typo'd mode must never quietly
// re-couple the twin).
inline twin::WheelCouplingMode parseWheelCouplingMode(const std::string& mode) {
    try {
        return twin::resolveWheelCouplingMode(mode);
    } catch (const std::invalid_argument& e) {
        throw CliException(e.what());
    }
}

// --coupling-model: clutch-map (smooth governor fallback) / torque-converter
// (default) / legacy (historical bang-bang relief, kept for A/B). Fail-fast
// on a typo, mirroring --wheel-coupling.
inline twin::CouplingModelKind parseCouplingModel(const std::string& model) {
    try {
        return twin::resolveCouplingModel(model);
    } catch (const std::invalid_argument& e) {
        throw CliException(e.what());
    }
}

// --pin-tau-ms stability-window warning (owner directive: tuning toggles are
// never restricted — warn, don't reject): thresholds + text live bridge-side
// in twin/PinTargetChase.h (consolidation wave B), beside the filter they
// describe. Returns the warning text when tau sits outside the stable window,
// nullptr when it is fine. tau <= 0 is the documented rigid passthrough (OFF)
// and never warns; every value is ACCEPTED (warn-only, owner directive
// 2026-09-04). Pinned VERBATIM by PinTauGuardTest.
inline const char* pinTauWarningText(double tauMs) {
    return twin::pinTauWarningText(tauMs);
}

}  // namespace telemetry_detail

// Apply the shared twin coupling flags to a coupling-bearing provider.
// Thin CLI adapter over the bridge recipe (input::applyTwinCouplingFlags):
// converts TwinArgs to input::TwinCouplingRecipe and maps the bridge's
// std::invalid_argument fail-fast to the CLI's CliException (same message).
// The setter ORDER and the store + re-apply-with-TWIN-defaults contract
// (Free/ClutchMap/0.0 — the 2026-09-06 factory-ordering regression fix) live
// in the bridge header; semantics are identical to the pre-move inline
// version pinned by the characterization nets.
template <typename Provider>
void applyTwinCouplingFlags(Provider& provider, const TwinArgs& twin) {
    input::TwinCouplingRecipe recipe;
    recipe.wheelCoupling = twin.wheelCoupling;
    recipe.couplingModel = twin.couplingModel;
    // Warn-only seam: pinTauWarningText (called at arg-parse time in
    // CLIconfig.cpp processArgs) owns the console warning; every value passes
    // through — tau <= 0 is rigid by PinTargetChase construction.
    recipe.pinTauMs = twin.pinTauMs;
    recipe.effectiveThrottle = twin.effectiveThrottle;
    recipe.torqueInformedGearbox = twin.torqueInformedGearbox;
    try {
        input::applyTwinCouplingFlags(provider, recipe);
    } catch (const std::invalid_argument& e) {
        throw CliException(e.what());
    }
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
