# This file replaces CMakeLists.txt with the refactored version

cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(Threads REQUIRED)

# Platform-specific audio libraries
if(APPLE)
    # macOS/iOS: Use CoreAudio AudioQueue (built-in framework)
    set(AUDIO_LIBRARY "-framework AudioToolbox -framework CoreAudio -framework CoreFoundation")
else()
    message(FATAL_ERROR "engine-sim-cli only supports macOS with AudioQueue. Linux/Windows support has been removed.")
endif()

# Add engine-sim-bridge submodule
# The bridge now handles engine-sim as its own nested submodule
add_subdirectory(engine-sim-bridge ${CMAKE_BINARY_DIR}/engine-sim-bridge )

# CLI executable - refactored into modular folder structure
# Organized by concern: config, simulation, presentation, input, logging, audio, bridge
add_executable(engine-sim-cli
    # Config layer
    src/config/CLIconfig.cpp
    src/config/CLIMain.cpp
    src/config/ANSIColors.cpp
    # Simulation layer
    src/simulation/SimulationLoop.cpp
    src/simulation/EngineConfig.cpp
    # Presentation layer
    src/presentation/ConsolePresentation.cpp
    # Input layer
    src/input/KeyboardInput.cpp
    src/input/KeyboardInputProvider.cpp
    # Audio layer (refactored: consolidated renderer implementations)
    src/audio/renderers/SyncPullRenderer.cpp
    src/audio/renderers/ThreadedRenderer.cpp
    src/audio/renderers/AudioRendererFactory.cpp
    src/audio/common/CircularBuffer.cpp
    src/audio/common/BridgeAudioSource.cpp
    # Audio strategies (new: IAudioStrategy interface implementations)
    src/audio/strategies/IAudioStrategyFactory.cpp
    src/audio/strategies/ThreadedStrategy.cpp
    src/audio/strategies/SyncPullStrategy.cpp
    # Legacy files (to be refactored)
    src/AudioPlayer.cpp
    src/AudioSource.cpp
    src/SyncPullAudio.cpp
)

target_link_libraries(engine-sim-cli
    PRIVATE
    engine-sim-bridge
    ${AUDIO_LIBRARY}
    Threads::Threads
)

target_include_directories(engine-sim-cli
    PRIVATE
    engine-sim-bridge/include
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

# Post-build: Copy engine-sim libraries from bridge build directory
# Bridge builds both engine-sim-bridge (self-contained dependency)
# Parent just copies what it needs - no intrinsic knowledge of bridge internals
add_custom_command(TARGET engine-sim-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/engine-sim-bridge/libenginesim.dylib
        ${CMAKE_BINARY_DIR}/libenginesim.dylib
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/engine-sim-bridge/libenginesim.1.dylib
        ${CMAKE_BINARY_DIR}/libenginesim.1.dylib
    # Copy engine-sim assets so bridge can find them when running from build dir
    COMMAND ${CMAKE_COMMAND} -E copy_directory
        ${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim
        ${CMAKE_BINARY_DIR}/engine-sim-bridge/engine-sim
    COMMENT "Copying engine-sim libraries from bridge build directory"
)

# Ensure engine-sim-bridge and dependencies are built before engine-sim-cli
add_dependencies(engine-sim-cli engine-sim-bridge)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)

# -----------------------------------------------------------------------------
# Tests
# Bridge owns its own tests (BridgeUnitTests); CLI owns its own tests (smoke_tests)
# `make test` from CLI runs both; `make run_tests` shows real-time output
# -----------------------------------------------------------------------------
option(BUILD_TESTS "Build engine-sim-cli unit tests" ON)
if(BUILD_TESTS)
    # Download GoogleTest if not found
    include(FetchContent)
    FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG release-1.12.1
    )

    # For Windows: Prevent overriding parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)

    # Enable testing
    enable_testing()

    # Add CLI smoke tests
    add_subdirectory(test)

    # Custom 'run_tests' target: runs all tests with real-time output
    # Bridge tests its own code; CLI tests its own code
    add_custom_target(run_tests
        DEPENDS engine-sim-cli smoke_tests bridge_unit_tests
        COMMAND ${CMAKE_COMMAND} -E echo "=== Bridge Unit Tests ==========================================="
        COMMAND ${CMAKE_BINARY_DIR}/engine-sim-bridge/bridge_unit_tests --gtest_color=yes
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "=== CLI Smoke Tests ============================================="
        COMMAND ${CMAKE_CURRENT_BINARY_DIR}/test/smoke_tests --gtest_color=yes
        COMMAND ${CMAKE_COMMAND} -E echo ""
        COMMAND ${CMAKE_COMMAND} -E echo "=== All tests complete ==========================================="
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
        COMMENT "Running all tests with real-time output"
    )
endif()
