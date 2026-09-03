#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

#include "presentation/SteeringGauge.h"

using presentation::ArrowGauge;
using presentation::ISteeringGauge;
using presentation::SteeringGauge;
using presentation::SteeringStyle;

// Dual-mode gauge suite (owner directive): the braille clock face (12
// sectors, 30 deg) and the arrow compass (8 sectors, 45 deg) share the
// owner-authored fence-post semantics — normalise into [0, 360), first
// inclusive ceiling wins — so every semantic test runs against BOTH styles,
// each asserted against its own sector map rather than a forked suite.

namespace {

// Every gauge style ships in this suite; shared tests index each gauge's own
// map, so they hold for any sector count.
struct GaugeCase {
    std::string name;
    std::unique_ptr<const ISteeringGauge> gauge;
};

std::vector<GaugeCase> allGauges() {
    std::vector<GaugeCase> cases;
    cases.push_back({"braille", std::make_unique<SteeringGauge>()});
    cases.push_back({"arrows", std::make_unique<ArrowGauge>()});
    return cases;
}

} // namespace

// ---------------------------------------------------------------------------
// Shared fence-post semantics (both styles)
// ---------------------------------------------------------------------------

TEST(SteeringGaugeTest, FirstSectorStabilityAcrossStyles) {
    for (const auto& gaugeCase : allGauges()) {
        const auto& map = gaugeCase.gauge->GetAngleMap();
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(0), map[0].graphic)
            << gaugeCase.name << ": 0 deg must render the first sector glyph";
    }
}

// At every fence the sector holds; one degree past it the next sector begins.
TEST(SteeringGaugeTest, OneDegreeStepOverBetweenSectorsAcrossStyles) {
    for (const auto& gaugeCase : allGauges()) {
        const auto& map = gaugeCase.gauge->GetAngleMap();
        for (std::size_t i = 0; i + 1 < map.size(); ++i) {
            EXPECT_EQ(gaugeCase.gauge->getSteeringString(map[i].maxDegrees), map[i].graphic)
                << gaugeCase.name << ": fence " << map[i].maxDegrees
                << " deg must hold its own sector";
            EXPECT_EQ(gaugeCase.gauge->getSteeringString(map[i].maxDegrees + 1), map[i + 1].graphic)
                << gaugeCase.name << ": one degree past fence " << map[i].maxDegrees
                << " must step to the next sector";
        }
    }
}

TEST(SteeringGaugeTest, PositiveMultiTurnOverflowWrapsAcrossStyles) {
    for (const auto& gaugeCase : allGauges()) {
        const auto& map = gaugeCase.gauge->GetAngleMap();
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(360), map[0].graphic)
            << gaugeCase.name << ": one positive turn must wrap to the first sector";
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(720), map[0].graphic)
            << gaugeCase.name << ": two positive turns must wrap to the first sector";
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(3614), map[0].graphic)
            << gaugeCase.name << ": TDC ceiling after 10 turns (3614 % 360 = 14)";
    }
}

TEST(SteeringGaugeTest, NegativeMultiTurnUnderflowWrapsAcrossStyles) {
    for (const auto& gaugeCase : allGauges()) {
        const auto& map = gaugeCase.gauge->GetAngleMap();
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(-360), map[0].graphic)
            << gaugeCase.name << ": one negative turn must wrap to the first sector";
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(-720), map[0].graphic)
            << gaugeCase.name << ": two negative turns must wrap to the first sector";
        // -90 deg wraps onto the identical 270 deg sector (9 o'clock / left)
        EXPECT_EQ(gaugeCase.gauge->getSteeringString(-90), gaugeCase.gauge->getSteeringString(270))
            << gaugeCase.name << ": -90 deg must render identically to 270 deg";
    }
}

// The documented style contract: 12 clock positions vs 8 arrow directions,
// each plus a closing wrap entry back to the first glyph.
TEST(SteeringGaugeTest, StyleSectorCounts) {
    EXPECT_EQ(SteeringGauge::AngleMap().size(), std::size_t{13}); // 12 sectors + wrap tail
    EXPECT_EQ(ArrowGauge::AngleMap().size(), std::size_t{9});     // 8 octants + wrap tail
}

// ---------------------------------------------------------------------------
// Braille clock face (owner's original scenarios)
// ---------------------------------------------------------------------------

TEST(BrailleGaugeTest, TdcSectorSpanStability) {
    SteeringGauge gauge;
    EXPECT_EQ(gauge.getSteeringString(0), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(7), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(14), SteeringGauge::GLYPH_12);
}

TEST(BrailleGaugeTest, QuadrantBoundariesAroundThreeOclock) {
    SteeringGauge gauge;
    // 90 deg falls into the entry at index 3 (75 to 104 degrees)
    EXPECT_EQ(gauge.getSteeringString(75), SteeringGauge::GLYPH_3);
    EXPECT_EQ(gauge.getSteeringString(90), SteeringGauge::GLYPH_3);
    EXPECT_EQ(gauge.getSteeringString(104), SteeringGauge::GLYPH_3);
    // Stepping over 104 deg moves cleanly to the 4 o'clock sector
    EXPECT_EQ(gauge.getSteeringString(105), SteeringGauge::GLYPH_4);
}

// ---------------------------------------------------------------------------
// Arrow compass (v2 semantics on the shared fence-post map)
// ---------------------------------------------------------------------------

TEST(ArrowGaugeTest, CardinalArrowsAtExactAngles) {
    ArrowGauge gauge;
    EXPECT_EQ(gauge.getSteeringString(0), ArrowGauge::GLYPH_UP);
    EXPECT_EQ(gauge.getSteeringString(90), ArrowGauge::GLYPH_RIGHT);
    EXPECT_EQ(gauge.getSteeringString(180), ArrowGauge::GLYPH_DOWN);
    EXPECT_EQ(gauge.getSteeringString(270), ArrowGauge::GLYPH_LEFT);
}

TEST(ArrowGaugeTest, OctantStepOverAtFence) {
    ArrowGauge gauge;
    EXPECT_EQ(gauge.getSteeringString(22), ArrowGauge::GLYPH_UP);
    EXPECT_EQ(gauge.getSteeringString(23), ArrowGauge::GLYPH_UP_RIGHT);
    // The top wrap: past the last octant fence the glyph returns to up
    EXPECT_EQ(gauge.getSteeringString(337), ArrowGauge::GLYPH_UP_LEFT);
    EXPECT_EQ(gauge.getSteeringString(338), ArrowGauge::GLYPH_UP);
}

// ---------------------------------------------------------------------------
// Style selection (factory + CLI name parsing)
// ---------------------------------------------------------------------------

TEST(SteeringGaugeTest, FactoryBuildsRequestedStyle) {
    const auto braille = presentation::makeSteeringGauge(SteeringStyle::Braille);
    const auto arrows = presentation::makeSteeringGauge(SteeringStyle::Arrows);
    // 90 deg separates the styles: braille 3-o'clock vs the plain right arrow
    EXPECT_EQ(braille->getSteeringString(90), SteeringGauge::GLYPH_3);
    EXPECT_EQ(arrows->getSteeringString(90), ArrowGauge::GLYPH_RIGHT);
}

TEST(SteeringGaugeTest, ParseSteeringStyleAcceptsBothNamesAndFailsFast) {
    EXPECT_EQ(presentation::ParseSteeringStyle("braille"), SteeringStyle::Braille);
    EXPECT_EQ(presentation::ParseSteeringStyle("arrows"), SteeringStyle::Arrows);
    EXPECT_THROW(presentation::ParseSteeringStyle("spin"), std::invalid_argument);
}
