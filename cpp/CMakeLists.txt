# Simplified CMakeLists.txt for testing the test framework
cmake_minimum_required(VERSION 3.20)
project(scratchpad_tests VERSION 1.0.0 LANGUAGES CXX)

# Set C++ standard
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Global compile options
set(CMAKE_CXX_FLAGS_DEBUG "-g -O0 -Wall -Wextra -Wpedantic")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG")

# Set default build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

# Enable testing
enable_testing()

# Find required packages
find_package(Threads REQUIRED)

# Use FetchContent for modern dependency management
include(FetchContent)

# GoogleTest for testing
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Make GoogleTest available
FetchContent_MakeAvailable(googletest)

# Include GoogleTest
include(GoogleTest)

# Simple test executable
add_executable(simple_test
    tests/unit/simple_test.cpp
)

target_link_libraries(simple_test PRIVATE
    gtest_main
    Threads::Threads
)

gtest_discover_tests(simple_test)