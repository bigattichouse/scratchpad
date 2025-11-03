#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "scratchpad/vm_manager.hpp"
#include "utils/test_helpers.hpp"
#include "utils/mock_qemu_adapter.hpp"
#include "utils/mock_ssh_client.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>

using namespace scratchpad;
using namespace scratchpad::test;
using testing::_;
using testing::Return;
using testing::InSequence;

class VMManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::make_unique<TempDirectory>();
        
        // Set up test options
        test_options_.vm_directory = temp_dir_->create_subdirectory("vms").string();
        test_options_.ssh_keys_directory = temp_dir_->create_subdirectory("ssh_keys").string();
        test_options_.default_limits.max_memory = MemoryAmount::gigabytes(8);
        test_options_.default_limits.max_disk_size = DiskSize::gigabytes(100);
        test_options_.default_limits.max_cpu_cores = 8;
        test_options_.enable_health_monitoring = true;
        test_options_.health_check_interval = std::chrono::milliseconds{100}; // Fast for testing
        
        // Create test SSH keys
        TestHelpers::create_temp_ssh_keys(std::filesystem::path(test_options_.ssh_keys_directory));
        
        // Set up test VM IDs
        test_vm_id_ = VMId("test-vm-001");
        test_vm_id_2_ = VMId("test-vm-002");
        
        // Set up create parameters
        test_create_params_.vm_id = test_vm_id_;
        test_create_params_.base_image = ImageType::Ubuntu2204;
        test_create_params_.memory = MemoryAmount::gigabytes(1);
        test_create_params_.disk_size = DiskSize::gigabytes(20);
        test_create_params_.disk_mode = DiskMode::Ephemeral;
        test_create_params_.auto_start = false;
    }
    
    std::unique_ptr<TempDirectory> temp_dir_;
    VMManager::Options test_options_;
    VMId test_vm_id_;
    VMId test_vm_id_2_;
    VMManager::CreateParams test_create_params_;
};

// Construction and basic tests
TEST_F(VMManagerTest, ConstructionWithDefaultOptions) {
    EXPECT_NO_THROW({
        VMManager manager;
        EXPECT_EQ(manager.vm_count(), 0);
        EXPECT_TRUE(manager.list_vms().empty());
    });
}

TEST_F(VMManagerTest, ConstructionWithCustomOptions) {
    EXPECT_NO_THROW({
        VMManager manager(test_options_);
        EXPECT_EQ(manager.vm_count(), 0);
        EXPECT_TRUE(manager.list_vms().empty());
        
        // Verify options were applied
        auto options = manager.get_options();
        EXPECT_EQ(options.vm_directory, test_options_.vm_directory);
        EXPECT_EQ(options.ssh_keys_directory, test_options_.ssh_keys_directory);
    });
}

// VM creation tests
TEST_F(VMManagerTest, CreateVMBasic) {
    VMManager manager(test_options_);
    
    auto result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.vm_id, test_vm_id_);
    EXPECT_TRUE(result.error_message.empty());
    
    // VM should exist in manager
    EXPECT_EQ(manager.vm_count(), 1);
    EXPECT_TRUE(manager.vm_exists(test_vm_id_));
    
    auto vm_list = manager.list_vms();
    EXPECT_EQ(vm_list.size(), 1);
    EXPECT_EQ(vm_list[0], test_vm_id_);
}

TEST_F(VMManagerTest, CreateVMWithAutoStart) {
    VMManager manager(test_options_);
    
    test_create_params_.auto_start = true;
    
    auto result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(result.success);
    
    // Give some time for auto-start
    std::this_thread::sleep_for(std::chrono::milliseconds{200});
    
    // VM should be running or starting
    auto status = manager.get_vm_status(test_vm_id_);
    EXPECT_TRUE(status == VMStatus::Starting || status == VMStatus::Running);
}

TEST_F(VMManagerTest, CreateVMDuplicate) {
    VMManager manager(test_options_);
    
    // Create first VM
    auto result1 = manager.create_vm(test_create_params_);
    EXPECT_TRUE(result1.success);
    
    // Try to create duplicate
    auto result2 = manager.create_vm(test_create_params_);
    EXPECT_FALSE(result2.success);
    EXPECT_THAT(result2.error_message, testing::HasSubstr("already exists"));
    
    // Should still have only one VM
    EXPECT_EQ(manager.vm_count(), 1);
}

