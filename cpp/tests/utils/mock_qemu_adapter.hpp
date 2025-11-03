#pragma once

#include "infrastructure/qemu/qemu_adapter.hpp"
#include <gmock/gmock.h>

namespace scratchpad::test {

class MockQemuAdapter : public QemuAdapter {
public:
    MOCK_METHOD(std::future<bool>, start_vm_async, 
                (const VMConfiguration& config), (override));
    
    MOCK_METHOD(std::future<bool>, stop_vm_async, 
                (const VMId& vm_id), (override));
    
    MOCK_METHOD(bool, is_vm_running, 
                (const VMId& vm_id), (const, override));
    
    MOCK_METHOD(std::vector<VMId>, list_running_vms, 
                (), (const, override));
    
    MOCK_METHOD(bool, create_vm_disk, 
                (const std::filesystem::path& disk_path, const std::string& size), (override));
    
    MOCK_METHOD(bool, delete_vm_disk, 
                (const std::filesystem::path& disk_path), (override));
    
    MOCK_METHOD(std::optional<int>, get_vm_ssh_port, 
                (const VMId& vm_id), (const, override));
    
    MOCK_METHOD(ResourceUsage, get_vm_resource_usage, 
                (const VMId& vm_id), (const, override));
    
    MOCK_METHOD(bool, validate_qemu_installation, 
                (), (const, override));
    
    MOCK_METHOD(std::string, get_qemu_version, 
                (), (const, override));
};

// Factory for creating mock adapters
class MockQemuAdapterFactory {
public:
    static std::unique_ptr<MockQemuAdapter> create_mock() {
        auto mock = std::make_unique<MockQemuAdapter>();
        
        // Set up default expectations for common calls
        ON_CALL(*mock, validate_qemu_installation())
            .WillByDefault(testing::Return(true));
        
        ON_CALL(*mock, get_qemu_version())
            .WillByDefault(testing::Return("QEMU emulator version 7.0.0 (mock)"));
        
        ON_CALL(*mock, is_vm_running(testing::_))
            .WillByDefault(testing::Return(false));
        
        ON_CALL(*mock, list_running_vms())
            .WillByDefault(testing::Return(std::vector<VMId>{}));
        
        return mock;
    }
};

} // namespace scratchpad::test