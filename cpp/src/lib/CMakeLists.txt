# Scratchpad library

set(SCRATCHPAD_LIB_SOURCES
    # Core infrastructure
    config/configuration.cpp
    logging/logger.cpp
    error/error_handler.cpp
    
    # Domain layer - VM Domain
    domain/vm/virtual_machine.cpp
    domain/vm/vm_configuration.cpp
    domain/vm/vm_id.cpp
    domain/vm/vm_lifecycle_service.cpp
    domain/vm/vm_health_service.cpp
    
    # Domain layer - Process Management
    domain/process/qemu_process.cpp
    domain/process/process_registry.cpp
    domain/process/process_manager.cpp
    domain/process/process_monitor.cpp
    
    # Domain layer - Communication
    domain/communication/ssh_connection.cpp
    domain/communication/command_execution.cpp
    domain/communication/ssh_service.cpp
    domain/communication/command_dispatcher.cpp
    
    # Domain layer - Image Management
    domain/image/base_image.cpp
    domain/image/prepared_image.cpp
    domain/image/overlay_disk.cpp
    domain/image/image_downloader.cpp
    domain/image/image_provisioner.cpp
    domain/image/overlay_manager.cpp
    
    # Domain layer - Resource Management
    domain/resource/port_allocator.cpp
    domain/resource/resource_tracker.cpp
    
    # Infrastructure layer
    infrastructure/qemu/qemu_adapter.cpp
    infrastructure/ssh/ssh_client.cpp
    infrastructure/filesystem/file_operations.cpp
    infrastructure/network/network_utils.cpp
    infrastructure/process/process_utils.cpp
    infrastructure/cloudinit/cloud_init_generator.cpp
    
    # Application services
    vm_manager.cpp
    image_manager.cpp
    resource_manager.cpp
)

set(SCRATCHPAD_LIB_HEADERS
    # Public API headers
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/vm_manager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/image_manager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/resource_manager.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/types.hpp
    ${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/errors.hpp
    
    # Domain headers (private)
    domain/vm/virtual_machine.hpp
    domain/vm/vm_configuration.hpp
    domain/vm/vm_id.hpp
    domain/process/qemu_process.hpp
    domain/communication/ssh_connection.hpp
    domain/image/base_image.hpp
    domain/resource/port_allocator.hpp
    
    # Infrastructure headers (private)
    infrastructure/qemu/qemu_adapter.hpp
    infrastructure/ssh/ssh_client.hpp
    infrastructure/filesystem/file_operations.hpp
)

# Create the library
add_library(scratchpad SHARED ${SCRATCHPAD_LIB_SOURCES})
add_library(scratchpad_static STATIC ${SCRATCHPAD_LIB_SOURCES})

# Set library properties
set_target_properties(scratchpad PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    OUTPUT_NAME scratchpad
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/vm_manager.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/image_manager.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/resource_manager.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/types.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/errors.hpp"
)

set_target_properties(scratchpad_static PROPERTIES
    OUTPUT_NAME scratchpad
    PUBLIC_HEADER "${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/vm_manager.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/image_manager.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/resource_manager.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/types.hpp;${CMAKE_CURRENT_SOURCE_DIR}/../../include/scratchpad/errors.hpp"
)

# Link dependencies
target_link_libraries(scratchpad PRIVATE scratchpad_deps)
target_link_libraries(scratchpad_static PRIVATE scratchpad_deps)

# Include directories
target_include_directories(scratchpad
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_include_directories(scratchpad_static
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../../include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# Compiler features
target_compile_features(scratchpad PUBLIC cxx_std_20)
target_compile_features(scratchpad_static PUBLIC cxx_std_20)

# Install targets
install(TARGETS scratchpad scratchpad_static
    EXPORT scratchpadTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/scratchpad
)

# Export targets
install(EXPORT scratchpadTargets
    FILE scratchpadTargets.cmake
    NAMESPACE scratchpad::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/scratchpad
)