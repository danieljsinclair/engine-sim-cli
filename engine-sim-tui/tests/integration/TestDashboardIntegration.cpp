#include <gtest/gtest.h>
#include "engine_sim_tui/widgets/DashboardLayout.h"
#include "engine_sim_tui/data/MockEngineDataProvider.h"
#include <memory>

using namespace engine_sim_tui;

class DashboardIntegrationTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_provider = std::make_shared<data::MockEngineDataProvider>();
        m_dashboard.SetDataProvider(m_provider);
    }

    widgets::DashboardLayout m_dashboard;
    std::shared_ptr<data::MockEngineDataProvider> m_provider;
};

TEST_F(DashboardIntegrationTest, DashboardRenders) {
    // Initial render should work
    auto element = m_dashboard.Render();
    ASSERT_NE(element, nullptr);
}

TEST_F(DashboardIntegrationTest, UpdateLoopDoesNotCrash) {
    // Run update loop for a "second"
    int iterations = 60; // ~60 FPS
    for (int i = 0; i < iterations; ++i) {
        m_dashboard.Update();
    }

    // Should reach here without crashing
    SUCCEED();
}

TEST_F(DashboardIntegrationTest, UpdateReturnsChangedFlag) {
    // First update after setting pattern should change
    m_provider->SetRpmPattern(data::MockEngineDataProvider::RevvingPattern(5000));
    m_provider->Update(0.016);

    bool changed = m_dashboard.Update();
    EXPECT_TRUE(changed);
}

TEST_F(DashboardIntegrationTest, MockDataProviderIntegration) {
    // Set up revving pattern
    m_provider->SetRpmPattern(data::MockEngineDataProvider::RevvingPattern(6000));

    // Simulate time passing
    for (int i = 0; i < 100; ++i) {
        m_provider->Update(0.016);
        m_dashboard.Update();
    }

    // Should have processed all updates without error
    SUCCEED();
}

TEST_F(DashboardIntegrationTest, DataProviderMetadataPropagation) {
    // Mock provider should return metadata
    EXPECT_EQ(m_provider->GetCylinderCount(), 4);
    EXPECT_EQ(m_provider->GetRedline(), 7000.0);
    EXPECT_EQ(m_provider->GetEngineName(), "Subaru EJ25 2.5L H4");
}

TEST_F(DashboardIntegrationTest, SimulateOneSecond) {
    // Simulate one second at 60 FPS
    for (int frame = 0; frame < 60; ++frame) {
        m_dashboard.Update();

        // Render after each update (should not crash)
        auto element = m_dashboard.Render();
        ASSERT_NE(element, nullptr);
    }
}
