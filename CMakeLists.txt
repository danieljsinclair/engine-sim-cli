cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenAL REQUIRED)
find_package(Threads REQUIRED)

# Add engine-sim submodule (without GUI app, without bridge)
set(BUILD_ENGINE_SIM_APP OFF CACHE BOOL "Build engine-sim GUI app" FORCE)
set(BUILD_BRIDGE OFF CACHE BOOL "Build engine-sim-bridge from engine-sim" FORCE)
add_subdirectory(engine-sim ${CMAKE_BINARY_DIR}/engine-sim EXCLUDE_FROM_ALL)

# Add engine-sim-bridge submodule (depends on engine-sim)
# Set ENGINE_SIM_PATH for the bridge to find engine-sim
set(ENGINE_SIM_PATH "${CMAKE_CURRENT_SOURCE_DIR}/engine-sim" CACHE PATH "Path to engine-sim" FORCE)
add_subdirectory(engine-sim-bridge ${CMAKE_BINARY_DIR}/engine-sim-bridge EXCLUDE_FROM_ALL)

# CLI executable
add_executable(engine-sim-cli
    src/engine_sim_cli.cpp
)

target_link_libraries(engine-sim-cli
    PRIVATE
    engine-sim-bridge
    ${OPENAL_LIBRARY}
    Threads::Threads
)

target_include_directories(engine-sim-cli
    PRIVATE
    ${OPENAL_INCLUDE_DIRS}
    engine-sim-bridge/include
)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
