# Infrastructure layer unit tests

# QEMU Adapter tests
add_executable(test_qemu_adapter
    test_qemu_adapter.cpp
)

target_link_libraries(test_qemu_adapter PRIVATE
    test_utils
    scratchpad_static
    gtest_main
)

gtest_discover_tests(test_qemu_adapter)

# SSH Client tests
add_executable(test_ssh_client
    test_ssh_client.cpp
)

target_link_libraries(test_ssh_client PRIVATE
    test_utils
    scratchpad_static
    gtest_main
)

gtest_discover_tests(test_ssh_client)