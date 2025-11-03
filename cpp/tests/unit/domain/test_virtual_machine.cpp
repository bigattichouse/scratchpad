#include <gtest/gtest.h>
#include "domain/vm/virtual_machine.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>

using namespace scratchpad;
using namespace scratchpad::test;

class VirtualMachineTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_vm_id_ = VMId("test-vm");
        test_config_ = VMConfiguration(test_vm_id_, ImageType::Ubuntu2204);
        test_config_.set_memory(MemoryAmount::from_string("1G"));
        test_config_.set_disk_size(DiskSize::from_string("10G"));
        test_config_.set_cpu_cores(2);
    }
    
    VMId test_vm_id_;
    VMConfiguration test_config_;
};

// Construction tests
TEST_F(VirtualMachineTest, ConstructionWithValidConfiguration) {
    EXPECT_NO_THROW({
        VirtualMachine vm(test_config_);
        EXPECT_EQ(vm.id(), test_vm_id_);
        EXPECT_EQ(vm.configuration().vm_id(), test_vm_id_);
        EXPECT_EQ(vm.status(), VMStatus::Stopped);
    });
}

TEST_F(VirtualMachineTest, ConstructionWithInvalidConfiguration) {
    VMConfiguration invalid_config; // Empty VM ID
    EXPECT_THROW({
        VirtualMachine vm(invalid_config);
    }, ScratchpadError);
}

TEST_F(VirtualMachineTest, MoveConstruction) {
    VMConfiguration config_copy = test_config_;
    
    VirtualMachine vm(std::move(config_copy));
    EXPECT_EQ(vm.id(), test_vm_id_);
    EXPECT_EQ(vm.configuration().vm_id(), test_vm_id_);
}

// Identity and configuration tests
TEST_F(VirtualMachineTest, IdentityAccess) {
    VirtualMachine vm(test_config_);
    
    EXPECT_EQ(vm.id(), test_vm_id_);
    EXPECT_EQ(vm.id().value(), "test-vm");
}

TEST_F(VirtualMachineTest, ConfigurationAccess) {
    VirtualMachine vm(test_config_);
    
    const VMConfiguration& config = vm.configuration();
    EXPECT_EQ(config.vm_id(), test_vm_id_);
    EXPECT_EQ(config.base_image(), ImageType::Ubuntu2204);
    EXPECT_EQ(config.memory().to_string(), "1G");
    EXPECT_EQ(config.cpu_cores(), 2);
}

TEST_F(VirtualMachineTest, ConfigurationUpdate) {
    VirtualMachine vm(test_config_);
    
    // Can update when stopped
    VMConfiguration new_config = test_config_;
    new_config.set_memory(MemoryAmount::from_string("2G"));
    new_config.set_cpu_cores(4);
    
    EXPECT_NO_THROW(vm.update_configuration(new_config));
    EXPECT_EQ(vm.configuration().memory().to_string(), "2G");
    EXPECT_EQ(vm.configuration().cpu_cores(), 4);
    
    // Cannot update when running
    vm.set_status(VMStatus::Running);
    EXPECT_THROW(vm.update_configuration(new_config), ScratchpadError);
}

// Status management tests
TEST_F(VirtualMachineTest, InitialStatus) {
    VirtualMachine vm(test_config_);
    
    EXPECT_EQ(vm.status(), VMStatus::Stopped);
    EXPECT_GT(vm.creation_time(), std::chrono::system_clock::time_point{});
    EXPECT_GT(vm.last_status_change(), std::chrono::system_clock::time_point{});
}

TEST_F(VirtualMachineTest, ValidStatusTransitions) {
    VirtualMachine vm(test_config_);
    
    // Test valid state transitions
    auto before_change = std::chrono::system_clock::now();
    
    // Stopped -> Starting
    vm.set_status(VMStatus::Starting);
    EXPECT_EQ(vm.status(), VMStatus::Starting);
    EXPECT_GE(vm.last_status_change(), before_change);
    
    // Starting -> Running
    vm.set_status(VMStatus::Running);
    EXPECT_EQ(vm.status(), VMStatus::Running);
    
    // Running -> Stopping
    vm.set_status(VMStatus::Stopping);
    EXPECT_EQ(vm.status(), VMStatus::Stopping);
    
    // Stopping -> Stopped
    vm.set_status(VMStatus::Stopped);
    EXPECT_EQ(vm.status(), VMStatus::Stopped);
    
    // Test error states
    vm.set_status(VMStatus::Error);
    EXPECT_EQ(vm.status(), VMStatus::Error);
}

