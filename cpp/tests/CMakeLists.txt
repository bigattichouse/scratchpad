cmake_minimum_required(VERSION 3.20)

# GoogleTest is already fetched in main CMakeLists.txt
# Include GoogleTest
include(GoogleTest)

# Create test utilities library
add_library(test_utils STATIC
    utils/test_helpers.cpp
    utils/mock_qemu_adapter.cpp
    utils/mock_ssh_client.cpp
    utils/temp_directory.cpp
)

target_link_libraries(test_utils PUBLIC
    gtest
    gmock
    scratchpad_lib
)

target_include_directories(test_utils PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Unit tests
add_subdirectory(unit)

# Integration tests
add_subdirectory(integration)

# Enable CTest integration
enable_testing()