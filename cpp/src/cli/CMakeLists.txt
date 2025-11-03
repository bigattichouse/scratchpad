# Scratchpad CLI applications

# Main scratchpad CLI
add_executable(scratchpad-cli
    main.cpp
    cli_application.cpp
    commands/run_command.cpp
    commands/shell_command.cpp
    commands/list_command.cpp
    commands/info_command.cpp
    utils/output_formatter.cpp
)

target_link_libraries(scratchpad-cli PRIVATE 
    scratchpad
    scratchpad_deps
)

target_include_directories(scratchpad-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

set_target_properties(scratchpad-cli PROPERTIES
    OUTPUT_NAME scratchpad
)

# Scratchpad Live CLI
add_executable(scratchpad-live-cli
    live_main.cpp
    live_application.cpp
    commands/spawn_command.cpp
    commands/connect_command.cpp
    commands/stop_command.cpp
    commands/logs_command.cpp
    commands/health_command.cpp
    commands/clean_command.cpp
    utils/output_formatter.cpp
)

target_link_libraries(scratchpad-live-cli PRIVATE 
    scratchpad
    scratchpad_deps
)

target_include_directories(scratchpad-live-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

set_target_properties(scratchpad-live-cli PROPERTIES
    OUTPUT_NAME scratchpad-live
)

# Scratchpad Prepare CLI
add_executable(scratchpad-prepare-cli
    prepare_main.cpp
    prepare_application.cpp
    commands/prepare_command.cpp
    utils/progress_reporter.cpp
    utils/output_formatter.cpp
)

target_link_libraries(scratchpad-prepare-cli PRIVATE 
    scratchpad
    scratchpad_deps
)

target_include_directories(scratchpad-prepare-cli PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include
)

set_target_properties(scratchpad-prepare-cli PROPERTIES
    OUTPUT_NAME scratchpad-prepare
)

# Install CLI executables
install(TARGETS scratchpad-cli scratchpad-live-cli scratchpad-prepare-cli
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)