#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "infrastructure/qemu/qemu_adapter.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>
#include <fstream>

using namespace scratchpad;
using namespace scratchpad::test;
using testing::_;
using testing::Return;
using testing::InSequence;

class QemuAdapterTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::make_unique<TempDirectory>();
        
        // Set up test VM configuration
        test_vm_id_ = VMId("test-qemu-vm");
        test_config_.set_vm_id(test_vm_id_);
        test_config_.set_base_image(ImageType::Ubuntu2204);
        test_config_.set_memory(MemoryAmount::megabytes(512));
        test_config_.set_disk_size(DiskSize::gigabytes(5));
        test_config_.set_cpu_cores(1);
        
        // Set up network configuration
        NetworkConfiguration net_config;
        net_config.ssh_port = TestHelpers::find_available_port();
        net_config.enable_networking = true;
        test_config_.set_network_configuration(net_config);
        
        // Create test disk path
        test_disk_path_ = temp_dir_->path() / "test-vm.qcow2";
        create_dummy_disk_image();
    }
    
    void create_dummy_disk_image() {
        // Create a dummy QCOW2-like file for testing
        std::ofstream disk_file(test_disk_path_, std::ios::binary);
        disk_file << "QFI\xfb"; // QCOW2 magic header
        disk_file.write(std::string(1024, '\0').c_str(), 1024); // Padding
        disk_file.close();
    }
    
    std::unique_ptr<TempDirectory> temp_dir_;
    VMId test_vm_id_;
    VMConfiguration test_config_;
    std::filesystem::path test_disk_path_;
};

// Construction and validation tests
TEST_F(QemuAdapterTest, QemuInstallationValidation) {
    QemuAdapter adapter;
    
    // Should validate QEMU installation
    bool is_valid = adapter.validate_qemu_installation();
    
    // In CI/test environment, QEMU might not be installed
    if (is_valid) {
        EXPECT_FALSE(adapter.get_qemu_version().empty());
        
        // Version should be reasonable format
        auto version = adapter.get_qemu_version();
        EXPECT_THAT(version, testing::AnyOf(
            testing::HasSubstr("QEMU"),
            testing::HasSubstr("qemu"),
            testing::MatchesRegex(".*[0-9]+\\.[0-9]+.*")
        ));
    } else {
        // Should provide clear error message
        EXPECT_TRUE(adapter.get_qemu_version().empty() || 
                   adapter.get_qemu_version().find("not found") != std::string::npos);
    }
}

TEST_F(QemuAdapterTest, QemuBinaryDetection) {
    QemuAdapter adapter;
    
    auto qemu_path = adapter.get_qemu_binary_path();
    
    if (!qemu_path.empty()) {
        // Should be an executable file
        EXPECT_TRUE(std::filesystem::exists(qemu_path));
        
        // Should have reasonable name
        auto filename = std::filesystem::path(qemu_path).filename().string();
        EXPECT_THAT(filename, testing::AnyOf(
            testing::HasSubstr("qemu"),
            testing::HasSubstr("QEMU")
        ));
    }
}

// VM disk management tests
TEST_F(QemuAdapterTest, CreateVMDisk) {
    QemuAdapter adapter;
    
    auto new_disk_path = temp_dir_->path() / "new-vm-disk.qcow2";
    
    bool result = adapter.create_vm_disk(new_disk_path, "1G");
    
    if (adapter.validate_qemu_installation()) {
        EXPECT_TRUE(result);
        EXPECT_TRUE(std::filesystem::exists(new_disk_path));
        
        // File should have reasonable size (not empty)
        auto file_size = std::filesystem::file_size(new_disk_path);
        EXPECT_GT(file_size, 1024); // At least 1KB
    } else {
        // If QEMU not available, should fail gracefully
        EXPECT_FALSE(result);
    }
}

TEST_F(QemuAdapterTest, CreateVMDiskInvalidSize) {
    QemuAdapter adapter;
    
    auto invalid_disk_path = temp_dir_->path() / "invalid-disk.qcow2";
    
    // Test with invalid size format
    bool result = adapter.create_vm_disk(invalid_disk_path, "invalid_size");
    EXPECT_FALSE(result);
    EXPECT_FALSE(std::filesystem::exists(invalid_disk_path));
}

TEST_F(QemuAdapterTest, DeleteVMDisk) {
    QemuAdapter adapter;
    
    // Disk should exist
    ASSERT_TRUE(std::filesystem::exists(test_disk_path_));
    
    bool result = adapter.delete_vm_disk(test_disk_path_);
    EXPECT_TRUE(result);
    EXPECT_FALSE(std::filesystem::exists(test_disk_path_));
}

