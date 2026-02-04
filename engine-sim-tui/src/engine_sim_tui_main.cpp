/**
 * @file engine_sim_tui_main.cpp
 * @brief TUI Dashboard Demo Application
 *
 * Entry point for the engine simulator TUI dashboard.
 * Uses mock data to demonstrate the UI without requiring the full simulator.
 */

#include "engine_sim_tui/data/MockEngineDataProvider.h"
#include "engine_sim_tui/widgets/DashboardLayout.h"
#include "engine_sim_tui/canvas/ColorPalette.h"

#include <ftxui/dom/elements.hpp>
#include <ftxui/screen/screen.hpp>
#include <ftxui/component/component.hpp>
#include <ftxui/component/screen_interactive.hpp>
#include <thread>
#include <chrono>
#include <iostream>

using namespace engine_sim_tui;
using namespace ftxui;

int main(int argc, char* argv[]) {
    (void)argc;
    (void)argv;

    std::cout << "Starting Engine Sim CLI TUI Dashboard..." << std::endl;
    std::cout << "Press 'q' or Ctrl+C to exit" << std::endl;
    std::cout << "Mock data: Engine will rev from 800 to 6000 RPM" << std::endl;
    std::cout << std::endl;

    // Create data provider
    auto dataProvider = std::make_shared<data::MockEngineDataProvider>();
    dataProvider->SetRpmPattern(
        data::MockEngineDataProvider::RevvingPattern(6000)
    );

    // Create dashboard
    auto dashboard = std::make_shared<widgets::DashboardLayout>();
    dashboard->SetDataProvider(dataProvider);

    std::atomic<bool> refresh_ui{true};
    std::atomic<bool> running{true};

    // Background update thread
    std::thread updateThread([&]() {
        while (running) {
            dashboard->Update();
            refresh_ui = true;
            std::this_thread::sleep_for(std::chrono::milliseconds(16)); // ~60 FPS
        }
    });

    // Create the UI component
    auto component = Renderer([&]() {
        return dashboard->Render();
    });

    // Add keyboard handler
    component = CatchEvent(component, [&](Event event) {
        if (event == Event::Character('q') || event == Event::Character('Q') ||
            event == Event::Escape) {
            running = false;
            return true;
        }
        return false;
    });

    // Create screen
    auto screen = ScreenInteractive::Fullscreen();

    // Add loop to refresh the screen periodically
    std::thread refreshThread([&]() {
        while (running) {
            using namespace std::chrono_literals;
            std::this_thread::sleep_for(0.03s);
            screen.PostEvent(Event::Custom);
        }
    });

    // Run the main loop
    screen.Loop(component);

    // Clean up
    running = false;
    updateThread.join();
    refreshThread.join();

    std::cout << "Dashboard exited." << std::endl;
    return 0;
}
