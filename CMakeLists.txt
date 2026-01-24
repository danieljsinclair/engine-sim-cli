cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenAL REQUIRED)
find_package(Threads REQUIRED)

# Add engine-sim-bridge submodule
# The bridge now handles engine-sim as its own nested submodule
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
