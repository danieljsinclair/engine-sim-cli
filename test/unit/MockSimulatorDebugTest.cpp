// MockSimulatorDebugTest.cpp - Debug test to verify MockDataSimulatorContext creation

#include "../mocks/MockDataSimulator.h"

#include <gtest/gtest.h>
#include <memory>
#include <iostream>

TEST(MockSimulatorDebugTest, CreateContextOnly) {
    std::cout << "Test starting: Create MockDataSimulatorContext only..." << std::endl;

    auto context = std::make_unique<MockDataSimulatorContext>();
    std::cout << "MockDataSimulatorContext created successfully!" << std::endl;

    ASSERT_NE(context, nullptr);
    std::cout << "Test completed!" << std::endl;
}
