// TelemetryProviderFactory.cpp - extracted from CLIMain.cpp createInputProvider
// (S3776 cognitive-complexity relief). The live/replay flag ladders were
// duplicated verbatim between the two branches — now one shared wiring path,
// so both transports exercise the SAME coupling code by construction.

#include "config/TelemetryProviderFactory.h"

#include <iostream>
#include <string>

#include "config/CLIconfig.h"
#include "config/CliException.h"
#include "input/LiveTelemetryProvider.h"
#include "input/ReplayTelemetryProvider.h"
#include "twin/CouplingModelSelector.h"
#include "twin/WheelCoupling.h"

namespace {

// --wheel-coupling: free / pin / torque. Fail-fast on anything else rather
// than silently falling back to FREE (a typo'd mode must never quietly
// re-couple the twin).
twin::WheelCouplingMode parseWheelCouplingMode(const std::string& mode) {
    if (mode == "free") return twin::WheelCouplingMode::Free;
    if (mode == "pin") return twin::WheelCouplingMode::Pin;
    if (mode == "torque") return twin::WheelCouplingMode::Torque;
    throw CliException("--wheel-coupling must be 'free', 'pin' or 'torque', got: " + mode);
}

// --coupling-model: clutch-map (smooth governor fallback) / torque-converter
// (default, the chosen fluid-coupling approach) / legacy (historical bang-bang
// relief, kept for A/B comparison). Fail-fast on a typo, mirroring
// --wheel-coupling.
twin::CouplingModelKind parseCouplingModel(const std::string& model) {
    if (model == "clutch-map") return twin::CouplingModelKind::ClutchMap;
    if (model == "torque-converter") return twin::CouplingModelKind::TorqueConverter;
    if (model == "legacy") return twin::CouplingModelKind::Legacy;
    throw CliException(
        "--coupling-model must be 'clutch-map', 'torque-converter' or 'legacy', got: " + model);
}

// --pin-tau-ms: negative is a typo'd flag value — fail fast rather than
// silently running rigid.
double validatedPinTauMs(double tauMs) {
    if (tauMs < 0.0) {
        throw CliException("--pin-tau-ms must be >= 0 (0 = rigid pin), got: " + std::to_string(tauMs));
    }
    return tauMs;
}

// Apply the shared twin coupling flags to a coupling-bearing provider, in the
// historical order (coupling mode, coupling model, tau, torque toggles).
// Template over the concrete provider: live and replay expose the same setter
// surface, but the setters are not yet on a shared base — promoting them into
// the bridge is the deferred interface refactor (Open/Closed seam).
// The torque toggles are forwarded unconditionally: the disabled configs are
// inert no-ops on the twin (set-disabled is provably identical to never-set),
// so the default path stays byte-identical.
template <typename Provider>
void applyTwinCouplingFlags(Provider& provider, const TwinArgs& twin) {
    provider.setWheelCouplingMode(parseWheelCouplingMode(twin.wheelCoupling));
    provider.setCouplingModel(parseCouplingModel(twin.couplingModel));
    provider.setPinTauMs(validatedPinTauMs(twin.pinTauMs));
    twin::EffectiveThrottleConfig effectiveThrottle;
    effectiveThrottle.enabled = twin.effectiveThrottle;
    provider.setEffectiveThrottleConfig(effectiveThrottle);
    twin::TorqueInformedGearboxConfig torqueInformedGearbox;
    torqueInformedGearbox.enabled = twin.torqueInformedGearbox;
    provider.setTorqueInformedGearboxConfig(torqueInformedGearbox);
}

// Wire --start-from/--end-at time slicing (pure member storage on both
// providers; the slice is applied when the provider runs).
template <typename Provider>
void applyTimeSlicing(Provider& provider, const ReplayArgs& replay) {
    provider.setStartFromS(replay.startFromS);
    provider.setEndAtS(replay.endAtS);
}

}  // namespace

std::unique_ptr<input::IInputProvider> buildTelemetryProvider(const CommandLineArgs& args) {
    if (args.twin.liveTelemetry) {
        // Live telemetry mode: read decoded CSV from stdin (vehicle-sim
        // --stdout-csv piped in), one row at a time. --start is implicit —
        // the provider fires the starter on frame 0. Initialize, slicing
        // validation, gearbox logger and warm-boot stay with the caller.
        auto live = std::make_unique<input::LiveTelemetryProvider>(
            std::cin, /*autoStart=*/true);
        applyTwinCouplingFlags(*live, args.twin);
        applyTimeSlicing(*live, args.replay);
        return live;
    }

    if (!args.replay.telemetryPath.empty()) {
        // Replay mode: the telemetry CSV is the sole input source (no
        // keyboard). Coupling flags must be set BEFORE Initialize() so the
        // twin is constructed with them (Initialize seeds the twin from
        // these); the caller runs Initialize + slicing validation + the
        // keyboard/overlay wiring.
        auto replay = std::make_unique<input::ReplayTelemetryProvider>(
            args.replay.telemetryPath, /*autoStart=*/true, /*autoGearbox=*/args.gearbox.automatic);
        applyTwinCouplingFlags(*replay, args.twin);
        applyTimeSlicing(*replay, args.replay);
        return replay;
    }

    return nullptr;
}
