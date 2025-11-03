# Scratchpad Examples

# Basic usage example
add_executable(basic_usage_example
    basic_usage.cpp
)

target_link_libraries(basic_usage_example
    PRIVATE
        scratchpad
        scratchpad_deps
)

target_include_directories(basic_usage_example
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_features(basic_usage_example PRIVATE cxx_std_20)

# Async operations example
add_executable(async_operations_example
    async_operations.cpp
)

target_link_libraries(async_operations_example
    PRIVATE
        scratchpad
        scratchpad_deps
)

target_include_directories(async_operations_example
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../src/lib
        ${CMAKE_CURRENT_SOURCE_DIR}/../include
)

target_compile_features(async_operations_example PRIVATE cxx_std_20)

# Set output directory
set_target_properties(basic_usage_example async_operations_example
    PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/examples
)

# Install examples (optional)
if(INSTALL_EXAMPLES)
    install(TARGETS basic_usage_example async_operations_example
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}/examples
    )
endif()

# Add custom target to build all examples
add_custom_target(examples
    DEPENDS basic_usage_example async_operations_example
    COMMENT "Building all examples"
)