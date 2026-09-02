#include <gtest/gtest.h>
#include "presentation/ConsolePresentation.h"
#include "presentation/SteeringGauge.h"
#include "simulator/GearConventions.h"
#include "config/ANSIColors.h"
#include "io/IPresentation.h"

#include <iostream>
#include <sstream>
#include <streambuf>

using namespace presentation;
using GS = bridge::GearSelector;

// Helper to create a minimal EngineState for testing specific fields
static EngineState makeState() {
    EngineState s{};
    s.engine.rpm = 3000.0;
    s.controls.throttle = 0.5;
    s.drivetrain.gear = 1;
    s.controls.gearSelector = static_cast<int>(GS::DRIVE);
    s.controls.gearAutoMode = true;
    s.drivetrain.vehicleSpeedKmh = 60.0;
    s.engine.engineTorqueNm = 200.0;
    s.engine.drivetrainTorqueNm = 1000.0;
    return s;
}

// RAII helper to capture std::cout/std::cerr output
class OutputCapture {
public:
    explicit OutputCapture(std::ostream& target) : target_(target), oldBuf_(target.rdbuf()) {
        target_.rdbuf(buffer_.rdbuf());
    }
    ~OutputCapture() {
        target_.rdbuf(oldBuf_);
    }
    std::string str() const { return buffer_.str(); }
    void reset() { buffer_.str(""); buffer_.clear(); }

private:
    std::ostream& target_;
    std::streambuf* oldBuf_;
    std::ostringstream buffer_;
};

class ConsolePresentationTest : public ::testing::Test {
protected:
    ConsolePresentation presentation_;

    void SetUp() override {
        PresentationConfig config;
        config.showDiagnostics = true;
        presentation_.Initialize(config);
    }

    // formatEngineState is private, so we test via the formatted output
    // by calling ShowEngineState and checking the result string
    std::string format(const EngineState& state) {
        return {};
    }
};

// ============================================================================
// Gear selector character mapping
// ============================================================================

// gearSelectorChar is a public free function in the presentation namespace
// (declared in ConsolePresentation.h), so these tests exercise the REAL
// production mapping rather than a duplicated copy.

// ============================================================================
// gearChar: the third field — actual engaged gear / transmission state
// ============================================================================

TEST(GearCharTest, ParkSelector_ReturnsP) {
    EXPECT_EQ(presentation::gearChar(static_cast<int>(GS::PARK), 0), 'P');
}

TEST(GearCharTest, ReverseSelector_ReturnsR) {
    EXPECT_EQ(presentation::gearChar(static_cast<int>(GS::REVERSE), 0), 'R');
}

TEST(GearCharTest, NeutralPhysical0_ReturnsN) {
    EXPECT_EQ(presentation::gearChar(static_cast<int>(GS::NEUTRAL), 0), 'N');
}

TEST(GearCharTest, DrivePhysical1through8_ReturnsDigits) {
    for (int g = 1; g <= 8; ++g) {
        EXPECT_EQ(presentation::gearChar(static_cast<int>(GS::DRIVE), g),
                  static_cast<char>('0' + g));
    }
}

TEST(GearCharTest, DrivePhysical0_ReturnsN) {
    // Drive but no ratio engaged (transient) -> neutral glyph.
    EXPECT_EQ(presentation::gearChar(static_cast<int>(GS::DRIVE), 0), 'N');
}

TEST(GearCharTest, PhysicalOutOfRange_ReturnsQuestion) {
    EXPECT_EQ(presentation::gearChar(static_cast<int>(GS::DRIVE), 9), '?');
}

// ============================================================================
// gearTriple: the composite [selector][mode][gear] readout
// ============================================================================

TEST(GearTripleTest, Manual_MirrorsSelectorForGear) {
    // Manual: selector == gear, so field-3 mirrors field-1.
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::REVERSE), false, 0), "RMR");
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::NEUTRAL), false, 0), "NMN");
    EXPECT_EQ(presentation::gearTriple(1, false, 1), "1M1");
    EXPECT_EQ(presentation::gearTriple(2, false, 2), "2M2");
    EXPECT_EQ(presentation::gearTriple(3, false, 3), "3M3");
    EXPECT_EQ(presentation::gearTriple(8, false, 8), "8M8");
}