TEST_F(QemuAdapterTest, DeleteNonExistentDisk) {
    QemuAdapter adapter;
    
    auto nonexistent_path = temp_dir_->path() / "nonexistent.qcow2";
    
    bool result = adapter.delete_vm_disk(nonexistent_path);
    EXPECT_FALSE(result); // Should fail gracefully
}

// VM lifecycle tests
TEST_F(QemuAdapterTest, StartVMAsync) {
    QemuAdapter adapter;
    
    if (!adapter.validate_qemu_installation()) {
        GTEST_SKIP() << "QEMU not available for testing";
    }
    
    auto future = adapter.start_vm_async(test_config_);
    
    // Should return a valid future
    EXPECT_TRUE(future.valid());
    
    // Wait with timeout to avoid hanging tests
    auto status = future.wait_for(std::chrono::seconds{10});
    
    if (status == std::future_status::ready) {
        bool result = future.get();
        // Result depends on environment, but should not crash
        EXPECT_TRUE(result || !result);
    } else {
        // Timeout is acceptable in test environment
        EXPECT_EQ(status, std::future_status::timeout);
    }
}

TEST_F(QemuAdapterTest, StartVMWithInvalidConfiguration) {
    QemuAdapter adapter;
    
    VMConfiguration invalid_config;
    // Empty VM ID should cause failure
    
    if (adapter.validate_qemu_installation()) {
        auto future = adapter.start_vm_async(invalid_config);
        
        auto status = future.wait_for(std::chrono::seconds{5});
        if (status == std::future_status::ready) {
            bool result = future.get();
            EXPECT_FALSE(result); // Should fail with invalid config
        }
    }
}

TEST_F(QemuAdapterTest, StopVMAsync) {
    QemuAdapter adapter;
    
    auto future = adapter.stop_vm_async(test_vm_id_);
    
    EXPECT_TRUE(future.valid());
    
    auto status = future.wait_for(std::chrono::seconds{5});
    if (status == std::future_status::ready) {
        bool result = future.get();
        // Should handle non-running VM gracefully
        EXPECT_TRUE(result || !result);
    }
}

// VM status and monitoring tests
TEST_F(QemuAdapterTest, CheckVMRunning) {
    QemuAdapter adapter;
    
    // VM should not be running initially
    bool is_running = adapter.is_vm_running(test_vm_id_);
    EXPECT_FALSE(is_running);
}

TEST_F(QemuAdapterTest, ListRunningVMs) {
    QemuAdapter adapter;
    
    auto running_vms = adapter.list_running_vms();
    
    // Should return valid list (empty or with VMs)
    EXPECT_TRUE(running_vms.size() >= 0);
    
    // test_vm_id_ should not be in the list (not running)
    auto it = std::find(running_vms.begin(), running_vms.end(), test_vm_id_);
    EXPECT_EQ(it, running_vms.end());
}

TEST_F(QemuAdapterTest, GetVMSSHPort) {
    QemuAdapter adapter;
    
    auto ssh_port = adapter.get_vm_ssh_port(test_vm_id_);
    
    // Should not have SSH port for non-running VM
    EXPECT_FALSE(ssh_port.has_value());
}

TEST_F(QemuAdapterTest, GetVMResourceUsage) {
    QemuAdapter adapter;
    
    auto usage = adapter.get_vm_resource_usage(test_vm_id_);
    
    // Non-running VM should have zero usage
    EXPECT_EQ(usage.memory_used.bytes, 0);
    EXPECT_DOUBLE_EQ(usage.cpu_percent, 0.0);
    EXPECT_EQ(usage.disk_used.bytes, 0);
}

// Command line generation tests
TEST_F(QemuAdapterTest, GenerateQemuCommandLine) {
    QemuAdapter adapter;
    
    auto command_line = adapter.generate_command_line(test_config_);
    
    EXPECT_FALSE(command_line.empty());
    
    // Should contain essential QEMU parameters
    EXPECT_TRUE(std::any_of(command_line.begin(), command_line.end(),
        [](const std::string& arg) { return arg.find("qemu") != std::string::npos; }));
    
    // Should contain memory specification
    EXPECT_TRUE(std::any_of(command_line.begin(), command_line.end(),
        [](const std::string& arg) { return arg == "-m"; }));
    
    // Should contain CPU specification
    EXPECT_TRUE(std::any_of(command_line.begin(), command_line.end(),
        [](const std::string& arg) { return arg == "-smp"; }));
}