TEST_F(VirtualMachineTest, InvalidStatusTransitions) {
    VirtualMachine vm(test_config_);
    
    // Cannot go directly from Stopped to Running
    EXPECT_THROW(vm.set_status(VMStatus::Running), ScratchpadError);
    
    // Cannot go from Stopped to Stopping
    EXPECT_THROW(vm.set_status(VMStatus::Stopping), ScratchpadError);
    
    vm.set_status(VMStatus::Starting);
    // Cannot go from Starting to Stopping without Running
    EXPECT_THROW(vm.set_status(VMStatus::Stopping), ScratchpadError);
    
    vm.set_status(VMStatus::Running);
    // Cannot go directly from Running to Starting
    EXPECT_THROW(vm.set_status(VMStatus::Starting), ScratchpadError);
}

TEST_F(VirtualMachineTest, StatusHistory) {
    VirtualMachine vm(test_config_);
    
    // Perform several status changes
    vm.set_status(VMStatus::Starting);
    vm.set_status(VMStatus::Running);
    vm.set_status(VMStatus::Stopping);
    vm.set_status(VMStatus::Stopped);
    
    auto history = vm.status_history();
    EXPECT_GE(history.size(), 4); // At least 4 transitions
    
    // Check that history is ordered chronologically
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
    }
    
    // Check specific transitions
    auto it = std::find_if(history.begin(), history.end(), [](const auto& change) {
        return change.from_status == VMStatus::Stopped && change.to_status == VMStatus::Starting;
    });
    EXPECT_NE(it, history.end());
}

// Resource and state queries
TEST_F(VirtualMachineTest, StateQueries) {
    VirtualMachine vm(test_config_);
    
    // Initially stopped
    EXPECT_TRUE(vm.is_stopped());
    EXPECT_FALSE(vm.is_running());
    EXPECT_FALSE(vm.is_transitioning());
    EXPECT_FALSE(vm.has_error());
    
    // Starting state
    vm.set_status(VMStatus::Starting);
    EXPECT_FALSE(vm.is_stopped());
    EXPECT_FALSE(vm.is_running());
    EXPECT_TRUE(vm.is_transitioning());
    EXPECT_FALSE(vm.has_error());
    
    // Running state
    vm.set_status(VMStatus::Running);
    EXPECT_FALSE(vm.is_stopped());
    EXPECT_TRUE(vm.is_running());
    EXPECT_FALSE(vm.is_transitioning());
    EXPECT_FALSE(vm.has_error());
    
    // Error state
    vm.set_status(VMStatus::Error);
    EXPECT_FALSE(vm.is_stopped());
    EXPECT_FALSE(vm.is_running());
    EXPECT_FALSE(vm.is_transitioning());
    EXPECT_TRUE(vm.has_error());
}

TEST_F(VirtualMachineTest, UptimeCalculation) {
    VirtualMachine vm(test_config_);
    
    // No uptime when stopped
    EXPECT_EQ(vm.uptime(), std::chrono::seconds{0});
    
    // Start VM and check uptime
    auto start_time = std::chrono::system_clock::now();
    vm.set_status(VMStatus::Starting);
    vm.set_status(VMStatus::Running);
    
    // Small delay to ensure measurable uptime
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    
    auto uptime = vm.uptime();
    EXPECT_GT(uptime, std::chrono::seconds{0});
    EXPECT_LT(uptime, std::chrono::seconds{10}); // Should be very small
    
    // Stop VM, uptime should be total running time
    vm.set_status(VMStatus::Stopping);
    vm.set_status(VMStatus::Stopped);
    
    auto final_uptime = vm.uptime();
    EXPECT_GE(final_uptime, uptime); // Should be at least as much as before
}

// Resource tracking tests
TEST_F(VirtualMachineTest, ResourceUsageTracking) {
    VirtualMachine vm(test_config_);
    
    // No resource usage when stopped
    auto usage = vm.resource_usage();
    EXPECT_EQ(usage.memory_used.bytes, 0);
    EXPECT_EQ(usage.cpu_percent, 0.0);
    EXPECT_EQ(usage.disk_used.bytes, 0);
    
    // Set resource usage (simulating real usage)
    ResourceUsage new_usage;
    new_usage.memory_used = MemoryAmount::from_string("512M");
    new_usage.cpu_percent = 25.5;
    new_usage.disk_used = DiskSize::from_string("2G");
    new_usage.network_rx_bytes = 1024 * 1024; // 1MB
    new_usage.network_tx_bytes = 512 * 1024;  // 512KB
    
    vm.set_resource_usage(new_usage);
    
    auto retrieved_usage = vm.resource_usage();
    EXPECT_EQ(retrieved_usage.memory_used.bytes, new_usage.memory_used.bytes);
    EXPECT_DOUBLE_EQ(retrieved_usage.cpu_percent, new_usage.cpu_percent);
    EXPECT_EQ(retrieved_usage.disk_used.bytes, new_usage.disk_used.bytes);
    EXPECT_EQ(retrieved_usage.network_rx_bytes, new_usage.network_rx_bytes);
    EXPECT_EQ(retrieved_usage.network_tx_bytes, new_usage.network_tx_bytes);
}

