cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

# Find required packages
find_package(Threads REQUIRED)
include(FetchContent)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0
)
FetchContent_MakeAvailable(cli11)

# engine-sim-bridge: consumed as pre-built shared library (macOS) or built from
# source via add_subdirectory (iOS cross-compilation).
set(BRIDGE_USE_PREBUILT ON CACHE BOOL "Link against pre-built bridge dylib (disable for iOS cross-build)" )

if(BRIDGE_USE_PREBUILT)
    # macOS: bridge builds independently via its own Makefile BEFORE cmake configure.
    if(NOT DEFINED BRIDGE_BUILD_DIR)
        set(BRIDGE_BUILD_DIR "${CMAKE_SOURCE_DIR}/engine-sim-bridge/build")
    endif()

    set(BRIDGE_DYLIB "${BRIDGE_BUILD_DIR}/libenginesim.dylib")

    if(NOT EXISTS "${BRIDGE_DYLIB}")
        message(FATAL_ERROR
            "engine-sim-bridge dylib not found at ${BRIDGE_DYLIB}\n"
            "The bridge must be built first. Run 'make build' from the CLI root."
        )
    endif()

    add_library(engine-sim-bridge SHARED IMPORTED)
    set_target_properties(engine-sim-bridge PROPERTIES
        IMPORTED_LOCATION "${BRIDGE_DYLIB}"
        IMPORTED_SONAME "${BRIDGE_BUILD_DIR}/libenginesim.1.dylib"
        INTERFACE_INCLUDE_DIRECTORIES
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/include"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/dependencies/dr_libs"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/include"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/scripting/include"
    )
else()
    # iOS: build bridge from source with the active toolchain.
    add_subdirectory(engine-sim-bridge ${CMAKE_BINARY_DIR}/engine-sim-bridge)
endif()

# Platform-specific audio libraries
if(APPLE)
    set(AUDIO_LIBRARY "-framework AudioToolbox -framework CoreAudio -framework CoreFoundation")
else()
    message(FATAL_ERROR "engine-sim-cli only supports macOS with AudioQueue. Linux/Windows support has been removed.")
endif()

# CLI executable - thin CLI layer only
# Phase F: SimulationLoop, EngineConfig, IInputProvider, IPresentation moved to engine-sim-bridge
add_executable(engine-sim-cli
    # Config layer
    src/config/CLIconfig.cpp
    src/config/CLIMain.cpp
    src/config/ANSIColors.cpp
    # Presentation layer
    src/presentation/ConsolePresentation.cpp
    # Input layer
    src/input/KeyboardInput.cpp
    # Bridge sources for connect-demo mode
    engine-sim-bridge/src/input/DemoInputProvider.cpp
    engine-sim-bridge/src/input/DemoThrottleSource.cpp
    engine-sim-bridge/src/input/GearSelectorInput.cpp
    engine-sim-bridge/src/input/IgnitionInput.cpp
    engine-sim-bridge/src/twin/VirtualIceTwin.cpp
    engine-sim-bridge/src/twin/ThrottleSmoother.cpp
    engine-sim-bridge/src/twin/AutomaticGearbox.cpp
)

target_link_libraries(engine-sim-cli
    PRIVATE
    engine-sim-bridge
    ${AUDIO_LIBRARY}
    Threads::Threads
    CLI11::CLI11
)

# Define BUILDING_FROM_PARENT_REPO when building bridge sources for CLI
target_compile_definitions(engine-sim-cli PRIVATE BUILDING_FROM_PARENT_REPO)

target_include_directories(engine-sim-cli
    PRIVATE
    engine-sim-bridge/include
    engine-sim-bridge
    engine-sim-bridge/engine-sim/dependencies/dr_libs
    engine-sim-bridge/engine-sim/include
    engine-sim-bridge/engine-sim/scripting/include
    src
)

target_compile_options(engine-sim-cli PRIVATE -g)

# Set rpath for CLI executable to find dylibs in build directory
if(APPLE)
    set_target_properties(engine-sim-cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
    )
endif()

# Post-build: Copy engine-sim-bridge dylib from bridge's own build tree (macOS only)
if(BRIDGE_USE_PREBUILT)
    add_custom_command(TARGET engine-sim-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BRIDGE_BUILD_DIR}/libenginesim.1.dylib"
            "${CMAKE_BINARY_DIR}/libenginesim.1.dylib"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${BRIDGE_BUILD_DIR}/libenginesim.dylib"
            "${CMAKE_BINARY_DIR}/libenginesim.dylib"
        COMMENT "Copying engine-sim-bridge dylib from ${BRIDGE_BUILD_DIR}"
    )
endif()

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION bin
)

# -----------------------------------------------------------------------------
# Tests
# -----------------------------------------------------------------------------
option(BUILD_TESTS "Build engine-sim-cli unit tests" ON)
if(BUILD_TESTS)
    # Suppress deprecation warnings from googletest's old cmake_minimum_required
    set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG v1.17.0
    )

    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    enable_testing()

    add_subdirectory(test)

    add_custom_target(run_tests
        DEPENDS engine-sim-cli smoke_tests unit_tests integration_tests telemetry_isp_tests
        COMMAND ${CMAKE_COMMAND} -E echo "=== CLI Smoke Tests ==="
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/smoke_tests --gtest_color=yes
        COMMAND ${CMAKE_COMMAND} -E echo "=== CLI Unit Tests ==="
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/unit/unit_tests --gtest_color=yes
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running CLI tests (bridge tests run separately via Makefile)"
    )
endif()
