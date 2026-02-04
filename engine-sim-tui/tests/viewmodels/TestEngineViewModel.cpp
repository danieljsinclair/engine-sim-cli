#include <gtest/gtest.h>
#include "engine_sim_tui/viewmodels/EngineViewModel.h"
#include "engine_sim_tui/data/MockEngineDataProvider.h"
#include <memory>

using namespace engine_sim_tui;

class EngineViewModelTest : public ::testing::Test {
protected:
    void SetUp() override {
        m_provider = std::make_shared<data::MockEngineDataProvider>();
        m_viewModel.SetDataProvider(m_provider);
    }

    viewmodels::EngineViewModel m_viewModel;
    std::shared_ptr<data::MockEngineDataProvider> m_provider;
};

TEST_F(EngineViewModelTest, InitializesToZero) {
    EXPECT_EQ(m_viewModel.GetRPM(), 0.0);
    EXPECT_EQ(m_viewModel.GetThrottle(), 0.0);
    EXPECT_EQ(m_viewModel.GetSpeed(), 0.0);
}

TEST_F(EngineViewModelTest, UpdateLoadsDataFromProvider) {
    // Set up provider with specific pattern
    m_provider->SetRpmPattern([](double) { return 3500.0; });
    m_provider->Update(0.016);

    // Update view model
    m_viewModel.Update();

    // Check values
    EXPECT_GT(m_viewModel.GetRPM(), 3000.0);
    EXPECT_LT(m_viewModel.GetRPM(), 4000.0);
}

TEST_F(EngineViewModelTest, HasChangedFlagWorks) {
    // Initially no change
    m_viewModel.Update();
    EXPECT_FALSE(m_viewModel.HasChanged());

    // Force a change
    m_provider->SetRpmPattern([](double) { return 5000.0; });
    m_provider->Update(0.016);
    m_viewModel.Update();

    EXPECT_TRUE(m_viewModel.HasChanged());
}

TEST_F(EngineViewModelTest, GetCylinderDataReturnsCorrectSize) {
    m_provider->Update(0.016);
    m_viewModel.Update();

    EXPECT_EQ(m_viewModel.GetCylinderTemperatures().size(), 4);
    EXPECT_EQ(m_viewModel.GetCylinderPressures().size(), 4);
    EXPECT_EQ(m_viewModel.GetCylinderFiring().size(), 4);
}

TEST_F(EngineViewModelTest, MetadataLoadedFromProvider) {
    m_viewModel.Update();

    EXPECT_EQ(m_viewModel.GetCylinderCount(), 4);
    EXPECT_EQ(m_viewModel.GetRedline(), 7000.0);
    EXPECT_FALSE(m_viewModel.GetEngineName().empty());
}

TEST_F(EngineViewModelTest, UpdateCallbackIsCalled) {
    bool callbackCalled = false;
    m_viewModel.SetUpdateCallback([&]() {
        callbackCalled = true;
    });

    m_provider->SetRpmPattern([](double) { return 2000.0; });
    m_provider->Update(0.016);
    m_viewModel.Update();

    EXPECT_TRUE(callbackCalled);
}

TEST_F(EngineViewModelTest, ManifoldPressureUpdates) {
    m_provider->SetRpmPattern([](double) { return 3000.0; });
    m_provider->Update(0.016);
    m_viewModel.Update();

    // Should have manifold pressure value
    double pressure = m_viewModel.GetManifoldPressure();
    EXPECT_GT(pressure, -50.0);  // Reasonable vacuum
    EXPECT_LT(pressure, 50.0);   // Reasonable boost
}