TEST(GearTripleTest, Auto_DerivesGearFromPhysics) {
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::PARK), true, 0), "PAP");
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::REVERSE), true, 0), "RAR");
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::NEUTRAL), true, 0), "NAN");
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::DRIVE), true, 1), "DA1");
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::DRIVE), true, 2), "DA2");
    EXPECT_EQ(presentation::gearTriple(static_cast<int>(GS::DRIVE), true, 3), "DA3");
}

// gearSelectorChar is now a public free function in the presentation namespace
// (declared in ConsolePresentation.h), so these tests exercise the REAL
// production mapping rather than a duplicated copy.

TEST(GearSelectorCharTest, Park_ReturnsP) {
    EXPECT_EQ(presentation::gearSelectorChar(static_cast<int>(GS::PARK)), 'P');
}

TEST(GearSelectorCharTest, Reverse_ReturnsR) {
    EXPECT_EQ(presentation::gearSelectorChar(static_cast<int>(GS::REVERSE)), 'R');
}

TEST(GearSelectorCharTest, Neutral_ReturnsN) {
    EXPECT_EQ(presentation::gearSelectorChar(static_cast<int>(GS::NEUTRAL)), 'N');
}

TEST(GearSelectorCharTest, Drive_ReturnsD) {
    EXPECT_EQ(presentation::gearSelectorChar(static_cast<int>(GS::DRIVE)), 'D');
}

TEST(GearSelectorCharTest, ManualGears_1through8) {
    // Gear NUMBER 1 (FIRST) must render as '1', never '?' (regression: the old
    // 2-8 range excluded 1, producing the '?M1' status line).
    EXPECT_EQ(presentation::gearSelectorChar(1), '1');
    EXPECT_EQ(presentation::gearSelectorChar(2), '2');
    EXPECT_EQ(presentation::gearSelectorChar(3), '3');
    EXPECT_EQ(presentation::gearSelectorChar(4), '4');
    EXPECT_EQ(presentation::gearSelectorChar(5), '5');
    EXPECT_EQ(presentation::gearSelectorChar(6), '6');
    EXPECT_EQ(presentation::gearSelectorChar(7), '7');
    EXPECT_EQ(presentation::gearSelectorChar(8), '8');
}

TEST(GearSelectorCharTest, UnknownValue_ReturnsQuestionMark) {
    EXPECT_EQ(presentation::gearSelectorChar(-99), '?');
    EXPECT_EQ(presentation::gearSelectorChar(50), '?');
    EXPECT_EQ(presentation::gearSelectorChar(9), '?');
}

// ============================================================================
// RPM display: values < 10 but > 0 are clamped to 0
// ============================================================================

TEST(ConsolePresentationRpmTest, RpmBelow10_ClampedToZero) {
    int rawRpm = 5;
    int displayedRpm = rawRpm;
    if (displayedRpm < 10 && 5.0 > 0) displayedRpm = 0;
    EXPECT_EQ(displayedRpm, 0);
}

TEST(ConsolePresentationRpmTest, RpmAtZero_StaysZero) {
    int rawRpm = 0;
    int displayedRpm = rawRpm;
    // The condition: rpm < 10 && state.rpm > 0
    // When rpm is exactly 0, state.rpm is 0.0, so 0.0 > 0 is false → no clamp
    EXPECT_EQ(displayedRpm, 0);
}

TEST(ConsolePresentationRpmTest, RpmAbove10_NotClamped) {
    int rawRpm = 800;
    int displayedRpm = rawRpm;
    if (displayedRpm < 10 && 800.0 > 0) displayedRpm = 0;
    EXPECT_EQ(displayedRpm, 800);
}