TEST_F(VMManagerTest, CreateVMInvalidParameters) {
    VMManager manager(test_options_);
    
    // Invalid VM ID
    auto invalid_params = test_create_params_;
    invalid_params.vm_id = VMId(); // Empty ID
    
    auto result = manager.create_vm(invalid_params);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
    
    // Invalid memory size
    invalid_params = test_create_params_;
    invalid_params.memory = MemoryAmount::bytes(1024); // Too small
    
    result = manager.create_vm(invalid_params);
    EXPECT_FALSE(result.success);
}

// VM lifecycle tests
TEST_F(VMManagerTest, StartStopVMLifecycle) {
    VMManager manager(test_options_);
    
    // Create VM
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Start VM
    auto start_result = manager.start_vm(test_vm_id_);
    EXPECT_TRUE(start_result.success);
    
    // Check status
    EXPECT_EQ(manager.get_vm_status(test_vm_id_), VMStatus::Starting);
    
    // Stop VM
    auto stop_result = manager.stop_vm(test_vm_id_);
    EXPECT_TRUE(stop_result.success);
    
    // Eventually should be stopped
    EXPECT_TRUE(TestHelpers::wait_for([&]() {
        return manager.get_vm_status(test_vm_id_) == VMStatus::Stopped;
    }, std::chrono::seconds{5}));
}

TEST_F(VMManagerTest, RestartVM) {
    VMManager manager(test_options_);
    
    // Create and start VM
    test_create_params_.auto_start = true;
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Wait for VM to start
    EXPECT_TRUE(TestHelpers::wait_for([&]() {
        auto status = manager.get_vm_status(test_vm_id_);
        return status == VMStatus::Running || status == VMStatus::Starting;
    }, std::chrono::seconds{5}));
    
    // Restart VM
    auto restart_result = manager.restart_vm(test_vm_id_);
    EXPECT_TRUE(restart_result.success);
    
    // Should go through stopping and starting states
    bool saw_stopping = false;
    bool saw_starting = false;
    
    for (int i = 0; i < 50; ++i) {
        auto status = manager.get_vm_status(test_vm_id_);
        if (status == VMStatus::Stopping) saw_stopping = true;
        if (status == VMStatus::Starting) saw_starting = true;
        std::this_thread::sleep_for(std::chrono::milliseconds{100});
    }
    
    EXPECT_TRUE(saw_stopping);
    EXPECT_TRUE(saw_starting);
}

TEST_F(VMManagerTest, DestroyVM) {
    VMManager manager(test_options_);
    
    // Create VM
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    EXPECT_TRUE(manager.vm_exists(test_vm_id_));
    
    // Destroy VM
    auto destroy_result = manager.destroy_vm(test_vm_id_);
    EXPECT_TRUE(destroy_result.success);
    
    // VM should no longer exist
    EXPECT_FALSE(manager.vm_exists(test_vm_id_));
    EXPECT_EQ(manager.vm_count(), 0);
}

// Command execution tests
TEST_F(VMManagerTest, ExecuteCommandBasic) {
    VMManager manager(test_options_);
    
    // Create and start VM
    test_create_params_.auto_start = true;
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Wait for VM to be ready
    EXPECT_TRUE(TestHelpers::wait_for([&]() {
        return manager.is_vm_ready(test_vm_id_);
    }, std::chrono::seconds{10}));
    
    // Execute command
    VMManager::ExecuteParams exec_params;
    exec_params.command = "echo 'Hello World'";
    exec_params.timeout = std::chrono::seconds{30};
    
    auto exec_result = manager.execute_command(test_vm_id_, exec_params);
    EXPECT_TRUE(exec_result.success);
    EXPECT_EQ(exec_result.exit_code, 0);
    EXPECT_THAT(exec_result.stdout_output, testing::HasSubstr("Hello World"));
}

TEST_F(VMManagerTest, ExecuteCommandWithTimeout) {
    VMManager manager(test_options_);
    
    // Create and start VM
    test_create_params_.auto_start = true;
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Wait for VM to be ready
    EXPECT_TRUE(TestHelpers::wait_for([&]() {
        return manager.is_vm_ready(test_vm_id_);
    }, std::chrono::seconds{10}));
    
    // Execute long-running command with short timeout
    VMManager::ExecuteParams exec_params;
    exec_params.command = "sleep 60";
    exec_params.timeout = std::chrono::milliseconds{200};
    
    auto exec_result = manager.execute_command(test_vm_id_, exec_params);
    EXPECT_FALSE(exec_result.success);
    EXPECT_THAT(exec_result.error_message, testing::HasSubstr("timeout"));
}

