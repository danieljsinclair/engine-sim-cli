// TelemetryProviderFactory.cpp - extracted from CLIMain.cpp createInputProvider
// (S3776 cognitive-complexity relief). The live/replay flag ladders were
// duplicated verbatim between the two branches — now one shared wiring path,
// so both transports exercise the SAME coupling code by construction.

#include "config/TelemetryProviderFactory.h"

#include <functional>
#include <iostream>
#include <string>

#include "input/LiveTelemetryProvider.h"
#include "input/ReplayTelemetryProvider.h"

// Wire --start-from/--end-at time slicing (pure member storage on both
// providers; the slice is applied when the provider runs).
template <typename Provider>
void applyTimeSlicing(Provider& provider, const ReplayArgs& replay) {
    provider.setStartFromS(replay.startFromS);
    provider.setEndAtS(replay.endAtS);
}

std::unique_ptr<input::IInputProvider> buildTelemetryProvider(
        const CommandLineArgs& args,
        std::function<bool()> streamDataReady /* = nullptr */) {
    if (args.twin.liveTelemetry) {
        // Live telemetry mode: read decoded CSV from stdin (vehicle-sim
        // --stdout-csv piped in), one row at a time. --start is implicit —
        // the provider fires the starter on frame 0. Initialize, slicing
        // validation, gearbox logger and warm-boot stay with the caller.
        //
        // streamDataReady is the non-blocking readiness probe for the live
        // pipe (see LiveTelemetryProvider.h): when injected, the provider's
        // row refill returns short instead of parking the loop thread on a
        // lagging writer. The CLI injects poll(2) with zero timeout on stdin
        // (fd 0); nullptr keeps the deterministic blocking behaviour.
        auto live = std::make_unique<input::LiveTelemetryProvider>(
            std::cin, /*autoStart=*/true, std::move(streamDataReady));
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
