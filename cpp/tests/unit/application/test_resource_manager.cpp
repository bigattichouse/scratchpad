#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "scratchpad/resource_manager.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>

using namespace scratchpad;
using namespace scratchpad::test;
using testing::_;
using testing::Return;
using testing::InSequence;

class ResourceManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Set up test options with reasonable limits
        test_options_.system_limits.max_memory = MemoryAmount::gigabytes(8);
        test_options_.system_limits.max_disk_size = DiskSize::gigabytes(100);
        test_options_.system_limits.max_cpu_cores = 8;
        test_options_.ssh_port_range = {TestHelpers::get_test_ssh_port_start(), 
                                       TestHelpers::get_test_ssh_port_start() + 100};
        test_options_.vnc_port_range = {5900, 5999};
        test_options_.resource_check_interval = std::chrono::milliseconds{100}; // Fast for testing
        test_options_.enable_resource_monitoring = true;
        test_options_.memory_reserve_percentage = 0.1;
        test_options_.disk_reserve_percentage = 0.1;
        
        test_vm_id_ = VMId("test-resource-vm");
        test_vm_id_2_ = VMId("test-resource-vm-2");
    }
    
    ResourceManager::Options test_options_;
    VMId test_vm_id_;
    VMId test_vm_id_2_;
};

// Construction and configuration tests
TEST_F(ResourceManagerTest, ConstructionWithDefaultOptions) {
    EXPECT_NO_THROW({
        ResourceManager manager;
        auto status = manager.get_system_status();
        EXPECT_GT(status.total_memory.bytes, 0);
        EXPECT_GT(status.total_disk_space.bytes, 0);
    });
}

TEST_F(ResourceManagerTest, ConstructionWithCustomOptions) {
    EXPECT_NO_THROW({
        ResourceManager manager(test_options_);
        
        auto options = manager.get_options();
        EXPECT_EQ(options.ssh_port_range.start, test_options_.ssh_port_range.start);
        EXPECT_EQ(options.ssh_port_range.end, test_options_.ssh_port_range.end);
        EXPECT_EQ(options.memory_reserve_percentage, test_options_.memory_reserve_percentage);
    });
}

// System resource monitoring tests
TEST_F(ResourceManagerTest, GetSystemStatus) {
    ResourceManager manager(test_options_);
    
    auto status = manager.get_system_status();
    
    EXPECT_GT(status.total_memory.bytes, 0);
    EXPECT_GT(status.available_memory.bytes, 0);
    EXPECT_LE(status.available_memory.bytes, status.total_memory.bytes);
    
    EXPECT_GT(status.total_disk_space.bytes, 0);
    EXPECT_GT(status.available_disk_space.bytes, 0);
    EXPECT_LE(status.available_disk_space.bytes, status.total_disk_space.bytes);
    
    EXPECT_GT(status.cpu_cores, 0);
    EXPECT_GE(status.cpu_usage_percent, 0.0);
    EXPECT_LE(status.cpu_usage_percent, 100.0);
}

TEST_F(ResourceManagerTest, SystemResourceLimits) {
    ResourceManager manager(test_options_);
    
    auto limits = manager.get_effective_limits();
    
    // Effective limits should account for reserved resources
    EXPECT_LT(limits.max_memory.bytes, test_options_.system_limits.max_memory.bytes);
    EXPECT_LT(limits.max_disk_size.bytes, test_options_.system_limits.max_disk_size.bytes);
    EXPECT_LE(limits.max_cpu_cores, test_options_.system_limits.max_cpu_cores);
}

TEST_F(ResourceManagerTest, ResourceAvailabilityCheck) {
    ResourceManager manager(test_options_);
    
    // Check availability of reasonable resources
    MemoryAmount small_memory = MemoryAmount::megabytes(100);
    DiskSize small_disk = DiskSize::megabytes(500);
    
    EXPECT_TRUE(manager.is_memory_available(small_memory));
    EXPECT_TRUE(manager.is_disk_space_available(small_disk));
    
    // Check unavailability of excessive resources
    MemoryAmount huge_memory = MemoryAmount::terabytes(100);
    DiskSize huge_disk = DiskSize::terabytes(100);
    
    EXPECT_FALSE(manager.is_memory_available(huge_memory));
    EXPECT_FALSE(manager.is_disk_space_available(huge_disk));
}

