#include <gtest/gtest.h>
#include "scratchpad/domain/vm/vm_configuration.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"

using namespace scratchpad;
using namespace scratchpad::test;

class VMConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_vm_id_ = VMId("test-vm");
        test_image_type_ = ImageType::Ubuntu2204;
    }
    
    VMId test_vm_id_;
    ImageType test_image_type_;
};

// Basic construction tests
TEST_F(VMConfigurationTest, DefaultConstruction) {
    VMConfiguration config;
    
    // Should have sensible defaults
    EXPECT_TRUE(config.vm_id().empty());
    EXPECT_FALSE(config.memory().bytes == 0);
    EXPECT_FALSE(config.disk_size().bytes == 0);
    EXPECT_GE(config.cpu_cores(), 1);
    EXPECT_LE(config.cpu_cores(), 32);
    EXPECT_TRUE(config.packages().empty());
    EXPECT_TRUE(config.environment_variables().empty());
    EXPECT_TRUE(config.custom_commands().empty());
    EXPECT_FALSE(config.has_work_directory());
}

TEST_F(VMConfigurationTest, ConstructionWithParameters) {
    VMConfiguration config(test_vm_id_, test_image_type_);
    
    EXPECT_EQ(config.vm_id().value(), "test-vm");
    EXPECT_EQ(config.base_image(), test_image_type_);
    EXPECT_NO_THROW(config.validate());
}

// Basic property tests
TEST_F(VMConfigurationTest, VMIdProperty) {
    VMConfiguration config;
    
    config.set_vm_id(test_vm_id_);
    EXPECT_EQ(config.vm_id().value(), "test-vm");
    
    VMId new_id("new-vm");
    config.set_vm_id(new_id);
    EXPECT_EQ(config.vm_id().value(), "new-vm");
}

TEST_F(VMConfigurationTest, BaseImageProperty) {
    VMConfiguration config;
    
    config.set_base_image(ImageType::Alpine317);
    EXPECT_EQ(config.base_image(), ImageType::Alpine317);
    
    config.set_base_image(ImageType::Ubuntu2204);
    EXPECT_EQ(config.base_image(), ImageType::Ubuntu2204);
}

// Resource configuration tests
TEST_F(VMConfigurationTest, MemoryConfiguration) {
    VMConfiguration config;
    
    // Test setting valid memory amounts
    MemoryAmount mem_512mb = MemoryAmount::from_string("512M");
    config.set_memory(mem_512mb);
    EXPECT_EQ(config.memory().bytes, mem_512mb.bytes);
    
    MemoryAmount mem_2gb = MemoryAmount::from_string("2G");
    config.set_memory(mem_2gb);
    EXPECT_EQ(config.memory().bytes, mem_2gb.bytes);
    
    // Test invalid memory amounts (too small/large)
    MemoryAmount mem_too_small = MemoryAmount::from_bytes(32 * 1024 * 1024); // 32MB
    EXPECT_THROW(config.set_memory(mem_too_small), ScratchpadError);
    
    MemoryAmount mem_too_large = MemoryAmount::from_string("128G");
    EXPECT_THROW(config.set_memory(mem_too_large), ScratchpadError);
}

TEST_F(VMConfigurationTest, DiskSizeConfiguration) {
    VMConfiguration config;
    
    // Test setting valid disk sizes
    DiskSize disk_5gb = DiskSize::from_string("5G");
    config.set_disk_size(disk_5gb);
    EXPECT_EQ(config.disk_size().bytes, disk_5gb.bytes);
    
    DiskSize disk_20gb = DiskSize::from_string("20G");
    config.set_disk_size(disk_20gb);
    EXPECT_EQ(config.disk_size().bytes, disk_20gb.bytes);
    
    // Test invalid disk sizes
    DiskSize disk_too_small = DiskSize::from_string("500M");
    EXPECT_THROW(config.set_disk_size(disk_too_small), ScratchpadError);
    
    DiskSize disk_too_large = DiskSize::from_string("2T");
    EXPECT_THROW(config.set_disk_size(disk_too_large), ScratchpadError);
}