TEST_F(VMManagerTest, ExecuteCommandOnStoppedVM) {
    VMManager manager(test_options_);
    
    // Create but don't start VM
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Try to execute command
    VMManager::ExecuteParams exec_params;
    exec_params.command = "echo 'test'";
    
    auto exec_result = manager.execute_command(test_vm_id_, exec_params);
    EXPECT_FALSE(exec_result.success);
    EXPECT_THAT(exec_result.error_message, testing::AnyOf(
        testing::HasSubstr("not running"),
        testing::HasSubstr("not ready")
    ));
}

// Async operation tests
TEST_F(VMManagerTest, AsyncOperations) {
    VMManager manager(test_options_);
    
    // Track status changes
    std::vector<std::pair<VMId, VMStatus>> status_changes;
    manager.set_status_callback([&](const VMId& vm_id, VMStatus status, const std::string& message) {
        status_changes.emplace_back(vm_id, status);
    });
    
    // Create VM asynchronously
    auto create_future = manager.create_vm_async(test_create_params_);
    auto create_result = create_future.get();
    EXPECT_TRUE(create_result.success);
    
    // Start VM asynchronously
    auto start_future = manager.start_vm_async(test_vm_id_);
    auto start_result = start_future.get();
    EXPECT_TRUE(start_result.success);
    
    // Should have received status change callbacks
    EXPECT_FALSE(status_changes.empty());
    
    // Check that we saw at least the starting status
    auto it = std::find_if(status_changes.begin(), status_changes.end(),
        [&](const auto& change) {
            return change.first == test_vm_id_ && change.second == VMStatus::Starting;
        });
    EXPECT_NE(it, status_changes.end());
}

// File operations tests
TEST_F(VMManagerTest, FileOperations) {
    VMManager manager(test_options_);
    
    // Create and start VM
    test_create_params_.auto_start = true;
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Wait for VM to be ready
    EXPECT_TRUE(TestHelpers::wait_for([&]() {
        return manager.is_vm_ready(test_vm_id_);
    }, std::chrono::seconds{10}));
    
    // Create test file
    auto test_file = temp_dir_->path() / "test_upload.txt";
    temp_dir_->create_file("test_upload.txt", "Hello from host!");
    
    // Copy file to VM
    auto upload_result = manager.copy_file_to_vm(test_vm_id_, test_file.string(), "/tmp/test_file.txt");
    EXPECT_TRUE(upload_result.success);
    
    // Verify file exists in VM
    VMManager::ExecuteParams exec_params;
    exec_params.command = "cat /tmp/test_file.txt";
    auto exec_result = manager.execute_command(test_vm_id_, exec_params);
    EXPECT_TRUE(exec_result.success);
    EXPECT_THAT(exec_result.stdout_output, testing::HasSubstr("Hello from host!"));
    
    // Copy file from VM
    auto download_file = temp_dir_->path() / "test_download.txt";
    auto download_result = manager.copy_file_from_vm(test_vm_id_, "/tmp/test_file.txt", download_file.string());
    EXPECT_TRUE(download_result.success);
    
    // Verify downloaded file
    EXPECT_TRUE(std::filesystem::exists(download_file));
}

// Multi-VM management tests
TEST_F(VMManagerTest, MultipleVMManagement) {
    VMManager manager(test_options_);
    
    // Create multiple VMs
    auto create_result1 = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result1.success);
    
    auto params2 = test_create_params_;
    params2.vm_id = test_vm_id_2_;
    auto create_result2 = manager.create_vm(params2);
    EXPECT_TRUE(create_result2.success);
    
    EXPECT_EQ(manager.vm_count(), 2);
    
    // Start both VMs
    auto start_result1 = manager.start_vm(test_vm_id_);
    auto start_result2 = manager.start_vm(test_vm_id_2_);
    EXPECT_TRUE(start_result1.success);
    EXPECT_TRUE(start_result2.success);
    
    // Get running VMs
    auto running_vms = manager.get_running_vms();
    EXPECT_GE(running_vms.size(), 2);
    
    // Stop all VMs
    auto stop_all_result = manager.stop_all_vms();
    EXPECT_TRUE(stop_all_result.success);
    
    // Wait for all to stop
    EXPECT_TRUE(TestHelpers::wait_for([&]() {
        return manager.get_running_vms().empty();
    }, std::chrono::seconds{10}));
}