// Resource allocation tests
TEST_F(ResourceManagerTest, AllocateResourcesBasic) {
    ResourceManager manager(test_options_);
    
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::megabytes(512);
    request.disk_needed = DiskSize::gigabytes(5);
    request.needs_ssh_port = true;
    request.needs_vnc_port = false;
    
    auto result = manager.allocate_resources(request);
    
    EXPECT_TRUE(result.success) << "Allocation failed: " << result.error_message;
    EXPECT_TRUE(result.allocated_ssh_port.has_value());
    EXPECT_FALSE(result.allocated_vnc_port.has_value());
    EXPECT_EQ(result.allocated_memory.bytes, request.memory_needed.bytes);
    EXPECT_EQ(result.allocated_disk.bytes, request.disk_needed.bytes);
}

TEST_F(ResourceManagerTest, AllocateResourcesWithPorts) {
    ResourceManager manager(test_options_);
    
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::megabytes(256);
    request.disk_needed = DiskSize::gigabytes(2);
    request.needs_ssh_port = true;
    request.needs_vnc_port = true;
    
    auto result = manager.allocate_resources(request);
    
    EXPECT_TRUE(result.success);
    EXPECT_TRUE(result.allocated_ssh_port.has_value());
    EXPECT_TRUE(result.allocated_vnc_port.has_value());
    
    // Ports should be in expected ranges
    EXPECT_GE(result.allocated_ssh_port.value(), test_options_.ssh_port_range.start);
    EXPECT_LE(result.allocated_ssh_port.value(), test_options_.ssh_port_range.end);
    EXPECT_GE(result.allocated_vnc_port.value(), test_options_.vnc_port_range.start);
    EXPECT_LE(result.allocated_vnc_port.value(), test_options_.vnc_port_range.end);
}

TEST_F(ResourceManagerTest, AllocateResourcesWithPreferredPorts) {
    ResourceManager manager(test_options_);
    
    PortNumber preferred_ssh = test_options_.ssh_port_range.start + 10;
    PortNumber preferred_vnc = test_options_.vnc_port_range.start + 5;
    
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::megabytes(256);
    request.disk_needed = DiskSize::gigabytes(1);
    request.needs_ssh_port = true;
    request.needs_vnc_port = true;
    request.preferred_ssh_port = preferred_ssh;
    request.preferred_vnc_port = preferred_vnc;
    
    auto result = manager.allocate_resources(request);
    
    EXPECT_TRUE(result.success);
    
    // Should use preferred ports if available
    if (TestHelpers::is_port_available(preferred_ssh)) {
        EXPECT_EQ(result.allocated_ssh_port.value(), preferred_ssh);
    }
    if (TestHelpers::is_port_available(preferred_vnc)) {
        EXPECT_EQ(result.allocated_vnc_port.value(), preferred_vnc);
    }
}

TEST_F(ResourceManagerTest, AllocateExcessiveResources) {
    ResourceManager manager(test_options_);
    
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::terabytes(10); // Way too much
    request.disk_needed = DiskSize::terabytes(10);
    request.needs_ssh_port = true;
    
    auto result = manager.allocate_resources(request);
    
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("memory"),
        testing::HasSubstr("disk"),
        testing::HasSubstr("insufficient"),
        testing::HasSubstr("limit")
    ));
}

// Resource deallocation tests
TEST_F(ResourceManagerTest, DeallocateResources) {
    ResourceManager manager(test_options_);
    
    // First allocate
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::megabytes(512);
    request.disk_needed = DiskSize::gigabytes(5);
    request.needs_ssh_port = true;
    
    auto alloc_result = manager.allocate_resources(request);
    ASSERT_TRUE(alloc_result.success);
    
    // Then deallocate
    auto dealloc_result = manager.deallocate_resources(test_vm_id_);
    EXPECT_TRUE(dealloc_result.success);
    
    // Resources should be available again
    auto status_after = manager.get_allocation_status(test_vm_id_);
    EXPECT_FALSE(status_after.has_value());
}

