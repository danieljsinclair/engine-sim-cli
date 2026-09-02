// SteeringGauge.h - 12-position braille clock-face steering gauge

#ifndef STEERING_GAUGE_H
#define STEERING_GAUGE_H

#include <string>
#include <vector>

namespace presentation {

// Owner-authored design: the steering angle as a clock face, each hour
// position rendered as a two-cell braille glyph (braille blank U+2800 pads
// the empty cell so every glyph is the same width). 0 deg = 12 o'clock
// (straight ahead); 90 deg = 3 o'clock (full right); sector fences sit at
// 14/44/74/.../344 deg (30 deg sectors, fence-post inclusive).
class SteeringGauge {
public:
    // Simple, clear definition assets
    static inline const auto GLYPH_12  = "⠉⠉"; // 12 o'clock / Top Dead Center
    static inline const auto GLYPH_1   = "⠈⠙"; // 1 o'clock
    static inline const auto GLYPH_2   = "⠀⠹"; // 2 o'clock
    static inline const auto GLYPH_3   = "⠀⢸"; // 3 o'clock / 90 degrees full right
    static inline const auto GLYPH_4   = "⠀⣰"; // 4 o'clock
    static inline const auto GLYPH_5   = "⢀⣠"; // 5 o'clock
    static inline const auto GLYPH_6   = "⣀⣀"; // 6 o'clock / 180 degrees full bottom
    static inline const auto GLYPH_7   = "⣄⡀"; // 7 o'clock
    static inline const auto GLYPH_8   = "⣆⠀"; // 8 o'clock
    static inline const auto GLYPH_9   = "⡇⠀"; // 9 o'clock / 270 degrees full left
    static inline const auto GLYPH_10  = "⠏⠀"; // 10 o'clock
    static inline const auto GLYPH_11  = "⠋⠁"; // 11 o'clock

    struct MapEntry {
        int maxDegrees;      // The upper inclusive angle threshold for this sector
        std::string graphic; // The direct text string (supports 1, 2, or more characters)
    };

    // The single, simple database matching an angle ceiling directly to its graphic
    static const std::vector<MapEntry>& GetAngleMap() {
        static const std::vector<MapEntry> angleMap = {
            {14,  GLYPH_12},
            {44,  GLYPH_1},
            {74,  GLYPH_2},
            {104, GLYPH_3},
            {134, GLYPH_4},
            {164, GLYPH_5},
            {194, GLYPH_6},
            {224, GLYPH_7},
            {254, GLYPH_8},
            {284, GLYPH_9},
            {314, GLYPH_10},
            {344, GLYPH_11},
            {359, GLYPH_12}
        };
        return angleMap;
    }

    // Resolves and normalises any angle, returning the direct string
    std::string getSteeringString(int angle) const {
        int normalizedAngle = angle % 360;
        if (normalizedAngle < 0) {
            normalizedAngle += 360;
        }

        for (const auto& entry : GetAngleMap()) {
            if (normalizedAngle <= entry.maxDegrees) {
                return entry.graphic;
            }
        }
        return GLYPH_12;
    }
};

} // namespace presentation

#endif // STEERING_GAUGE_H
