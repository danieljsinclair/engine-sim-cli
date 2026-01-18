cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenAL REQUIRED)
find_package(Threads REQUIRED)

# Fetch engine-sim as a dependency
include(FetchContent)
FetchContent_Declare(
    engine-sim
    GIT_REPOSITORY https://github.com/danieljsinclair/engine-sim.git
    GIT_TAG master
)
FetchContent_MakeAvailable(engine-sim)

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
)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