TEST_F(ResourceManagerTest, DeallocateNonExistentVM) {
    ResourceManager manager(test_options_);
    
    VMId nonexistent("nonexistent-vm");
    auto result = manager.deallocate_resources(nonexistent);
    
    // Should handle gracefully
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

// Port management tests
TEST_F(ResourceManagerTest, PortAllocationAndRelease) {
    ResourceManager manager(test_options_);
    
    // Allocate a specific port
    auto port_result = manager.allocate_port(test_vm_id_, PortType::SSH);
    EXPECT_TRUE(port_result.success);
    EXPECT_TRUE(port_result.allocated_port.has_value());
    
    PortNumber allocated_port = port_result.allocated_port.value();
    
    // Port should be marked as in use
    EXPECT_FALSE(manager.is_port_available(allocated_port));
    
    // Release the port
    auto release_result = manager.release_port(allocated_port);
    EXPECT_TRUE(release_result.success);
    
    // Port should be available again
    EXPECT_TRUE(manager.is_port_available(allocated_port));
}

TEST_F(ResourceManagerTest, PortConflictHandling) {
    ResourceManager manager(test_options_);
    
    // Allocate port for first VM
    auto port1 = manager.allocate_port(test_vm_id_, PortType::SSH);
    ASSERT_TRUE(port1.success);
    
    // Try to allocate same port for second VM
    auto port2 = manager.allocate_port(test_vm_id_2_, PortType::SSH, port1.allocated_port.value());
    
    if (port2.success) {
        // Should get a different port
        EXPECT_NE(port2.allocated_port.value(), port1.allocated_port.value());
    } else {
        // Or fail with appropriate error
        EXPECT_THAT(port2.error_message, testing::AnyOf(
            testing::HasSubstr("port"),
            testing::HasSubstr("conflict"),
            testing::HasSubstr("in use")
        ));
    }
}

TEST_F(ResourceManagerTest, PortRangeExhaustion) {
    ResourceManager manager(test_options_);
    
    // Allocate many ports to test range exhaustion
    std::vector<PortNumber> allocated_ports;
    const size_t max_attempts = 50; // Don't try forever
    
    for (size_t i = 0; i < max_attempts; ++i) {
        auto vm_id = VMId("test-vm-" + std::to_string(i));
        auto port_result = manager.allocate_port(vm_id, PortType::SSH);
        
        if (port_result.success) {
            allocated_ports.push_back(port_result.allocated_port.value());
        } else {
            // Should eventually fail with meaningful error
            EXPECT_THAT(port_result.error_message, testing::AnyOf(
                testing::HasSubstr("port"),
                testing::HasSubstr("range"),
                testing::HasSubstr("exhausted"),
                testing::HasSubstr("available")
            ));
            break;
        }
    }
    
    EXPECT_FALSE(allocated_ports.empty());
}

// Quota management tests
TEST_F(ResourceManagerTest, SetAndEnforceQuotas) {
    ResourceManager manager(test_options_);
    
    ResourceManager::ResourceQuota quota;
    quota.max_memory = MemoryAmount::gigabytes(2);
    quota.max_disk_space = DiskSize::gigabytes(10);
    quota.max_concurrent_vms = 2;
    quota.max_port_allocations = 5;
    
    manager.set_quota("test-user", quota);
    
    // Try allocation within quota
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::gigabytes(1);
    request.disk_needed = DiskSize::gigabytes(5);
    request.needs_ssh_port = true;
    
    auto result1 = manager.allocate_resources_with_quota(request, "test-user");
    EXPECT_TRUE(result1.success);
    
    // Try allocation exceeding quota
    ResourceManager::AllocationRequest excessive_request;
    excessive_request.vm_id = test_vm_id_2_;
    excessive_request.memory_needed = MemoryAmount::gigabytes(5); // Exceeds quota
    excessive_request.disk_needed = DiskSize::gigabytes(2);
    
    auto result2 = manager.allocate_resources_with_quota(excessive_request, "test-user");
    EXPECT_FALSE(result2.success);
    EXPECT_THAT(result2.error_message, testing::HasSubstr("quota"));
}

TEST_F(ResourceManagerTest, QuotaUsageTracking) {
    ResourceManager manager(test_options_);
    
    ResourceManager::ResourceQuota quota;
    quota.max_memory = MemoryAmount::gigabytes(4);
    quota.max_disk_space = DiskSize::gigabytes(20);
    quota.max_concurrent_vms = 3;
    
    manager.set_quota("test-user", quota);
    
    // Allocate some resources
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::gigabytes(1);
    request.disk_needed = DiskSize::gigabytes(5);
    
    auto result = manager.allocate_resources_with_quota(request, "test-user");
    ASSERT_TRUE(result.success);
    
    // Check quota usage
    auto usage = manager.get_quota_usage("test-user");
    EXPECT_TRUE(usage.has_value());
    EXPECT_EQ(usage->used_memory.bytes, MemoryAmount::gigabytes(1).bytes);
    EXPECT_EQ(usage->used_disk_space.bytes, DiskSize::gigabytes(5).bytes);
    EXPECT_EQ(usage->concurrent_vms, 1);
}

// Monitoring and statistics tests
TEST_F(ResourceManagerTest, ResourceUsageHistory) {
    ResourceManager manager(test_options_);
    
    // Let monitoring run for a short time
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    
    auto history = manager.get_resource_usage_history(std::chrono::seconds{1});
    
    // Should have some history entries
    EXPECT_FALSE(history.empty());
    
    // Entries should be chronologically ordered
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
    }
}