// Resource management tests
TEST_F(VMManagerTest, ResourceLimits) {
    // Set strict resource limits
    test_options_.default_limits.max_memory = MemoryAmount::megabytes(512);
    test_options_.default_limits.max_cpu_cores = 2;
    
    VMManager manager(test_options_);
    
    // Try to create VM that exceeds limits
    test_create_params_.memory = MemoryAmount::gigabytes(2); // Exceeds limit
    test_create_params_.cpu_cores = 4; // Exceeds limit
    
    auto result = manager.create_vm(test_create_params_);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("memory"),
        testing::HasSubstr("limit"),
        testing::HasSubstr("exceed")
    ));
}

TEST_F(VMManagerTest, ResourceUsageMonitoring) {
    VMManager manager(test_options_);
    
    // Create and start VM
    test_create_params_.auto_start = true;
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Wait for VM to start
    std::this_thread::sleep_for(std::chrono::milliseconds{500});
    
    // Get resource usage
    auto usage = manager.get_vm_resource_usage(test_vm_id_);
    EXPECT_GE(usage.memory_used.bytes, 0);
    EXPECT_GE(usage.cpu_percent, 0.0);
    
    // Get system resource usage
    auto system_usage = manager.get_system_resource_usage();
    EXPECT_GT(system_usage.total_memory.bytes, 0);
    EXPECT_GE(system_usage.available_memory.bytes, 0);
}

// Health monitoring tests
TEST_F(VMManagerTest, HealthMonitoring) {
    VMManager manager(test_options_);
    
    // Create and start VM
    test_create_params_.auto_start = true;
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Wait for health monitoring to kick in
    std::this_thread::sleep_for(std::chrono::milliseconds{300});
    
    // Check VM health
    auto health = manager.get_vm_health(test_vm_id_);
    EXPECT_FALSE(health.status == HealthStatus::Unknown);
    
    // Get health report
    auto report = manager.get_health_report();
    EXPECT_EQ(report.total_vms, 1);
    EXPECT_GE(report.healthy_vms, 0);
    EXPECT_LE(report.unhealthy_vms, 1);
}

// Error handling and edge cases
TEST_F(VMManagerTest, OperationsOnNonexistentVM) {
    VMManager manager(test_options_);
    
    VMId nonexistent("nonexistent-vm");
    
    // All operations should fail gracefully
    EXPECT_FALSE(manager.vm_exists(nonexistent));
    
    auto start_result = manager.start_vm(nonexistent);
    EXPECT_FALSE(start_result.success);
    
    auto stop_result = manager.stop_vm(nonexistent);
    EXPECT_FALSE(stop_result.success);
    
    VMManager::ExecuteParams exec_params;
    exec_params.command = "echo test";
    auto exec_result = manager.execute_command(nonexistent, exec_params);
    EXPECT_FALSE(exec_result.success);
}

TEST_F(VMManagerTest, ConcurrentOperations) {
    VMManager manager(test_options_);
    
    // Create VM
    auto create_result = manager.create_vm(test_create_params_);
    EXPECT_TRUE(create_result.success);
    
    // Try concurrent start operations
    auto future1 = std::async(std::launch::async, [&]() {
        return manager.start_vm(test_vm_id_);
    });
    
    auto future2 = std::async(std::launch::async, [&]() {
        return manager.start_vm(test_vm_id_);
    });
    
    auto result1 = future1.get();
    auto result2 = future2.get();
    
    // One should succeed, one might fail or both might succeed
    // depending on the exact timing
    EXPECT_TRUE(result1.success || result2.success);
}

// Cleanup and shutdown tests
TEST_F(VMManagerTest, GracefulShutdown) {
    {
        VMManager manager(test_options_);
        
        // Create multiple VMs
        auto create_result1 = manager.create_vm(test_create_params_);
        EXPECT_TRUE(create_result1.success);
        
        auto params2 = test_create_params_;
        params2.vm_id = test_vm_id_2_;
        auto create_result2 = manager.create_vm(params2);
        EXPECT_TRUE(create_result2.success);
        
        // Start them
        manager.start_vm(test_vm_id_);
        manager.start_vm(test_vm_id_2_);
        
        // Manager should clean up on destruction
    }
    
    // All VMs should be stopped after manager destruction
    // (This would be verified by checking no QEMU processes are running)
}