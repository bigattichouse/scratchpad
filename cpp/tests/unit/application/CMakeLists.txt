# Application layer unit tests

# VM Manager tests
add_executable(test_vm_manager
    test_vm_manager.cpp
)

target_link_libraries(test_vm_manager PRIVATE
    test_utils
    scratchpad_static
    gtest_main
)

gtest_discover_tests(test_vm_manager)

# Image Manager tests
add_executable(test_image_manager
    test_image_manager.cpp
)

target_link_libraries(test_image_manager PRIVATE
    test_utils
    scratchpad_static
    gtest_main
)

gtest_discover_tests(test_image_manager)

# Resource Manager tests
add_executable(test_resource_manager
    test_resource_manager.cpp
)

target_link_libraries(test_resource_manager PRIVATE
    test_utils
    scratchpad_static
    gtest_main
)

gtest_discover_tests(test_resource_manager)