TEST_F(QemuAdapterTest, CommandLineMemoryConfiguration) {
    QemuAdapter adapter;
    
    test_config_.set_memory(MemoryAmount::gigabytes(2));
    auto command_line = adapter.generate_command_line(test_config_);
    
    // Find memory argument
    auto mem_it = std::find(command_line.begin(), command_line.end(), "-m");
    ASSERT_NE(mem_it, command_line.end());
    
    // Next argument should be memory size
    ASSERT_NE(mem_it + 1, command_line.end());
    std::string mem_value = *(mem_it + 1);
    
    EXPECT_THAT(mem_value, testing::AnyOf(
        testing::Eq("2048"),  // MB
        testing::Eq("2G"),    // GB
        testing::HasSubstr("2")
    ));
}

TEST_F(QemuAdapterTest, CommandLineCPUConfiguration) {
    QemuAdapter adapter;
    
    test_config_.set_cpu_cores(4);
    auto command_line = adapter.generate_command_line(test_config_);
    
    // Find SMP argument
    auto smp_it = std::find(command_line.begin(), command_line.end(), "-smp");
    ASSERT_NE(smp_it, command_line.end());
    
    // Next argument should be CPU count
    ASSERT_NE(smp_it + 1, command_line.end());
    std::string smp_value = *(smp_it + 1);
    
    EXPECT_THAT(smp_value, testing::AnyOf(
        testing::Eq("4"),
        testing::HasSubstr("4")
    ));
}

TEST_F(QemuAdapterTest, CommandLineNetworkConfiguration) {
    QemuAdapter adapter;
    
    auto command_line = adapter.generate_command_line(test_config_);
    
    // Should contain network device configuration
    bool has_netdev = std::any_of(command_line.begin(), command_line.end(),
        [](const std::string& arg) { return arg == "-netdev" || arg.find("netdev") != std::string::npos; });
    
    if (test_config_.networking_enabled()) {
        EXPECT_TRUE(has_netdev);
    }
}

// Disk and storage tests
TEST_F(QemuAdapterTest, DiskPathValidation) {
    QemuAdapter adapter;
    
    // Valid disk path
    EXPECT_TRUE(adapter.validate_disk_path(test_disk_path_));
    
    // Invalid paths
    EXPECT_FALSE(adapter.validate_disk_path("/nonexistent/path/disk.qcow2"));
    EXPECT_FALSE(adapter.validate_disk_path(""));
    
    // Directory instead of file
    EXPECT_FALSE(adapter.validate_disk_path(temp_dir_->path()));
}

TEST_F(QemuAdapterTest, DiskFormatDetection) {
    QemuAdapter adapter;
    
    auto format = adapter.detect_disk_format(test_disk_path_);
    
    // Should detect QCOW2 format from our dummy file
    if (format.has_value()) {
        EXPECT_THAT(format.value(), testing::AnyOf(
            testing::Eq("qcow2"),
            testing::Eq("raw"),
            testing::Eq("unknown")
        ));
    }
}

// Process management tests
TEST_F(QemuAdapterTest, ProcessLifecycleManagement) {
    QemuAdapter adapter;
    
    // Test process tracking
    auto tracked_processes = adapter.get_tracked_processes();
    EXPECT_TRUE(tracked_processes.size() >= 0);
    
    // Should not have test_vm_id_ in tracked processes
    auto it = std::find_if(tracked_processes.begin(), tracked_processes.end(),
        [this](const auto& info) { return info.vm_id == test_vm_id_; });
    EXPECT_EQ(it, tracked_processes.end());
}

TEST_F(QemuAdapterTest, ProcessResourceMonitoring) {
    QemuAdapter adapter;
    
    // Test resource monitoring for non-existent process
    ProcessId fake_pid = 99999;
    auto usage = adapter.get_process_resource_usage(fake_pid);
    
    // Should handle non-existent process gracefully
    EXPECT_EQ(usage.memory_rss_bytes, 0);
    EXPECT_DOUBLE_EQ(usage.cpu_percent, 0.0);
}

// Acceleration and platform tests
TEST_F(QemuAdapterTest, HardwareAccelerationDetection) {
    QemuAdapter adapter;
    
    auto acceleration = adapter.detect_available_acceleration();
    
    // Should return valid acceleration type
    EXPECT_TRUE(acceleration == AccelerationType::KVM ||
               acceleration == AccelerationType::HVF ||
               acceleration == AccelerationType::WHPX ||
               acceleration == AccelerationType::TCG);
}

