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
    engine-sim-bridge
    sine-wave-generator
    ${AUDIO_LIBRARY}
    Threads::Threads
)

target_include_directories(engine-sim-cli
    PRIVATE
    engine-sim-bridge/include
)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
