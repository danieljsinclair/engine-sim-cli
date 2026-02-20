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

# CRITICAL: Ensure dependencies are built before CLI
add_dependencies(engine-sim-cli engine-sim-bridge engine-sim-mock)

# Set rpath for CLI executable to find dylibs in build directory
if(APPLE)
    set_target_properties(engine-sim-cli PROPERTIES
        BUILD_WITH_INSTALL_RPATH TRUE
        INSTALL_RPATH "@executable_path"
        BUILD_RPATH "@executable_path"
    )
endif()

# Post-build copy: Copy engine-sim-bridge library to root directory
# This is needed because dlopen() searches in the executable's directory
add_custom_command(TARGET engine-sim-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        ${CMAKE_BINARY_DIR}/engine-sim-bridge/libenginesim.1.0.0.dylib
        ${CMAKE_BINARY_DIR}/libenginesim.1.0.0.dylib
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libenginesim.1.0.0.dylib
        ${CMAKE_BINARY_DIR}/libenginesim.1.dylib
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libenginesim.1.dylib
        ${CMAKE_BINARY_DIR}/libenginesim.dylib
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libenginesim-mock.1.0.0.dylib
        ${CMAKE_BINARY_DIR}/libenginesim-mock.1.dylib
    COMMAND ${CMAKE_COMMAND} -E create_symlink
        libenginesim-mock.1.dylib
        ${CMAKE_BINARY_DIR}/libenginesim-mock.dylib
    COMMENT "Copying engine-sim libraries to executable directory"
)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
