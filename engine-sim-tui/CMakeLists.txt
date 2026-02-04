cmake_minimum_required(VERSION 3.20)
project(engine-sim-tui VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ============================================================================
# Options
# ============================================================================
option(ENGINE_SIM_TUI_BUILD_TESTS "Build unit tests" ON)
option(ENGINE_SIM_TUI_BUILD_DEMO "Build demo application" ON)

# ============================================================================
# Dependencies - FTXUI
# ============================================================================
include(FetchContent)

# FTXUI via FetchContent for easy integration
FetchContent_Declare(
    ftxui
    GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI.git
    GIT_TAG main
)
FetchContent_MakeAvailable(ftxui)

# GoogleTest (for testing)
if(ENGINE_SIM_TUI_BUILD_TESTS)
    find_package(GTest QUIET)
    if(NOT GTest_FOUND)
        include(FetchContent)
        FetchContent_Declare(
            googletest
            GIT_REPOSITORY https://github.com/google/googletest.git
            GIT_TAG release-1.12.1
        )
        # For Windows: Prevent overriding parent project's compiler/linker settings
        set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
        FetchContent_MakeAvailable(googletest)
    endif()
endif()

# ============================================================================
# Source Files
# ============================================================================

# Data Provider Layer
set(DATA_PROVIDER_SOURCES
    src/data/MockEngineDataProvider.cpp
)

set(DATA_PROVIDER_HEADERS
    include/engine_sim_tui/data/IEngineDataProvider.h
    include/engine_sim_tui/data/MockEngineDataProvider.h
)

# ViewModel Layer
set(VIEWMODEL_SOURCES
    src/viewmodels/EngineViewModel.cpp
    src/viewmodels/GaugeViewModel.cpp
)

set(VIEWMODEL_HEADERS
    include/engine_sim_tui/viewmodels/EngineViewModel.h
    include/engine_sim_tui/viewmodels/GaugeViewModel.h
)

# Widget Layer
set(WIDGET_SOURCES
    src/widgets/CircularGaugeWidget.cpp
    src/widgets/DashboardLayout.cpp
)

set(WIDGET_HEADERS
    include/engine_sim_tui/widgets/CircularGaugeWidget.h
    include/engine_sim_tui/widgets/DashboardLayout.h
)

# Canvas Extensions
set(CANVAS_SOURCES
    src/canvas/ColorPalette.cpp
)

set(CANVAS_HEADERS
    include/engine_sim_tui/canvas/ColorPalette.h
)

# ============================================================================
# Library Target
# ============================================================================
add_library(engine-sim-tui
    ${DATA_PROVIDER_SOURCES}
    ${VIEWMODEL_SOURCES}
    ${WIDGET_SOURCES}
    ${CANVAS_SOURCES}
)

target_include_directories(engine-sim-tui
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_link_libraries(engine-sim-tui
    PUBLIC
        ftxui::dom
        ftxui::screen
        ftxui::component
)

# ============================================================================
# Demo Application
# ============================================================================
if(ENGINE_SIM_TUI_BUILD_DEMO)
    add_executable(engine-sim-tui-demo
        src/engine_sim_tui_main.cpp
    )

    target_link_libraries(engine-sim-tui-demo
        PRIVATE
            engine-sim-tui
    )

    # Set output directory
    set_target_properties(engine-sim-tui-demo PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ============================================================================
# Tests
# ============================================================================
if(ENGINE_SIM_TUI_BUILD_TESTS)
    enable_testing()

    # Test executable
    add_executable(engine-sim-tui-tests
        tests/viewmodels/TestEngineViewModel.cpp
        tests/viewmodels/TestGaugeViewModel.cpp
        tests/widgets/TestCircularGaugeWidget.cpp
        tests/integration/TestDashboardIntegration.cpp
    )

    target_link_libraries(engine-sim-tui-tests
        PRIVATE
            engine-sim-tui
            GTest::gtest
            GTest::gtest_main
            GTest::gmock
    )

    # Register tests with CTest
    include(GoogleTest)
    gtest_discover_tests(engine-sim-tui-tests)

    # Set output directory
    set_target_properties(engine-sim-tui-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
    )
endif()

# ============================================================================
# Installation
# ============================================================================
install(TARGETS engine-sim-tui
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin
)

install(DIRECTORY include/
    DESTINATION include
)

# ============================================================================
# Summary
# ============================================================================
message(STATUS "")
message(STATUS "Engine Sim TUI Configuration:")
message(STATUS "  Version: ${PROJECT_VERSION}")
message(STATUS "  Build Tests: ${ENGINE_SIM_TUI_BUILD_TESTS}")
message(STATUS "  Build Demo: ${ENGINE_SIM_TUI_BUILD_DEMO}")
message(STATUS "")
