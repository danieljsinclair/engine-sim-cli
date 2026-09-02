#include <gtest/gtest.h>

#include "presentation/SteeringGauge.h"

using presentation::SteeringGauge;

// Owner-authored fence-post semantics, converted from the author's Catch2
// draft to the house gTest style. The clock face has 12 sectors of 30 deg;
// the fences sit at 14/44/74/.../344 deg (inclusive).

TEST(SteeringGaugeTest, TdcSectorSpanStability) {
    SteeringGauge gauge;
    EXPECT_EQ(gauge.getSteeringString(0), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(7), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(14), SteeringGauge::GLYPH_12);
}

TEST(SteeringGaugeTest, OneDegreeStepOverBetweenSectors) {
    SteeringGauge gauge;
    const auto& map = SteeringGauge::GetAngleMap();
    const int tdcCeiling = map[0].maxDegrees;    // 14
    const int oneOclockFloor = tdcCeiling + 1;   // 15

    EXPECT_EQ(gauge.getSteeringString(tdcCeiling), map[0].graphic);
    EXPECT_EQ(gauge.getSteeringString(oneOclockFloor), map[1].graphic);
}

TEST(SteeringGaugeTest, QuadrantBoundariesAroundThreeOclock) {
    SteeringGauge gauge;
    const auto& map = SteeringGauge::GetAngleMap();
    // 90 deg falls into the entry at index 3 (75 to 104 degrees)
    EXPECT_EQ(gauge.getSteeringString(75), map[3].graphic);
    EXPECT_EQ(gauge.getSteeringString(90), map[3].graphic);
    EXPECT_EQ(gauge.getSteeringString(104), map[3].graphic);
    // Stepping over 104 deg moves cleanly to the 4 o'clock sector
    EXPECT_EQ(gauge.getSteeringString(105), map[4].graphic);
}

TEST(SteeringGaugeTest, PositiveMultiTurnOverflowWraps) {
    SteeringGauge gauge;
    EXPECT_EQ(gauge.getSteeringString(360), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(720), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(3614), SteeringGauge::GLYPH_12); // TDC ceiling after 10 turns
}

TEST(SteeringGaugeTest, NegativeMultiTurnUnderflowWraps) {
    SteeringGauge gauge;
    EXPECT_EQ(gauge.getSteeringString(-360), SteeringGauge::GLYPH_12);
    EXPECT_EQ(gauge.getSteeringString(-720), SteeringGauge::GLYPH_12);
    // -90 deg wraps onto the identical 270 deg asset vector (9 o'clock)
    EXPECT_EQ(gauge.getSteeringString(-90), gauge.getSteeringString(270));
}
