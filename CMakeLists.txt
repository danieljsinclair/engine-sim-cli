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
add_subdirectory(engine-sim-bridge ${CMAKE_BINARY_DIR}/engine-sim-bridge EXCLUDE_FROM_ALL)

# Mock engine-sim library (shared library for --sine mode)
add_library(engine-sim-mock SHARED
    engine-sim-bridge/src/mock_engine_sim.cpp
)

target_include_directories(engine-sim-mock
    PUBLIC
    engine-sim-bridge/include
)

set_target_properties(engine-sim-mock PROPERTIES
    OUTPUT_NAME "enginesim-mock"
    VERSION 1.0.0
    SOVERSION 1
)

if(APPLE)
    set_target_properties(engine-sim-mock PROPERTIES
        MACOSX_RPATH TRUE
        INSTALL_NAME_DIR "@rpath"
    )
endif()

# Sine wave generator library (header-only for simplicity)
add_library(sine-wave-generator
    src/sine_wave_generator.cpp
    src/sine_wave_generator.h
)

target_include_directories(sine-wave-generator
    PUBLIC
    ${CMAKE_SOURCE_DIR}/src
)

# CLI executable
add_executable(engine-sim-cli
    src/engine_sim_cli.cpp
)

target_link_libraries(engine-sim-cli
    PRIVATE
    sine-wave-generator
    ${AUDIO_LIBRARY}
    Threads::Threads
    ${CMAKE_DL_LIBS}
)

target_include_directories(engine-sim-cli
    PRIVATE
    engine-sim-bridge/include
)

# Post-build: Ensure dylib libraries are available in build directory
# The CLI uses dlopen() to load libenginesim.dylib and libenginesim-mock.dylib
# from the same directory as the executable
# Note: Both engine-sim-bridge and engine-sim-mock are built with
# default output to CMAKE_BINARY_DIR, so dylibs are already in the right place
if(APPLE)
    add_custom_command(TARGET engine-sim-cli POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E echo "Engine-sim CLI built successfully"
        COMMAND ${CMAKE_COMMAND} -E echo "Dylib libraries are available in: ${CMAKE_BINARY_DIR}"
        COMMENT "Build complete - dylibs ready for runtime loading"
    )
endif()

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
