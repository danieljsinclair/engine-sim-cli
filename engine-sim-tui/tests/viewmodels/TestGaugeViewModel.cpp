#include <gtest/gtest.h>
#include "engine_sim_tui/viewmodels/GaugeViewModel.h"

using namespace engine_sim_tui;

class GaugeViewModelTest : public ::testing::Test {
protected:
    viewmodels::GaugeViewModel m_viewModel;
};

TEST_F(GaugeViewModelTest, DefaultRange) {
    EXPECT_EQ(m_viewModel.GetMin(), 0.0);
    EXPECT_EQ(m_viewModel.GetMax(), 100.0);
}

TEST_F(GaugeViewModelTest, SetValueReturnsTrueOnFirstSet) {
    bool changed = m_viewModel.SetValue(50.0);
    EXPECT_TRUE(changed);
    EXPECT_EQ(m_viewModel.GetValue(), 50.0);
}

TEST_F(GaugeViewModelTest, SetValueReturnsFalseForSmallChanges) {
    m_viewModel.SetValue(50.0);
    bool changed = m_viewModel.SetValue(50.5); // Small change < 1%
    EXPECT_FALSE(changed);
}

TEST_F(GaugeViewModelTest, SetValueReturnsTrueForLargeChanges) {
    m_viewModel.SetValue(50.0);
    bool changed = m_viewModel.SetValue(55.0); // Large change > 1%
    EXPECT_TRUE(changed);
}

TEST_F(GaugeViewModelTest, SetValueClampsToRange) {
    m_viewModel.SetRange(0, 100);

    m_viewModel.SetValue(-10.0);
    EXPECT_EQ(m_viewModel.GetValue(), 0.0);

    m_viewModel.SetValue(150.0);
    EXPECT_EQ(m_viewModel.GetValue(), 100.0);
}

TEST_F(GaugeViewModelTest, SetRangeChangesRange) {
    m_viewModel.SetRange(0, 7000);
    EXPECT_EQ(m_viewModel.GetMin(), 0.0);
    EXPECT_EQ(m_viewModel.GetMax(), 7000.0);
}

TEST_F(GaugeViewModelTest, SetRangeClampsCurrentValue) {
    m_viewModel.SetValue(50.0);
    m_viewModel.SetRange(0, 10);  // Current value outside new range
    EXPECT_EQ(m_viewModel.GetValue(), 10.0);  // Should be clamped
}

TEST_F(GaugeViewModelTest, GetNormalizedValue) {
    m_viewModel.SetRange(0, 100);

    m_viewModel.SetValue(0.0);
    EXPECT_DOUBLE_EQ(m_viewModel.GetNormalizedValue(), 0.0);

    m_viewModel.SetValue(50.0);
    EXPECT_DOUBLE_EQ(m_viewModel.GetNormalizedValue(), 0.5);

    m_viewModel.SetValue(100.0);
    EXPECT_DOUBLE_EQ(m_viewModel.GetNormalizedValue(), 1.0);
}

TEST_F(GaugeViewModelTest, FormatValue) {
    m_viewModel.SetRange(0, 7000);
    m_viewModel.SetPrecision(0);
    m_viewModel.SetUnit("RPM");
    m_viewModel.SetValue(3500);

    std::string formatted = m_viewModel.FormatValue();
    EXPECT_EQ(formatted, "3500 RPM");
}

TEST_F(GaugeViewModelTest, FormatValueWithDecimals) {
    m_viewModel.SetRange(0, 100);
    m_viewModel.SetPrecision(2);
    m_viewModel.SetUnit("psi");
    m_viewModel.SetValue(50.567);

    std::string formatted = m_viewModel.FormatValue();
    EXPECT_EQ(formatted, "50.57 psi");
}

TEST_F(GaugeViewModelTest, TitleAndUnit) {
    m_viewModel.SetTitle("TACHOMETER");
    m_viewModel.SetUnit("RPM");

    EXPECT_EQ(m_viewModel.GetTitle(), "TACHOMETER");
    EXPECT_EQ(m_viewModel.GetUnit(), "RPM");
}
