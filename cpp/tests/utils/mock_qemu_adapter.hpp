#pragma once

#include "scratchpad/infrastructure/qemu/qemu_adapter.hpp"
#include <gmock/gmock.h>

namespace scratchpad::test {

class MockQemuAdapter : public QemuAdapter {
public:
    MockQemuAdapter() : QemuAdapter() {}
    
    MOCK_METHOD(std::future<StartResult>, start_vm_async, 
                (const VMConfiguration& config), (override));
    
    MOCK_METHOD(std::future<bool>, stop_vm_async, 
                (const VMId& vm_id), (override));
    
    MOCK_METHOD(std::future<bool>, destroy_vm_async, 
                (const VMId& vm_id, bool force), (override));
    
    MOCK_METHOD(bool, is_vm_running, 
                (const VMId& vm_id), (const, override));
    
    MOCK_METHOD(VMStatus, get_vm_status, 
                (const VMId& vm_id), (const, override));
    
    MOCK_METHOD(bool, create_disk_image, 
                (const std::filesystem::path& path, DiskSize size), (override));
    
    MOCK_METHOD(bool, clone_disk_image, 
                (const std::filesystem::path& source, const std::filesystem::path& dest), (override));
    
    MOCK_METHOD(AccelerationType, detect_available_acceleration, 
                (), (const, override));
    
    MOCK_METHOD(bool, supports_nested_virtualization, 
                (), (const, override));
};

// Factory for creating mock adapters
class MockQemuAdapterFactory {
public:
    static std::unique_ptr<MockQemuAdapter> create_mock() {
        auto mock = std::make_unique<MockQemuAdapter>();
        
        // Set up default expectations for common calls
        ON_CALL(*mock, supports_nested_virtualization())
            .WillByDefault(testing::Return(true));
        
        ON_CALL(*mock, is_vm_running(testing::_))
            .WillByDefault(testing::Return(false));
        
        ON_CALL(*mock, detect_available_acceleration())
            .WillByDefault(testing::Return(AccelerationType::KVM));
        
        return mock;
    }
};

} // namespace scratchpad::test