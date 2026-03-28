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

# CLI executable - refactored into separate source files for SOLID compliance
# NEW: Includes audio/modes and audio/renderers for one-class-per-file structure
add_executable(engine-sim-cli
    src/CLIConfig.cpp
    src/AudioPlayer.cpp
    src/KeyboardInput.cpp
    src/RPMController.cpp
    src/AudioSource.cpp
    src/ConsoleColors.cpp
    src/audio/modes/ThreadedAudioMode.cpp
    src/audio/modes/SyncPullAudioMode.cpp
    src/audio/modes/AudioModeFactory.cpp
    src/audio/renderers/SyncPullRenderer.cpp      # NEW: Extracted SyncPullRenderer
    src/audio/renderers/CircularBufferRenderer.cpp # NEW: Extracted CircularBufferRenderer
    src/audio/renderers/SilentRenderer.cpp        # NEW: Extracted SilentRenderer
    src/SimulationLoop.cpp
    src/CLIMain.cpp
    src/CircularBuffer.cpp      # NEW: Pure ring buffer handling
    src/SyncPullAudio.cpp       # NEW: Sync-pull audio rendering
    src/SineGenerator.cpp       # NEW: One place for sine generation
    src/EngineConfig.cpp          # NEW: Engine config and script loading
    src/ILogging.cpp             # NEW: Logging interface implementation
    src/interfaces/KeyboardInputProvider.cpp    # NEW: Input provider interface
    src/interfaces/ConsolePresentation.cpp     # NEW: Presentation interface
)

target_link_libraries(engine-sim-cli
    PRIVATE
    ${AUDIO_LIBRARY}
    Threads::Threads
    ${CMAKE_DL_LIBS}
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
# Bridge builds both engine-sim-bridge and engine-sim-mock (self-contained dependency)
# Parent just copies what it needs - no intrinsic knowledge of bridge internals
add_custom_command(TARGET engine-sim-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory ${CMAKE_BINARY_DIR}
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/engine-sim-bridge/libenginesim.dylib
        ${CMAKE_BINARY_DIR}/libenginesim.dylib
    COMMAND ${CMAKE_COMMAND} -E copy
        ${CMAKE_BINARY_DIR}/engine-sim-bridge/libenginesim-mock.dylib
        ${CMAKE_BINARY_DIR}/libenginesim-mock.dylib
    COMMENT "Copying engine-sim libraries from bridge build directory"
)

# Ensure engine-sim-bridge and engine-sim-mock are built before engine-sim-cli
add_dependencies(engine-sim-cli engine-sim-bridge engine-sim-mock)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
