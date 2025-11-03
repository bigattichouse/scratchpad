# Scratchpad CLI Applications

# Main CLI executable
add_executable(scratchpad-cli
    main.cpp
    scratchpad_cli.cpp
)

# Link with scratchpad library
target_link_libraries(scratchpad-cli
    PRIVATE 
        scratchpad
        scratchpad_deps
)

# Include directories
target_include_directories(scratchpad-cli
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../lib
        ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

# Compiler features
target_compile_features(scratchpad-cli PRIVATE cxx_std_20)

# Set output name and properties
set_target_properties(scratchpad-cli PROPERTIES
    OUTPUT_NAME scratchpad
    RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin
)

# Install the executable
install(TARGETS scratchpad-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Create symlinks for common commands (optional)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux" OR CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    install(CODE "
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink 
            scratchpad \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/vm)
        execute_process(COMMAND ${CMAKE_COMMAND} -E create_symlink 
            scratchpad \${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_BINDIR}/scratchpad-vm)
    ")
endif()

# Development helper: copy to build directory
add_custom_command(TARGET scratchpad-cli POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
    $<TARGET_FILE:scratchpad-cli>
    ${CMAKE_SOURCE_DIR}/scratchpad
    COMMENT "Copying scratchpad executable to project root"
)