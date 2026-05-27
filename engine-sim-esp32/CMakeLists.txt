# engine-sim-esp32 — ESP-IDF build interface for engine-sim-cli on ESP32-S3
cmake_minimum_required(VERSION 3.16)

set(BRIDGE_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../engine-sim-bridge")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(engine-sim-esp32)
