cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_SUPPRESS_DEVELOPER_WARNINGS ON CACHE BOOL "" FORCE)

option(ENABLE_COVERAGE "Enable code coverage instrumentation" OFF)
if(ENABLE_COVERAGE)
    add_compile_options(--coverage -g)
    add_link_options(--coverage)
endif()

# Find required packages
find_package(Threads REQUIRED)

# Keep FetchContent's download cache OUTSIDE build/ so `make scrub` (which
# removes build/) does NOT force a fresh network re-fetch of cli11 on the next
# configure. A fresh configure that must hit the network can stall/hang
# (DNS, captive portal, throttled github reachability, credential prompt),
# which presents as the `check-platform` step getting SIGTERM'd when a
# wrapper/IDE/user kills the hung configure — surfacing as
# `make: *** [build/.presets-ready.stamp] Error 2`. This mirrors the fix
# already applied to engine-sim-bridge for googletest. Pinning the cache to
# .fetchcache means the tarball is downloaded once and reused across scrubs,
# so configure is deterministic and offline-capable.
#
# The cache is partitioned PER BUILD DIRECTORY (build/, build-OS64/,
# build-SIMULATOR64/, build-cov/) because the macOS build uses Ninja while
# the iOS cross-builds use Unix Makefiles, and FetchContent's internal
# "subbuild" step records the generator in its CMakeCache.txt — sharing one
# cache across generators is a hard CMake error ("generator does not match").
# Each partition still lives under top-level .fetchcache/ (outside build/),
# so every build type survives its own scrub without a network re-fetch.
#
# NOTE: must be set BEFORE include(FetchContent). The FetchContent module
# itself sets FETCHCONTENT_BASE_DIR to ${CMAKE_BINARY_DIR}/_deps as a cache
# var on load; setting ours after include() (without FORCE) is a no-op.
# FORCE covers any stale value left in CMakeCache.txt from older runs.
get_filename_component(_cli_build_name "${CMAKE_BINARY_DIR}" NAME)
set(FETCHCONTENT_BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.fetchcache/${_cli_build_name}"
    CACHE PATH "FetchContent cache (per-build-dir, outside build/ to survive scrub)" FORCE)
# Never let a git operation inside FetchContent block on an interactive
# credential prompt — fail fast instead of hanging indefinitely.
set(ENV{GIT_TERMINAL_PROMPT} "0")

include(FetchContent)

FetchContent_Declare(
    cli11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.5.0
)
FetchContent_MakeAvailable(cli11)

# Platform-specific audio libraries. Defined early because the static bridge
# archive (linked below) requires these frameworks to be satisfied by every
# final link target that consumes it.
if(APPLE)
    set(AUDIO_LIBRARY "-framework AudioToolbox" "-framework CoreAudio" "-framework CoreFoundation")
else()
    message(FATAL_ERROR "engine-sim-cli only supports macOS with AudioQueue. Linux/Windows support has been removed.")
endif()

# engine-sim-bridge: consumed as a pre-built STATIC archive (macOS) or built
# from source via add_subdirectory (iOS cross-compilation). The bridge builds
# independently via its own Makefile BEFORE this cmake configure runs.
set(BRIDGE_USE_PREBUILT ON CACHE BOOL "Link against the pre-built static bridge archive (disable for iOS cross-build)")

if(BRIDGE_USE_PREBUILT)
    # macOS: bridge builds independently via its own Makefile BEFORE cmake configure.
    if(NOT DEFINED BRIDGE_BUILD_DIR)
        set(BRIDGE_BUILD_DIR "${CMAKE_SOURCE_DIR}/engine-sim-bridge/build")
    endif()

    set(BRIDGE_STATIC_LIB "${BRIDGE_BUILD_DIR}/libenginesim.a")

    if(NOT EXISTS "${BRIDGE_STATIC_LIB}")
        message(FATAL_ERROR
            "engine-sim-bridge static archive not found at ${BRIDGE_STATIC_LIB}\n"
            "The bridge must be built first. Run 'make build' from the CLI root."
        )
    endif()

    # A static archive does not carry its dependencies the way a dylib does.
    # The bridge privately links engine-sim (+ simple-2d-constraint-solver,
    # csv-io) and, when Piranha scripting is enabled, engine-sim-script-
    # interpreter (+ piranha). Each must be supplied at final link time, so we
    # expose them via INTERFACE_LINK_LIBRARIES. Existence-guarded so an optional
    # dependency (e.g. Piranha disabled) does not break the link.
    set(BRIDGE_LINK_DEPS "")
    foreach(_dep
            "engine-sim/libengine-sim.a"
            "engine-sim/libengine-sim-script-interpreter.a"
            "engine-sim/dependencies/submodules/piranha/libpiranha.a"
            "engine-sim/dependencies/submodules/simple-2d-constraint-solver/libsimple-2d-constraint-solver.a"
            "engine-sim/dependencies/submodules/csv-io/libcsv-io.a")
        if(EXISTS "${BRIDGE_BUILD_DIR}/${_dep}")
            list(APPEND BRIDGE_LINK_DEPS "${BRIDGE_BUILD_DIR}/${_dep}")
        endif()
    endforeach()

    # Assemble the bridge's transitive link closure as a single clean
    # semicolon-joined list (mixing list-valued vars with spaces into
    # INTERFACE_LINK_LIBRARIES corrupts the list and drops entries).
    set(BRIDGE_INTERFACE_LIBS ${BRIDGE_LINK_DEPS} ${AUDIO_LIBRARY} Threads::Threads)

    add_library(engine-sim-bridge STATIC IMPORTED)
    set_target_properties(engine-sim-bridge PROPERTIES
        IMPORTED_LOCATION "${BRIDGE_STATIC_LIB}"
        INTERFACE_INCLUDE_DIRECTORIES
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/include"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/dependencies/dr_libs"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/include"
            "${CMAKE_SOURCE_DIR}/engine-sim-bridge/engine-sim/scripting/include"
        INTERFACE_LINK_LIBRARIES "${BRIDGE_INTERFACE_LIBS}"
    )
else()
    # iOS: build bridge from source with the active toolchain.
    add_subdirectory(engine-sim-bridge ${CMAKE_BINARY_DIR}/engine-sim-bridge)
endif()

# CLI executable - thin CLI layer only
# Phase F: SimulationLoop, EngineConfig, IInputProvider, IPresentation moved to engine-sim-bridge
add_executable(engine-sim-cli
    # Config layer
    src/config/CLIconfig.cpp
    src/config/CLIMain.cpp
    src/config/KqueueSignalStopController.cpp
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

# Post-build: stage the bridge static archive and its dependency archives from
# the bridge's own build tree into the CLI build tree (build/lib/) for packaging
# and discovery. macOS only; the bridge must already be built.
if(BRIDGE_USE_PREBUILT)
    set(_stage_cmds COMMAND ${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/lib")
    foreach(_archive "${BRIDGE_STATIC_LIB}" ${BRIDGE_LINK_DEPS})
        list(APPEND _stage_cmds COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${_archive}" "${CMAKE_BINARY_DIR}/lib/")
    endforeach()
    add_custom_command(TARGET engine-sim-cli POST_BUILD
        ${_stage_cmds}
        COMMENT "Staging engine-sim-bridge static archives into ${CMAKE_BINARY_DIR}/lib"
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
