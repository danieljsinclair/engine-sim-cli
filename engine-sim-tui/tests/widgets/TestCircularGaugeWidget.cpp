#include <gtest/gtest.h>
#include "engine_sim_tui/widgets/CircularGaugeWidget.h"
#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>

using namespace engine_sim_tui;

class CircularGaugeWidgetTest : public ::testing::Test {
protected:
    widgets::CircularGaugeWidget m_gauge;
};

TEST_F(CircularGaugeWidgetTest, RendersBasicGauge) {
    auto element = m_gauge.Render();
    ASSERT_NE(element, nullptr);

    // Render to screen to verify it works
    auto screen = ftxui::Screen::Create(
        ftxui::Dimension::Fit(element)
    );
    ftxui::Render(screen, element);

    EXPECT_GT(screen.dimx(), 0);
    EXPECT_GT(screen.dimy(), 0);
}

TEST_F(CircularGaugeWidgetTest, FluentInterface) {
    auto& gauge = m_gauge
        .SetTitle("TEST")
        .SetUnit("RPM")
        .SetRange(0, 7000)
        .SetPrecision(0)
        .SetSize(30, 15)
        .SetColor(ftxui::Color::Red);

    // Should be able to render
    auto element = gauge.Render();
    ASSERT_NE(element, nullptr);
}

TEST_F(CircularGaugeWidgetTest, SetValueUpdatesGauge) {
    m_gauge.SetRange(0, 100);
    m_gauge.SetValue(50);

    auto element = m_gauge.Render();
    ASSERT_NE(element, nullptr);
}

TEST_F(CircularGaugeWidgetTest, ValueClampingDoesNotCrash) {
    m_gauge.SetRange(0, 100);

    // These should not crash - values are clamped internally
    m_gauge.SetValue(-10);
    m_gauge.SetValue(150);

    // Just verify rendering works
    auto element = m_gauge.Render();
    ASSERT_NE(element, nullptr);
}

TEST_F(CircularGaugeWidgetTest, MultipleSetValueCalls) {
    m_gauge.SetRange(0, 7000);

    // Should not crash on multiple updates
    for (int i = 0; i <= 7000; i += 100) {
        m_gauge.SetValue(i);
        auto element = m_gauge.Render();
        ASSERT_NE(element, nullptr);
    }
}