TEST_F(VMConfigurationTest, CPUCoresConfiguration) {
    VMConfiguration config;
    
    // Test valid CPU core counts
    config.set_cpu_cores(1);
    EXPECT_EQ(config.cpu_cores(), 1);
    
    config.set_cpu_cores(4);
    EXPECT_EQ(config.cpu_cores(), 4);
    
    config.set_cpu_cores(32);
    EXPECT_EQ(config.cpu_cores(), 32);
    
    // Test invalid CPU core counts
    EXPECT_THROW(config.set_cpu_cores(0), ScratchpadError);
    EXPECT_THROW(config.set_cpu_cores(33), ScratchpadError);
}

TEST_F(VMConfigurationTest, DiskModeConfiguration) {
    VMConfiguration config;
    
    config.set_disk_mode(DiskMode::Ephemeral);
    EXPECT_EQ(config.disk_mode(), DiskMode::Ephemeral);
    
    config.set_disk_mode(DiskMode::Persistent);
    EXPECT_EQ(config.disk_mode(), DiskMode::Persistent);
}

// Feature flag tests
TEST_F(VMConfigurationTest, AccelerationFlag) {
    VMConfiguration config;
    
    config.set_acceleration_enabled(true);
    EXPECT_TRUE(config.acceleration_enabled());
    
    config.set_acceleration_enabled(false);
    EXPECT_FALSE(config.acceleration_enabled());
}

TEST_F(VMConfigurationTest, NetworkingFlag) {
    VMConfiguration config;
    
    config.set_networking_enabled(true);
    EXPECT_TRUE(config.networking_enabled());
    
    config.set_networking_enabled(false);
    EXPECT_FALSE(config.networking_enabled());
}

TEST_F(VMConfigurationTest, SSHFlag) {
    VMConfiguration config;
    
    config.set_ssh_enabled(true);
    EXPECT_TRUE(config.ssh_enabled());
    
    config.set_ssh_enabled(false);
    EXPECT_FALSE(config.ssh_enabled());
}

TEST_F(VMConfigurationTest, VNCFlag) {
    VMConfiguration config;
    
    config.set_vnc_enabled(true);
    EXPECT_TRUE(config.vnc_enabled());
    
    config.set_vnc_enabled(false);
    EXPECT_FALSE(config.vnc_enabled());
}

// Package management tests
TEST_F(VMConfigurationTest, PackageManagement) {
    VMConfiguration config;
    
    // Initially empty
    EXPECT_TRUE(config.packages().empty());
    EXPECT_FALSE(config.has_package("git"));
    
    // Add packages
    config.add_package("git");
    EXPECT_TRUE(config.has_package("git"));
    EXPECT_EQ(config.packages().size(), 1);
    
    config.add_package("vim");
    config.add_package("curl");
    EXPECT_EQ(config.packages().size(), 3);
    EXPECT_TRUE(config.has_package("vim"));
    EXPECT_TRUE(config.has_package("curl"));
    
    // Remove package
    config.remove_package("vim");
    EXPECT_FALSE(config.has_package("vim"));
    EXPECT_EQ(config.packages().size(), 2);
    
    // Set complete package list
    PackageList new_packages = {"python3", "nodejs", "docker"};
    config.set_packages(new_packages);
    EXPECT_EQ(config.packages().size(), 3);
    EXPECT_TRUE(config.has_package("python3"));
    EXPECT_FALSE(config.has_package("git"));
    
    // Test empty package name
    EXPECT_THROW(config.add_package(""), ScratchpadError);
    
    PackageList invalid_packages = {"valid", "", "also_valid"};
    EXPECT_THROW(config.set_packages(invalid_packages), ScratchpadError);
}

// Environment variable tests
TEST_F(VMConfigurationTest, EnvironmentVariables) {
    VMConfiguration config;
    
    // Initially empty
    EXPECT_TRUE(config.environment_variables().empty());
    EXPECT_FALSE(config.get_environment_variable("PATH").has_value());
    
    // Add variables
    config.add_environment_variable("PATH", "/usr/bin:/bin");
    config.add_environment_variable("USER", "testuser");
    
    EXPECT_EQ(config.environment_variables().size(), 2);
    EXPECT_EQ(config.get_environment_variable("PATH").value(), "/usr/bin:/bin");
    EXPECT_EQ(config.get_environment_variable("USER").value(), "testuser");
    
    // Update existing variable
    config.add_environment_variable("PATH", "/custom/bin:/usr/bin:/bin");
    EXPECT_EQ(config.get_environment_variable("PATH").value(), "/custom/bin:/usr/bin:/bin");
    EXPECT_EQ(config.environment_variables().size(), 2); // Size shouldn't change
    
    // Remove variable
    config.remove_environment_variable("USER");
    EXPECT_FALSE(config.get_environment_variable("USER").has_value());
    EXPECT_EQ(config.environment_variables().size(), 1);
    
    // Test empty name
    EXPECT_THROW(config.add_environment_variable("", "value"), ScratchpadError);
}