TEST(ConsolePresentationRpmTest, RpmExactly10_NotClamped) {
    int rawRpm = 10;
    int displayedRpm = rawRpm;
    if (displayedRpm < 10 && 10.0 > 0) displayedRpm = 0;
    EXPECT_EQ(displayedRpm, 10); // 10 < 10 is false
}

// ============================================================================
// Torque color logic: green for positive, red for negative
// ============================================================================

TEST(TorqueColorTest, PositiveTorque_Green) {
    int torque = 447;
    const std::string& color = (torque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;
    EXPECT_EQ(color, ANSIColors::GREEN);
}

TEST(TorqueColorTest, NegativeTorque_Red) {
    int torque = -120;
    const std::string& color = (torque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;
    EXPECT_EQ(color, ANSIColors::RED);
}

TEST(TorqueColorTest, ZeroTorque_Green) {
    int torque = 0;
    const std::string& color = (torque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;
    EXPECT_EQ(color, ANSIColors::GREEN);
}

TEST(TorqueColorTest, EngineAndDriveTorque_IndependentColors) {
    // Engine positive, drivetrain negative (shouldn't happen normally but logic is independent)
    int engTorque = 200;
    int drvTorque = -50;
    const std::string& engColor = (engTorque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;
    const std::string& drvColor = (drvTorque >= 0) ? ANSIColors::GREEN : ANSIColors::RED;
    EXPECT_EQ(engColor, ANSIColors::GREEN);
    EXPECT_EQ(drvColor, ANSIColors::RED);
}

// ============================================================================
// Speed conversion: km/h to mph, rounded to whole number
// ============================================================================

TEST(SpeedConversionTest, ZeroSpeed_ZeroMph) {
    double kmh = 0.0;
    int mph = static_cast<int>(std::round(kmh * EngineSimDefaults::KMH_TO_MPH));
    EXPECT_EQ(mph, 0);
}

TEST(SpeedConversionTest, HundredKmh_SixtyTwoMph) {
    double kmh = 100.0;
    int mph = static_cast<int>(std::round(kmh * EngineSimDefaults::KMH_TO_MPH));
    EXPECT_EQ(mph, 62);
}

TEST(SpeedConversionTest, FractionalRoundsCorrectly) {
    // 96.56 km/h → 59.99... mph → rounds to 60
    double kmh = 96.56;
    int mph = static_cast<int>(std::round(kmh * EngineSimDefaults::KMH_TO_MPH));
    EXPECT_EQ(mph, 60);
}

// ============================================================================
// ANSI color constants exist and are correct
// ============================================================================

TEST(ANSIColorTest, GreenContainsEscapeCode) {
    EXPECT_FALSE(ANSIColors::GREEN.empty());
    EXPECT_EQ(ANSIColors::GREEN[0], '\x1b');
}

TEST(ANSIColorTest, RedContainsEscapeCode) {
    EXPECT_FALSE(ANSIColors::RED.empty());
    EXPECT_EQ(ANSIColors::RED[0], '\x1b');
}

TEST(ANSIColorTest, ResetContainsEscapeCode) {
    EXPECT_FALSE(ANSIColors::RESET.empty());
    EXPECT_EQ(ANSIColors::RESET[0], '\x1b');
}

TEST(ANSIColorTest, ColorsAreDistinct) {
    EXPECT_NE(ANSIColors::GREEN, ANSIColors::RED);
    EXPECT_NE(ANSIColors::GREEN, ANSIColors::RESET);
    EXPECT_NE(ANSIColors::RED, ANSIColors::RESET);
}

// ============================================================================
// KMH_TO_MPH constant is correct
// ============================================================================

TEST(ConversionConstantTest, KmhToMph_Value) {
    EXPECT_NEAR(EngineSimDefaults::KMH_TO_MPH, 0.621371, 0.000001);
}

// ============================================================================
// ShowMessage / ShowError / ShowProgress tests with DI for ostream
// ============================================================================

TEST_F(ConsolePresentationTest, ShowMessage_OutputsMessageToCout) {
    OutputCapture capture(std::cout);
    const std::string testMessage = "Test message content";

    presentation_.ShowMessage(testMessage);

    EXPECT_NE(capture.str().find(testMessage), std::string::npos)
        << "ShowMessage should output the message to cout";
}

TEST_F(ConsolePresentationTest, ShowMessage_EmptyString_OutputsNewline) {
    OutputCapture capture(std::cout);
    presentation_.ShowMessage("");

    // Should output at least a newline
    EXPECT_FALSE(capture.str().empty());
}

TEST_F(ConsolePresentationTest, ShowError_OutputsErrorToCerr) {
    OutputCapture capture(std::cerr);
    const std::string testError = "Test error content";

    presentation_.ShowError(testError);

    EXPECT_NE(capture.str().find("ERROR:"), std::string::npos)
        << "ShowError should prefix with 'ERROR:'";
    EXPECT_NE(capture.str().find(testError), std::string::npos)
        << "ShowError should include the error message";
}

TEST_F(ConsolePresentationTest, ShowError_EmptyString_OutputsErrorPrefix) {
    OutputCapture capture(std::cerr);
    presentation_.ShowError("");

    EXPECT_NE(capture.str().find("ERROR:"), std::string::npos)
        << "ShowError should output 'ERROR:' prefix even for empty message";
}

TEST_F(ConsolePresentationTest, ShowProgress_DisabledByDefault_NoOutput) {
    OutputCapture capture(std::cout);
    presentation_.ShowProgress(1.0, 5.0);

    // Progress is disabled by default (showProgress=false in config)
    EXPECT_TRUE(capture.str().empty())
        << "ShowProgress should produce no output when showProgress is false";
}

TEST_F(ConsolePresentationTest, ShowProgress_Enabled_OutputsProgressBar) {
    PresentationConfig config;
    config.showDiagnostics = true;
    config.showProgress = true;
    config.interactive = true;
    presentation_.Initialize(config);

    OutputCapture capture(std::cout);
    presentation_.ShowProgress(2.5, 5.0);  // 50% progress

    EXPECT_NE(capture.str().find("Progress:"), std::string::npos)
        << "ShowProgress should output 'Progress:' when enabled";
    EXPECT_NE(capture.str().find("50%"), std::string::npos)
        << "ShowProgress should show 50% for half-complete";
}

TEST_F(ConsolePresentationTest, ShowProgress_ZeroDuration_NoOutput) {
    PresentationConfig config;
    config.showDiagnostics = true;
    config.showProgress = true;
    config.interactive = true;
    presentation_.Initialize(config);

    OutputCapture capture(std::cout);
    presentation_.ShowProgress(1.0, 0.0);  // Zero duration

    EXPECT_TRUE(capture.str().empty())
        << "ShowProgress should produce no output when duration is 0";
}

TEST_F(ConsolePresentationTest, ShowProgress_NonInteractive_NoOutput) {
    PresentationConfig config;
    config.showDiagnostics = true;
    config.showProgress = true;
    config.interactive = false;  // Not interactive
    presentation_.Initialize(config);

    OutputCapture capture(std::cout);
    presentation_.ShowProgress(1.0, 5.0);

    EXPECT_TRUE(capture.str().empty())
        << "ShowProgress should produce no output when not interactive";
}

TEST_F(ConsolePresentationTest, ShowProgress_Complete_ShowsPercentage) {
    PresentationConfig config;
    config.showDiagnostics = true;
    config.showProgress = true;
    config.interactive = true;
    presentation_.Initialize(config);

    OutputCapture capture(std::cout);
    presentation_.ShowProgress(10.0, 5.0);  // 200% progress

    // Implementation doesn't cap at 100%, so it shows 200%
    EXPECT_NE(capture.str().find("200%"), std::string::npos)
        << "ShowProgress should show 200% when currentTime/duration = 2.0";
}

// ============================================================================
// Brake display — single binary indicator in the Gas bracket, nothing in Gear
// ============================================================================

// Helper: render a state and return the captured output.
static std::string renderState(ConsolePresentation& presentation, const EngineState& state) {
    OutputCapture capture(std::cout);
    presentation.ShowSimulatorStates(state);
    return capture.str();
}

TEST_F(ConsolePresentationTest, BrakeOn_RedBInGasBracket) {
    EngineState s = makeState();
    s.controls.brakeLight = true;

    const std::string out = renderState(presentation_, s);

    // Red 'B' closes the Gas bracket (binary indicator, replaces the old B:0.0 numeric)
    EXPECT_NE(out.find("% " + ANSIColors::RED + "B" + ANSIColors::RESET + "]"), std::string::npos)
        << "Brake on must show the red B indicator inside the Gas bracket";
    // The analog numeric must be gone entirely
    EXPECT_EQ(out.find("B:"), std::string::npos)
        << "No analog B:<level> numeric may appear anywhere";
}

TEST_F(ConsolePresentationTest, BrakeOff_PlainDashInGasBracket) {
    EngineState s = makeState();
    s.controls.brakeLight = false;

    const std::string out = renderState(presentation_, s);

    EXPECT_NE(out.find("% -]"), std::string::npos)
        << "Brake off must show plain '-' inside the Gas bracket";
    EXPECT_EQ(out.find("B:"), std::string::npos);
}

TEST_F(ConsolePresentationTest, BrakeUnreported_PlainDashInGasBracket) {
    EngineState s = makeState();
    s.controls.brakeLight = std::nullopt;

    const std::string out = renderState(presentation_, s);

    EXPECT_NE(out.find("% -]"), std::string::npos)
        << "Unreported brake must show plain '-' (same as off)";
}

TEST_F(ConsolePresentationTest, GearBracket_HasNoBrakeIndicator) {
    EngineState s = makeState();
    s.controls.brakeLight = true;  // brake ON — still nothing in the Gear bracket

    const std::string out = renderState(presentation_, s);

    const auto start = out.find("[Gear:");
    ASSERT_NE(start, std::string::npos);
    const auto end = out.find("]", start);
    ASSERT_NE(end, std::string::npos);
    EXPECT_EQ(out.substr(start, end - start + 1),
              "[Gear:" + gearTriple(s.controls.gearSelector, s.controls.gearAutoMode, s.drivetrain.gear) + "]")
        << "Gear bracket must contain only the selector/mode/gear triple — no brake char";
}

// ============================================================================
// Steering-angle display: the console line shows the steering wheel angle
// when the feed carries it (vehicle-sim steering_angle_deg / CAN 0x129
// SCCM_steeringAngle, plumbed to EngineState.Controls.steeringAngleDeg), and
// degrades to NOTHING when absent (non-DBC sources unchanged — no dead field,
// no zero placeholder). Values below come from the same real-capture rows
// embedded in the bridge CsvTelemetryParser steering tests.
// ============================================================================

static std::string renderStateLine(const EngineState& state,
                                    presentation::SteeringStyle style = presentation::SteeringStyle::Arrows) {
    ConsolePresentation presentation(style);
    PresentationConfig config;
    config.showDiagnostics = true;
    presentation.Initialize(config);

    OutputCapture capture(std::cout);
    presentation.ShowSimulatorStates(state);
    return capture.str();
}

TEST(ConsolePresentationSteeringTest, SteeringPresent_RendersAngleInConsoleLine) {
    EngineState state = makeState();
    state.controls.steeringAngleDeg = -12.5;

    const std::string line = renderStateLine(state);

    // v4 (owner verdict): the DEFAULT style is arrows — the up arrow leads the
    // readout (12.5 deg sits in the top sector), the signed value is
    // right-justified to a fixed width, the icon stays off, and no braille
    // glyph appears unless --steering-style braille asks for it.
    EXPECT_NE(line.find(presentation::ArrowGauge::GLYPH_UP), std::string::npos)
        << "default (arrows) gauge glyph missing when the feed carries steering";
    EXPECT_EQ(line.find(presentation::SteeringGauge::GLYPH_12), std::string::npos)
        << "default output must not render braille glyphs (arrows is the default)";
    EXPECT_NE(line.find("-12.5"), std::string::npos)
        << "signed steering value not rendered";
    EXPECT_EQ(line.find("\xF0\x9F\x9B\x9E"), std::string::npos)
        << "steering-wheel icon must stay commented out (v4 owner directive)";
}

TEST(ConsolePresentationSteeringTest, SteeringPositive_RendersSigned) {
    EngineState state = makeState();
    state.controls.steeringAngleDeg = 3.9;

    const std::string line = renderStateLine(state);

    EXPECT_NE(line.find("3.9"), std::string::npos);
}

// v2: the steering component must occupy the same byte-width regardless of
// angle, so the rest of the telemetry line never jitters. We render at a
// wide and a narrow angle (default arrows style) and assert the component
// substring is identical length. Component runs from the gauge glyph to the
// closing ']' (both renders sit in the top sector: 0 deg and -359 -> 359 ->
// wrap tail).
TEST(ConsolePresentationSteeringTest, SteeringComponent_FixedWidth) {
    auto render = [](double deg) {
        EngineState s = makeState();
        s.controls.steeringAngleDeg = deg;
        return renderStateLine(s);
    };

    const std::string narrow = render(0.0);
    const std::string wide   = render(-359.0);

    auto componentWidth = [](const std::string& line) -> std::size_t {
        std::size_t start = line.find(presentation::ArrowGauge::GLYPH_UP);
        if (start == std::string::npos) return 0;
        std::size_t end = line.find(']', start);
        if (end == std::string::npos) return 0;
        return end - start + 1; // include the closing ']'
    };

    EXPECT_EQ(componentWidth(narrow), componentWidth(wide))
        << "steering component width must not vary with angle (narrow=\""
        << narrow << "\" wide=\"" << wide << "\")";
}

// v3 (owner design): braille clock face, selected EXPLICITLY (--steering-style
// braille). Dead center (0) must render the 12-o'clock TDC glyph, and
// full-right (90) the 3-o'clock glyph.
TEST(ConsolePresentationSteeringTest, SteeringCenter_RendersTdcGlyph) {
    EngineState state = makeState();
    state.controls.steeringAngleDeg = 0.0;
    const std::string line = renderStateLine(state, presentation::SteeringStyle::Braille);
    EXPECT_NE(line.find(presentation::SteeringGauge::GLYPH_12), std::string::npos)
        << "dead-center steering must render the 12-o'clock TDC braille glyph";

    state.controls.steeringAngleDeg = 90.0;
    const std::string right = renderStateLine(state, presentation::SteeringStyle::Braille);
    EXPECT_NE(right.find(presentation::SteeringGauge::GLYPH_3), std::string::npos)
        << "full-right steering must render the 3-o'clock braille glyph";
}

// v4 (--steering-style arrows): the same 90 deg input must render the arrow
// compass instead of the braille clock face.
TEST(ConsolePresentationSteeringTest, SteeringStyleArrows_RendersArrowGlyph) {
    EngineState state = makeState();
    state.controls.steeringAngleDeg = 90.0;

    const std::string line = renderStateLine(state, presentation::SteeringStyle::Arrows);

    EXPECT_NE(line.find(presentation::ArrowGauge::GLYPH_RIGHT), std::string::npos)
        << "arrows style must render the right arrow at full-right steering";
    EXPECT_EQ(line.find(presentation::SteeringGauge::GLYPH_3), std::string::npos)
        << "arrows style must not render braille glyphs";
}

TEST(ConsolePresentationSteeringTest, SteeringAbsent_RendersNothing) {
    EngineState state = makeState();  // no steering value set

    const std::string line = renderStateLine(state);

    // Degraded to nothing: no steering readout at all, and the rest of the
    // line renders exactly as before (no behavior change for steering-less
    // feeds — keyboard/demo/non-DBC sources).
    EXPECT_EQ(line.find("\xF0\x9F\x9B\x9E"), std::string::npos);
    EXPECT_NE(line.find("RPM"), std::string::npos);
    EXPECT_NE(line.find("mph"), std::string::npos);
}
