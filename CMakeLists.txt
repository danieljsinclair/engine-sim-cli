cmake_minimum_required(VERSION 3.20)
project(engine-sim-cli VERSION 1.0.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find required packages
find_package(OpenAL REQUIRED)
find_package(Threads REQUIRED)

# Use local engine-sim for development
# Comment this out and use FetchContent for release builds
set(ENGINE_SIM_LOCAL_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../engine-sim" CACHE PATH "Path to local engine-sim")

if(EXISTS "${ENGINE_SIM_LOCAL_PATH}/CMakeLists.txt")
    # Disable building GUI app
    set(BUILD_ENGINE_SIM_APP OFF CACHE BOOL "Disable GUI application" FORCE)
    add_subdirectory(${ENGINE_SIM_LOCAL_PATH} ${CMAKE_BINARY_DIR}/engine-sim EXCLUDE_FROM_ALL)
else()
    # Fetch engine-sim as a dependency (fallback)
    include(FetchContent)
    FetchContent_Declare(
        engine-sim
        GIT_REPOSITORY https://github.com/danieljsinclair/engine-sim.git
        GIT_TAG master
    )
    FetchContent_MakeAvailable(engine-sim)
endif()

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