// Custom command tests
TEST_F(VMConfigurationTest, CustomCommands) {
    VMConfiguration config;
    
    // Initially empty
    EXPECT_TRUE(config.custom_commands().empty());
    
    // Add commands
    config.add_custom_command("apt-get update");
    config.add_custom_command("apt-get install -y build-essential");
    
    EXPECT_EQ(config.custom_commands().size(), 2);
    EXPECT_EQ(config.custom_commands()[0], "apt-get update");
    EXPECT_EQ(config.custom_commands()[1], "apt-get install -y build-essential");
    
    // Set all commands
    std::vector<std::string> new_commands = {
        "echo 'Setup starting'",
        "mkdir -p /workspace",
        "echo 'Setup complete'"
    };
    config.set_custom_commands(new_commands);
    EXPECT_EQ(config.custom_commands().size(), 3);
    EXPECT_EQ(config.custom_commands()[0], "echo 'Setup starting'");
    
    // Clear commands
    config.clear_custom_commands();
    EXPECT_TRUE(config.custom_commands().empty());
    
    // Test empty command
    EXPECT_THROW(config.add_custom_command(""), ScratchpadError);
}

// Work directory tests
TEST_F(VMConfigurationTest, WorkDirectory) {
    VMConfiguration config;
    
    // Initially no work directory
    EXPECT_FALSE(config.has_work_directory());
    EXPECT_TRUE(config.work_directory().empty());
    
    // Set work directory
    config.set_work_directory("/home/user/project");
    EXPECT_TRUE(config.has_work_directory());
    EXPECT_EQ(config.work_directory(), "/home/user/project");
    
    // Test relative path (should throw)
    EXPECT_THROW(config.set_work_directory("relative/path"), ScratchpadError);
    
    // Test empty path
    config.set_work_directory("");
    EXPECT_FALSE(config.has_work_directory());
}

// Network configuration tests
TEST_F(VMConfigurationTest, NetworkConfiguration) {
    VMConfiguration config;
    
    NetworkConfiguration net_config;
    net_config.ssh_port = 2222;
    net_config.vnc_port = 5900;
    net_config.enable_port_forwarding = true;
    
    config.set_network_configuration(net_config);
    EXPECT_EQ(config.network_config().ssh_port, 2222);
    EXPECT_EQ(config.network_config().vnc_port, 5900);
    EXPECT_TRUE(config.network_config().enable_port_forwarding);
    
    // Test invalid port configuration
    NetworkConfiguration invalid_config;
    invalid_config.ssh_port = 0; // Invalid port
    EXPECT_THROW(config.set_network_configuration(invalid_config), ScratchpadError);
    
    invalid_config.ssh_port = 65536; // Port too high
    EXPECT_THROW(config.set_network_configuration(invalid_config), ScratchpadError);
}

// Validation tests
TEST_F(VMConfigurationTest, ConfigurationValidation) {
    // Valid configuration
    VMConfiguration valid_config(test_vm_id_, test_image_type_);
    EXPECT_TRUE(valid_config.validate());
    
    // Invalid configuration (empty VM ID)
    VMConfiguration invalid_config;
    EXPECT_THROW(invalid_config.validate(), ScratchpadError);
}

TEST_F(VMConfigurationTest, ResourceLimitChecking) {
    VMConfiguration config(test_vm_id_, test_image_type_);
    
    ResourceLimits limits;
    limits.max_memory = MemoryAmount::from_string("4G");
    limits.max_disk_size = DiskSize::from_string("50G");
    limits.max_cpu_cores = 8;
    
    // Configuration should fit within limits
    config.set_memory(MemoryAmount::from_string("2G"));
    config.set_disk_size(DiskSize::from_string("20G"));
    config.set_cpu_cores(4);
    
    EXPECT_TRUE(config.fits_within_limits(limits));
    
    // Configuration exceeds memory limit
    config.set_memory(MemoryAmount::from_string("8G"));
    EXPECT_FALSE(config.fits_within_limits(limits));
}

