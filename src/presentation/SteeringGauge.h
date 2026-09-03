// SteeringGauge.h - steering-angle gauge styles
//
// This file is UTF-8 encoded; every glyph is written as its literal character
// (never a \x escape sequence) so the definitions stay human-readable.
//
// Two owner-selectable styles share one algorithm:
//   - ArrowGauge:    8-way directional arrows, 45 deg sectors (default)
//   - SteeringGauge: 12-position two-cell braille clock face (--steering-style braille)
// Both use the owner-authored fence-post semantics: normalise the angle into
// [0, 360) and walk an ordered map of inclusive ceilings - the first entry
// whose maxDegrees >= the angle wins.

#ifndef STEERING_GAUGE_H
#define STEERING_GAUGE_H

#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace presentation {

// A steering gauge resolves a steering-wheel angle (degrees, signed, any
// magnitude) to a direction glyph. The sector database is the style; the
// lookup algorithm below is shared and owner-authored.
class ISteeringGauge {
public:
    struct MapEntry {
        int maxDegrees;      // The upper inclusive angle threshold for this sector
        std::string graphic; // The direct text string (supports 1, 2, or more characters)
    };

    virtual ~ISteeringGauge() = default;

    // The ordered sector database for this style (ascending maxDegrees).
    virtual const std::vector<MapEntry>& GetAngleMap() const = 0;

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
        return GetAngleMap().front().graphic;
    }
};

// Owner-authored design: the steering angle as a clock face, each hour
// position rendered as a two-cell braille glyph (braille blank U+2800 pads
// the empty cell so every glyph is the same width). 0 deg = 12 o'clock
// (straight ahead); 90 deg = 3 o'clock (full right); sector fences sit at
// 14/44/74/.../344 deg (30 deg sectors, fence-post inclusive).
class SteeringGauge final : public ISteeringGauge {
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

    // The single, simple database matching an angle ceiling directly to its graphic
    static const std::vector<MapEntry>& AngleMap() {
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

    const std::vector<MapEntry>& GetAngleMap() const override {
        return AngleMap();
    }
};

// The v2 arrow style, revived as a selectable variant: 8 directional arrows
// on 45 deg octants, converted to the shared fence-post map (sector centres
// at 0/45/90/.../315 deg, fences at 22/67/112/.../337 deg inclusive).
// Single glyphs; 338-359 wraps back to up (the map's closing entry).
class ArrowGauge final : public ISteeringGauge {
public:
    // Cardinal and intercardinal arrow glyphs (literal UTF-8 characters)
    static inline const auto GLYPH_UP          = "↑"; // straight ahead / TDC
    static inline const auto GLYPH_UP_RIGHT    = "↗"; // 45 deg
    static inline const auto GLYPH_RIGHT       = "→"; // 90 deg full right
    static inline const auto GLYPH_DOWN_RIGHT  = "↘"; // 135 deg
    static inline const auto GLYPH_DOWN        = "↓"; // 180 deg full bottom
    static inline const auto GLYPH_DOWN_LEFT   = "↙"; // 225 deg
    static inline const auto GLYPH_LEFT        = "←"; // 270 deg full left
    static inline const auto GLYPH_UP_LEFT     = "↖"; // 315 deg

    static const std::vector<MapEntry>& AngleMap() {
        static const std::vector<MapEntry> angleMap = {
            {22,  GLYPH_UP},
            {67,  GLYPH_UP_RIGHT},
            {112, GLYPH_RIGHT},
            {157, GLYPH_DOWN_RIGHT},
            {202, GLYPH_DOWN},
            {247, GLYPH_DOWN_LEFT},
            {292, GLYPH_LEFT},
            {337, GLYPH_UP_LEFT},
            {359, GLYPH_UP}
        };
        return angleMap;
    }

    const std::vector<MapEntry>& GetAngleMap() const override {
        return AngleMap();
    }
};

// Selectable gauge styles (--steering-style braille|arrows).
enum class SteeringStyle {
    Braille,
    Arrows
};

// Parses the CLI style name. The CLI layer validates membership, so an
// unknown name here is a programmer error: fail fast, never guess.
inline SteeringStyle ParseSteeringStyle(const std::string& name) {
    if (name == "braille") {
        return SteeringStyle::Braille;
    }
    if (name == "arrows") {
        return SteeringStyle::Arrows;
    }
    throw std::invalid_argument("Unknown steering style: " + name);
}

// Factory: the single construction seam for gauge styles.
inline std::unique_ptr<ISteeringGauge> makeSteeringGauge(SteeringStyle style) {
    switch (style) {
    case SteeringStyle::Braille:
        return std::make_unique<SteeringGauge>();
    case SteeringStyle::Arrows:
        return std::make_unique<ArrowGauge>();
    }
    return std::make_unique<SteeringGauge>();
}

} // namespace presentation

#endif // STEERING_GAUGE_H
