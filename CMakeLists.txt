cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenAL REQUIRED)
find_package(Threads REQUIRED)

# Use local engine-sim-bridge path (sibling directory)
# The bridge will pull in engine-sim as needed
set(ENGINE_SIM_BRIDGE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../engine-sim-bridge" CACHE PATH "Path to engine-sim-bridge")

if(NOT EXISTS "${ENGINE_SIM_BRIDGE_PATH}/CMakeLists.txt")
    message(FATAL_ERROR "engine-sim-bridge not found at ${ENGINE_SIM_BRIDGE_PATH}")
endif()

# Add engine-sim-bridge (it will pull in engine-sim as a subdirectory)
add_subdirectory(${ENGINE_SIM_BRIDGE_PATH} ${CMAKE_BINARY_DIR}/engine-sim-bridge EXCLUDE_FROM_ALL)

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
    ${ENGINE_SIM_BRIDGE_PATH}/include
)

# Installation
install(TARGETS engine-sim-cli
    RUNTIME DESTINATION bin
)
