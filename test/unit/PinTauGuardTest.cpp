// PinTauGuardTest.cpp — --pin-tau-ms stability-window guard contract.
//
// Owner directive (2026-09-04): tau is a TUNING toggle — every value is
// accepted, no fail-fast. Ill-advised values print a YELLOW console warning
// instead: below 60 ms (drivetrain bifurcation territory) and above 3000 ms
// (over-damped). tau <= 0 is the documented rigid passthrough (OFF) and is
// silent. Pinned here so the boundaries can't drift silently.
//
// Empirical basis: engine-sim-bridge docs/architecture/pin-tau-compliance.md
// (20-50 ms bench runs ran away to ~207 mph / 15.5k rpm; 15000 ms halved
// road speed; 60-1000 ms stable).

#include <gtest/gtest.h>

#include "config/TelemetryProviderFactory.h"

namespace {

TEST(PinTauGuardTest, BelowStableWindowWarns) {
    // 0 < tau < 60 — includes the bifurcation band (20-50) and everything
    // smaller (below dt/2 at tau < 8.3 ms is wrong-by-construction).
    EXPECT_NE(telemetry_detail::pinTauWarningText(59.9), nullptr);
    EXPECT_NE(telemetry_detail::pinTauWarningText(50.0), nullptr);
    EXPECT_NE(telemetry_detail::pinTauWarningText(1.0), nullptr);
}

TEST(PinTauGuardTest, StableWindowIsSilent) {
    // 60..1000 inclusive = the recommended window; the owner tunes within it
    // (default 150) and must see no warning noise.
    EXPECT_EQ(telemetry_detail::pinTauWarningText(60.0), nullptr);
    EXPECT_EQ(telemetry_detail::pinTauWarningText(150.0), nullptr);
    EXPECT_EQ(telemetry_detail::pinTauWarningText(1000.0), nullptr);
}

TEST(PinTauGuardTest, OverdampedWarns) {
    // Above 3000 ms only — the 1001..3000 stretch is odd but legal and silent.
    EXPECT_EQ(telemetry_detail::pinTauWarningText(3000.0), nullptr);
    EXPECT_NE(telemetry_detail::pinTauWarningText(3000.1), nullptr);
    EXPECT_NE(telemetry_detail::pinTauWarningText(15000.0), nullptr);
}

TEST(PinTauGuardTest, RigidPassthroughIsSilent) {
    // tau <= 0 = the documented rigid OFF; PinTargetChase clamps it rigid.
    EXPECT_EQ(telemetry_detail::pinTauWarningText(0.0), nullptr);
    EXPECT_EQ(telemetry_detail::pinTauWarningText(-1.0), nullptr);
}

TEST(PinTauGuardTest, WarningNamesTheFlag) {
    // The warning must identify which flag and quote the stable window so the
    // owner can act on it mid-context-switch (owner comms directive).
    const char* low = telemetry_detail::pinTauWarningText(30.0);
    ASSERT_NE(low, nullptr);
    EXPECT_NE(std::string(low).find("--pin-tau-ms"), std::string::npos);
    EXPECT_NE(std::string(low).find("60-1000"), std::string::npos);
    const char* high = telemetry_detail::pinTauWarningText(5000.0);
    ASSERT_NE(high, nullptr);
    EXPECT_NE(std::string(high).find("--pin-tau-ms"), std::string::npos);
}

}  // namespace
