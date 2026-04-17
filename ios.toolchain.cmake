# iOS CMake Toolchain File
# Based on https://github.com/leetal/ios-cmake (simplified for our use case)
#
# Usage:
#   cmake -DCMAKE_TOOLCHAIN_FILE=ios.toolchain.cmake -DPLATFORM=OS64 ..
#
# PLATFORM options:
#   OS64       - iOS device (arm64)
#   SIMULATOR64 - iOS Simulator (x86_64/arm64)

cmake_minimum_required(VERSION 3.20)

# Default to iOS device if not specified
if(NOT DEFINED PLATFORM)
    set(PLATFORM OS64)
endif()

# Parse platform
if(PLATFORM STREQUAL "OS64")
    set(SDK_NAME "iphoneos")
    set(ARCHS "arm64")
    set(APPLE_TARGET_TRIPLE "arm64-apple-ios")
elseif(PLATFORM STREQUAL "SIMULATOR64")
    set(SDK_NAME "iphonesimulator")
    set(ARCHS "x86_64;arm64")
    set(APPLE_TARGET_TRIPLE "x86_64-apple-ios-simulator")
else()
    message(FATAL_ERROR "Invalid PLATFORM: ${PLATFORM}. Use OS64 or SIMULATOR64")
endif()

# Set deployment target
if(NOT DEFINED DEPLOYMENT_TARGET)
    set(DEPLOYMENT_TARGET "16.0")
endif()

# Set system properties
set(CMAKE_SYSTEM_NAME iOS)
set(CMAKE_SYSTEM_VERSION ${DEPLOYMENT_TARGET})
set(CMAKE_OSX_SYSROOT ${SDK_NAME})
set(CMAKE_OSX_DEPLOYMENT_TARGET ${DEPLOYMENT_TARGET})

# Set architectures
set(CMAKE_OSX_ARCHITECTURES "${ARCHS}" CACHE STRING "Target architectures")

# Set the target
set(CMAKE_C_COMPILER_TARGET "${APPLE_TARGET_TRIPLE}")
set(CMAKE_CXX_COMPILER_TARGET "${APPLE_TARGET_TRIPLE}")

# Search paths: find programs from host, but libraries/includes from iOS SDK
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
# Allow finding host packages (Boost, Bison) needed by piranha configure step
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)

# iOS-specific settings
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY "-" CACHE STRING "Code signing" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED NO CACHE BOOL "" FORCE)
set(CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED NO CACHE BOOL "" FORCE)

# Mark that we're building for iOS
set(IOS TRUE CACHE BOOL "Building for iOS" FORCE)
set(BUILD_IOS TRUE CACHE BOOL "Building for iOS" FORCE)

message(STATUS "iOS Toolchain: PLATFORM=${PLATFORM}, ARCHS=${ARCHS}, SDK=${SDK_NAME}, DEPLOYMENT_TARGET=${DEPLOYMENT_TARGET}")
