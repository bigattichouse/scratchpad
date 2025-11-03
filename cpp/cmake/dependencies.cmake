# External dependencies for Scratchpad project

# Use FetchContent for modern dependency management
include(FetchContent)

# Set policy for FetchContent
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# fmt - Fast string formatting
FetchContent_Declare(
    fmt
    GIT_REPOSITORY https://github.com/fmtlib/fmt.git
    GIT_TAG 10.2.1
)

# spdlog - Fast logging library
FetchContent_Declare(
    spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG v1.12.0
)

# CLI11 - Command line parsing
FetchContent_Declare(
    CLI11
    GIT_REPOSITORY https://github.com/CLIUtils/CLI11.git
    GIT_TAG v2.4.1
)

# nlohmann/json - JSON library
FetchContent_Declare(
    nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
)

# GoogleTest for testing
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG v1.14.0
)

# Make dependencies available
FetchContent_MakeAvailable(fmt spdlog CLI11 nlohmann_json)

# Only fetch GoogleTest if testing is enabled
if(BUILD_TESTING)
    FetchContent_MakeAvailable(googletest)
endif()

# System dependencies

# Check for libssh
pkg_check_modules(LIBSSH REQUIRED libssh>=0.9.0)
if(LIBSSH_FOUND)
    message(STATUS "Found libssh: ${LIBSSH_VERSION}")
    add_library(ssh INTERFACE)
    target_include_directories(ssh INTERFACE ${LIBSSH_INCLUDE_DIRS})
    target_link_libraries(ssh INTERFACE ${LIBSSH_LIBRARIES})
    target_compile_options(ssh INTERFACE ${LIBSSH_CFLAGS_OTHER})
else()
    message(FATAL_ERROR "libssh not found. Please install libssh development package.")
endif()

# Check for libcurl
find_package(CURL REQUIRED)
if(CURL_FOUND)
    message(STATUS "Found CURL: ${CURL_VERSION_STRING}")
else()
    message(FATAL_ERROR "libcurl not found. Please install libcurl development package.")
endif()

# Check for OpenSSL
find_package(OpenSSL REQUIRED)
if(OPENSSL_FOUND)
    message(STATUS "Found OpenSSL: ${OPENSSL_VERSION}")
else()
    message(FATAL_ERROR "OpenSSL not found. Please install OpenSSL development package.")
endif()

# Platform-specific dependencies
if(WIN32)
    # Windows-specific libraries
    set(PLATFORM_LIBS ws2_32 wsock32)
elseif(APPLE)
    # macOS-specific libraries
    find_library(CORE_FOUNDATION CoreFoundation)
    find_library(SYSTEM_CONFIGURATION SystemConfiguration)
    set(PLATFORM_LIBS ${CORE_FOUNDATION} ${SYSTEM_CONFIGURATION})
else()
    # Linux-specific libraries
    set(PLATFORM_LIBS)
endif()

# Create interface library for all common dependencies
add_library(scratchpad_deps INTERFACE)

target_link_libraries(scratchpad_deps INTERFACE
    fmt::fmt
    spdlog::spdlog
    CLI11::CLI11
    nlohmann_json::nlohmann_json
    ssh
    CURL::libcurl
    OpenSSL::SSL
    OpenSSL::Crypto
    Threads::Threads
    ${PLATFORM_LIBS}
)

target_compile_features(scratchpad_deps INTERFACE cxx_std_20)

# Compiler-specific options
if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(scratchpad_deps INTERFACE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "Clang")
    target_compile_options(scratchpad_deps INTERFACE
        -Wall -Wextra -Wpedantic -Wno-unused-parameter
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "MSVC")
    target_compile_options(scratchpad_deps INTERFACE
        /W4 /permissive-
    )
endif()