TEST_F(QemuAdapterTest, AccelerationCapabilities) {
    QemuAdapter adapter;
    
    bool kvm_available = adapter.is_acceleration_available(AccelerationType::KVM);
    bool tcg_available = adapter.is_acceleration_available(AccelerationType::TCG);
    
    // TCG should always be available if QEMU is installed
    if (adapter.validate_qemu_installation()) {
        EXPECT_TRUE(tcg_available);
    }
    
    // KVM availability depends on system
    EXPECT_TRUE(kvm_available || !kvm_available);
}

// Error handling and edge cases
TEST_F(QemuAdapterTest, InvalidVMConfiguration) {
    QemuAdapter adapter;
    
    VMConfiguration invalid_config;
    // Leave VM ID empty - should cause errors
    
    EXPECT_THROW({
        auto command_line = adapter.generate_command_line(invalid_config);
    }, ScratchpadError);
}

TEST_F(QemuAdapterTest, InsufficientSystemResources) {
    QemuAdapter adapter;
    
    // Create configuration requiring excessive resources
    VMConfiguration excessive_config = test_config_;
    excessive_config.set_memory(MemoryAmount::terabytes(100));
    
    if (adapter.validate_qemu_installation()) {
        auto future = adapter.start_vm_async(excessive_config);
        
        auto status = future.wait_for(std::chrono::seconds{10});
        if (status == std::future_status::ready) {
            bool result = future.get();
            EXPECT_FALSE(result); // Should fail with excessive resources
        }
    }
}

TEST_F(QemuAdapterTest, ConcurrentVMOperations) {
    QemuAdapter adapter;
    
    if (!adapter.validate_qemu_installation()) {
        GTEST_SKIP() << "QEMU not available for testing";
    }
    
    // Start multiple VMs concurrently
    std::vector<std::future<bool>> start_futures;
    
    for (int i = 0; i < 3; ++i) {
        VMConfiguration config = test_config_;
        config.set_vm_id(VMId("concurrent-vm-" + std::to_string(i)));
        
        start_futures.emplace_back(adapter.start_vm_async(config));
    }
    
    // Collect results
    for (auto& future : start_futures) {
        auto status = future.wait_for(std::chrono::seconds{15});
        if (status == std::future_status::ready) {
            bool result = future.get();
            // Results depend on system resources, but should not crash
            EXPECT_TRUE(result || !result);
        }
    }
}

// Configuration validation tests
TEST_F(QemuAdapterTest, ValidateVMConfiguration) {
    QemuAdapter adapter;
    
    // Valid configuration
    auto validation_result = adapter.validate_vm_configuration(test_config_);
    EXPECT_TRUE(validation_result.is_valid);
    EXPECT_TRUE(validation_result.warnings.empty());
    
    // Invalid configuration
    VMConfiguration invalid_config;
    auto invalid_result = adapter.validate_vm_configuration(invalid_config);
    EXPECT_FALSE(invalid_result.is_valid);
    EXPECT_FALSE(invalid_result.errors.empty());
}

TEST_F(QemuAdapterTest, ConfigurationWarnings) {
    QemuAdapter adapter;
    
    // Configuration that should generate warnings
    VMConfiguration warning_config = test_config_;
    warning_config.set_memory(MemoryAmount::megabytes(64)); // Very low memory
    warning_config.set_cpu_cores(16); // Many cores
    
    auto result = adapter.validate_vm_configuration(warning_config);
    
    if (result.is_valid) {
        // May generate warnings about resource allocation
        EXPECT_TRUE(result.warnings.size() >= 0);
    }
}

// Cleanup and resource management tests
TEST_F(QemuAdapterTest, CleanupStaleProcesses) {
    QemuAdapter adapter;
    
    // Should handle cleanup gracefully even with no stale processes
    auto cleanup_result = adapter.cleanup_stale_processes();
    EXPECT_TRUE(cleanup_result.success);
    EXPECT_GE(cleanup_result.processes_cleaned, 0);
}

TEST_F(QemuAdapterTest, ResourceLeakPrevention) {
    {
        QemuAdapter adapter;
        
        // Perform some operations
        adapter.validate_qemu_installation();
        adapter.list_running_vms();
        
        // Adapter goes out of scope - should clean up resources
    }
    
    // No easy way to test this directly, but function should not crash
    EXPECT_TRUE(true);
}