TEST_F(VirtualMachineTest, ResourceUsageHistory) {
    VirtualMachine vm(test_config_);
    
    // Add several resource usage samples
    for (int i = 0; i < 5; ++i) {
        ResourceUsage usage;
        usage.memory_used = MemoryAmount::from_bytes(100 * 1024 * 1024 * (i + 1)); // 100MB * (i+1)
        usage.cpu_percent = 10.0 * (i + 1);
        vm.set_resource_usage(usage);
        
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    
    auto history = vm.resource_usage_history();
    EXPECT_EQ(history.size(), 5);
    
    // Check that samples are stored correctly
    EXPECT_EQ(history[0].usage.memory_used.bytes, 100 * 1024 * 1024);
    EXPECT_EQ(history[4].usage.memory_used.bytes, 500 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(history[0].usage.cpu_percent, 10.0);
    EXPECT_DOUBLE_EQ(history[4].usage.cpu_percent, 50.0);
    
    // Check timestamps are ordered
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
    }
}

// Process and SSH information tests
TEST_F(VirtualMachineTest, ProcessInformation) {
    VirtualMachine vm(test_config_);
    
    // Initially no process info
    EXPECT_FALSE(vm.qemu_process_id().has_value());
    EXPECT_FALSE(vm.ssh_port().has_value());
    
    // Set process information
    vm.set_qemu_process_id(12345);
    vm.set_ssh_port(2222);
    
    EXPECT_TRUE(vm.qemu_process_id().has_value());
    EXPECT_EQ(vm.qemu_process_id().value(), 12345);
    EXPECT_TRUE(vm.ssh_port().has_value());
    EXPECT_EQ(vm.ssh_port().value(), 2222);
    
    // Clear process information
    vm.clear_qemu_process_id();
    vm.clear_ssh_port();
    
    EXPECT_FALSE(vm.qemu_process_id().has_value());
    EXPECT_FALSE(vm.ssh_port().has_value());
}

// Error handling tests
TEST_F(VirtualMachineTest, ErrorHandling) {
    VirtualMachine vm(test_config_);
    
    // Initially no error
    EXPECT_FALSE(vm.has_error());
    EXPECT_TRUE(vm.last_error().empty());
    
    // Set error
    std::string error_msg = "Failed to start VM: QEMU process exited unexpectedly";
    vm.set_error(error_msg);
    
    EXPECT_TRUE(vm.has_error());
    EXPECT_EQ(vm.status(), VMStatus::Error);
    EXPECT_EQ(vm.last_error(), error_msg);
    
    // Clear error by setting valid status
    vm.set_status(VMStatus::Stopped);
    EXPECT_FALSE(vm.has_error());
    EXPECT_TRUE(vm.last_error().empty());
}

// Copy and move semantics tests
TEST_F(VirtualMachineTest, CopySemantics) {
    VirtualMachine original(test_config_);
    original.set_status(VMStatus::Starting);
    original.set_qemu_process_id(12345);
    
    VirtualMachine copy(original);
    
    EXPECT_EQ(copy.id(), original.id());
    EXPECT_EQ(copy.status(), original.status());
    EXPECT_EQ(copy.qemu_process_id(), original.qemu_process_id());
    
    // Changes to copy shouldn't affect original
    copy.set_status(VMStatus::Running);
    EXPECT_EQ(original.status(), VMStatus::Starting);
}

TEST_F(VirtualMachineTest, MoveSemantics) {
    VirtualMachine original(test_config_);
    original.set_status(VMStatus::Starting);
    VMId original_id = original.id();
    
    VirtualMachine moved(std::move(original));
    
    EXPECT_EQ(moved.id(), original_id);
    EXPECT_EQ(moved.status(), VMStatus::Starting);
}

// Persistence and serialization tests
TEST_F(VirtualMachineTest, PersistenceOperations) {
    VirtualMachine vm(test_config_);
    vm.set_status(VMStatus::Running);
    vm.set_qemu_process_id(12345);
    vm.set_ssh_port(2222);
    
    // Check if VM can be marked for persistence
    EXPECT_TRUE(vm.can_be_persisted());
    
    // Mark as persistent
    vm.mark_as_persistent();
    EXPECT_TRUE(vm.is_persistent());
    
    // Error state VMs cannot be persisted
    vm.set_status(VMStatus::Error);
    EXPECT_FALSE(vm.can_be_persisted());
}

// Validation tests
TEST_F(VirtualMachineTest, StateValidation) {
    VirtualMachine vm(test_config_);
    
    // VM should be in valid state initially
    EXPECT_TRUE(vm.is_in_valid_state());
    
    // Manually put VM in invalid state (this would normally not happen)
    // We can't easily test this without breaking encapsulation
    // but we can test the validation logic
    vm.set_status(VMStatus::Running);
    vm.set_qemu_process_id(12345);
    EXPECT_TRUE(vm.is_in_valid_state());
    
    // Running VM without process ID would be invalid
    vm.clear_qemu_process_id();
    EXPECT_FALSE(vm.is_in_valid_state());
}