// Factory method tests
TEST_F(VMConfigurationTest, CreateMinimal) {
    VMConfiguration config = VMConfiguration::create_minimal(test_vm_id_, test_image_type_);
    
    EXPECT_EQ(config.vm_id(), test_vm_id_);
    EXPECT_EQ(config.base_image(), test_image_type_);
    EXPECT_EQ(config.disk_mode(), DiskMode::Ephemeral);
    EXPECT_TRUE(config.packages().empty());
    EXPECT_NO_THROW(config.validate());
}

TEST_F(VMConfigurationTest, CreateDevelopment) {
    VMConfiguration config = VMConfiguration::create_development(test_vm_id_, test_image_type_);
    
    EXPECT_EQ(config.vm_id(), test_vm_id_);
    EXPECT_EQ(config.base_image(), test_image_type_);
    EXPECT_FALSE(config.packages().empty()); // Should have development packages
    EXPECT_TRUE(config.ssh_enabled());
    EXPECT_NO_THROW(config.validate());
}

TEST_F(VMConfigurationTest, CreateTesting) {
    VMConfiguration config = VMConfiguration::create_testing(test_vm_id_, test_image_type_);
    
    EXPECT_EQ(config.vm_id(), test_vm_id_);
    EXPECT_EQ(config.base_image(), test_image_type_);
    EXPECT_EQ(config.disk_mode(), DiskMode::Ephemeral); // Always ephemeral for testing
    EXPECT_NO_THROW(config.validate());
}

// Utility method tests
TEST_F(VMConfigurationTest, RecommendedMemoryForImage) {
    MemoryAmount ubuntu_mem = VMConfiguration::get_recommended_memory_for_image(ImageType::Ubuntu2204);
    MemoryAmount alpine_mem = VMConfiguration::get_recommended_memory_for_image(ImageType::Alpine317);
    
    EXPECT_GT(ubuntu_mem.bytes, 0);
    EXPECT_GT(alpine_mem.bytes, 0);
    EXPECT_GT(ubuntu_mem.bytes, alpine_mem.bytes); // Ubuntu should need more memory
}

TEST_F(VMConfigurationTest, RecommendedDiskForImage) {
    DiskSize ubuntu_disk = VMConfiguration::get_recommended_disk_for_image(ImageType::Ubuntu2204);
    DiskSize alpine_disk = VMConfiguration::get_recommended_disk_for_image(ImageType::Alpine317);
    
    EXPECT_GT(ubuntu_disk.bytes, 0);
    EXPECT_GT(alpine_disk.bytes, 0);
    EXPECT_GT(ubuntu_disk.bytes, alpine_disk.bytes); // Ubuntu should need more disk space
}

// Copy and move semantics tests
TEST_F(VMConfigurationTest, CopyConstruction) {
    VMConfiguration original(test_vm_id_, test_image_type_);
    original.add_package("git");
    original.add_environment_variable("TEST", "value");
    
    VMConfiguration copy(original);
    
    EXPECT_EQ(copy.vm_id(), original.vm_id());
    EXPECT_EQ(copy.base_image(), original.base_image());
    EXPECT_TRUE(copy.has_package("git"));
    EXPECT_EQ(copy.get_environment_variable("TEST").value(), "value");
}

TEST_F(VMConfigurationTest, MoveConstruction) {
    VMConfiguration original(test_vm_id_, test_image_type_);
    original.add_package("git");
    VMId original_id = original.vm_id();
    
    VMConfiguration moved(std::move(original));
    
    EXPECT_EQ(moved.vm_id(), original_id);
    EXPECT_TRUE(moved.has_package("git"));
}

TEST_F(VMConfigurationTest, Assignment) {
    VMConfiguration original(test_vm_id_, test_image_type_);
    original.add_package("git");
    
    VMConfiguration assigned;
    assigned = original;
    
    EXPECT_EQ(assigned.vm_id(), original.vm_id());
    EXPECT_TRUE(assigned.has_package("git"));
}