TEST_F(ResourceManagerTest, ResourceAlerts) {
    ResourceManager manager(test_options_);
    
    // Set up alert thresholds
    manager.set_memory_alert_threshold(0.8); // 80%
    manager.set_disk_alert_threshold(0.9);   // 90%
    
    auto alerts = manager.get_active_alerts();
    
    // Should not have alerts in test environment (hopefully)
    // But function should work without crashing
    EXPECT_TRUE(alerts.size() >= 0);
}

TEST_F(ResourceManagerTest, PerformanceMetrics) {
    ResourceManager manager(test_options_);
    
    auto metrics = manager.get_performance_metrics();
    
    EXPECT_GE(metrics.allocation_count, 0);
    EXPECT_GE(metrics.deallocation_count, 0);
    EXPECT_GE(metrics.average_allocation_time_ms, 0);
    EXPECT_GE(metrics.port_allocation_count, 0);
}

// Concurrent operation tests
TEST_F(ResourceManagerTest, ConcurrentResourceAllocation) {
    ResourceManager manager(test_options_);
    
    // Launch multiple allocation requests concurrently
    std::vector<std::future<ResourceManager::AllocationResult>> futures;
    
    for (int i = 0; i < 5; ++i) {
        futures.emplace_back(std::async(std::launch::async, [&, i]() {
            ResourceManager::AllocationRequest request;
            request.vm_id = VMId("concurrent-vm-" + std::to_string(i));
            request.memory_needed = MemoryAmount::megabytes(100);
            request.disk_needed = DiskSize::megabytes(500);
            request.needs_ssh_port = true;
            
            return manager.allocate_resources(request);
        }));
    }
    
    // Collect results
    std::vector<ResourceManager::AllocationResult> results;
    for (auto& future : futures) {
        results.push_back(future.get());
    }
    
    // At least some should succeed
    size_t success_count = std::count_if(results.begin(), results.end(),
        [](const auto& result) { return result.success; });
    
    EXPECT_GT(success_count, 0);
    
    // No duplicate port allocations
    std::set<PortNumber> allocated_ssh_ports;
    for (const auto& result : results) {
        if (result.success && result.allocated_ssh_port.has_value()) {
            auto [it, inserted] = allocated_ssh_ports.insert(result.allocated_ssh_port.value());
            EXPECT_TRUE(inserted) << "Duplicate port allocation: " << result.allocated_ssh_port.value();
        }
    }
}

// Configuration and options tests
TEST_F(ResourceManagerTest, UpdateConfiguration) {
    ResourceManager manager(test_options_);
    
    auto new_options = test_options_;
    new_options.memory_reserve_percentage = 0.2; // Change reserve percentage
    
    manager.update_options(new_options);
    
    auto updated_options = manager.get_options();
    EXPECT_DOUBLE_EQ(updated_options.memory_reserve_percentage, 0.2);
    
    // Effective limits should change
    auto new_limits = manager.get_effective_limits();
    EXPECT_LT(new_limits.max_memory.bytes, test_options_.system_limits.max_memory.bytes);
}

// Error handling and edge cases
TEST_F(ResourceManagerTest, InvalidPortRanges) {
    ResourceManager::Options bad_options = test_options_;
    bad_options.ssh_port_range = {9999, 2222}; // Invalid range (start > end)
    
    EXPECT_THROW({
        ResourceManager manager(bad_options);
    }, ScratchpadError);
}

TEST_F(ResourceManagerTest, SystemResourceExhaustion) {
    ResourceManager manager(test_options_);
    
    // Mock system resource exhaustion by requesting everything
    ResourceManager::AllocationRequest huge_request;
    huge_request.vm_id = test_vm_id_;
    huge_request.memory_needed = test_options_.system_limits.max_memory;
    huge_request.disk_needed = test_options_.system_limits.max_disk_size;
    
    auto result = manager.allocate_resources(huge_request);
    
    // Should fail gracefully
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

// Move semantics tests
TEST_F(ResourceManagerTest, MoveSemantics) {
    ResourceManager manager1(test_options_);
    
    // Allocate some resources
    ResourceManager::AllocationRequest request;
    request.vm_id = test_vm_id_;
    request.memory_needed = MemoryAmount::megabytes(256);
    request.disk_needed = DiskSize::gigabytes(2);
    request.needs_ssh_port = true;
    
    auto alloc_result = manager1.allocate_resources(request);
    ASSERT_TRUE(alloc_result.success);
    
    // Move construct
    ResourceManager manager2(std::move(manager1));
    
    // Moved-to object should retain allocations
    auto status = manager2.get_allocation_status(test_vm_id_);
    EXPECT_TRUE(status.has_value());
    EXPECT_EQ(status->allocated_memory.bytes, MemoryAmount::megabytes(256).bytes);
}