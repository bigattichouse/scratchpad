# Domain unit tests

# VM Domain tests
add_executable(test_vm_domain
    test_vm_id.cpp
    test_vm_configuration.cpp
    test_virtual_machine.cpp
)

target_link_libraries(test_vm_domain PRIVATE
    test_utils
    scratchpad_lib
    gtest_main
)

gtest_discover_tests(test_vm_domain)

# Process Domain tests
add_executable(test_process_domain
    test_qemu_process.cpp
)

target_link_libraries(test_process_domain PRIVATE
    test_utils
    scratchpad_lib
    gtest_main
)

gtest_discover_tests(test_process_domain)

# Communication Domain tests
add_executable(test_communication_domain
    test_ssh_connection.cpp
    test_command_execution.cpp
)

target_link_libraries(test_communication_domain PRIVATE
    test_utils
    scratchpad_lib
    gtest_main
)

gtest_discover_tests(test_communication_domain)