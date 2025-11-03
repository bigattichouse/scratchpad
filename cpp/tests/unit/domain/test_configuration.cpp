#include <gtest/gtest.h>
#include "config/configuration.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <fstream>
#include <filesystem>

using namespace scratchpad;
using namespace scratchpad::test;

class ConfigurationTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::make_unique<TempDirectory>();
        config_file_path_ = temp_dir_->path() / "test_config.json";
    }
    
    void create_test_config_file(const std::string& content) {
        std::ofstream file(config_file_path_);
        file << content;
    }
    
    std::unique_ptr<TempDirectory> temp_dir_;
    std::filesystem::path config_file_path_;
};

// Default configuration tests
TEST_F(ConfigurationTest, DefaultConstruction) {
    Configuration config;
    
    // Should have sensible defaults
    EXPECT_FALSE(config.get_string("vm_directory").empty());
    EXPECT_FALSE(config.get_string("images_directory").empty());
    EXPECT_GT(config.get_int("ssh_port_range_start"), 0);
    EXPECT_GT(config.get_int("ssh_port_range_end"), config.get_int("ssh_port_range_start"));
    EXPECT_FALSE(config.get_string("default_memory").empty());
    
    // Should validate by default
    EXPECT_TRUE(config.validate());
}

// File loading tests
TEST_F(ConfigurationTest, LoadFromValidJsonFile) {
    std::string valid_config = R"({
        "vm_directory": "/tmp/test_vms",
        "images_directory": "/tmp/test_images",
        "default_memory": "2G",
        "ssh_port_range_start": 3000,
        "ssh_port_range_end": 4000,
        "enable_health_monitoring": true,
        "log_level": "info"
    })";
    
    create_test_config_file(valid_config);
    
    Configuration config;
    EXPECT_NO_THROW(config.load_from_file(config_file_path_.string()));
    
    EXPECT_EQ(config.get_string("vm_directory"), "/tmp/test_vms");
    EXPECT_EQ(config.get_string("images_directory"), "/tmp/test_images");
    EXPECT_EQ(config.get_string("default_memory"), "2G");
    EXPECT_EQ(config.get_int("ssh_port_range_start"), 3000);
    EXPECT_EQ(config.get_int("ssh_port_range_end"), 4000);
    EXPECT_TRUE(config.get_bool("enable_health_monitoring"));
    EXPECT_EQ(config.get_string("log_level"), "info");
}

TEST_F(ConfigurationTest, LoadFromInvalidJsonFile) {
    std::string invalid_config = R"({
        "vm_directory": "/tmp/test_vms",
        "invalid_json": true,
        "missing_quote: "value"
    })";
    
    create_test_config_file(invalid_config);
    
    Configuration config;
    EXPECT_THROW(config.load_from_file(config_file_path_.string()), ScratchpadError);
}

TEST_F(ConfigurationTest, LoadFromNonexistentFile) {
    Configuration config;
    EXPECT_THROW(config.load_from_file("/nonexistent/config.json"), ScratchpadError);
}

// File saving tests
TEST_F(ConfigurationTest, SaveToFile) {
    Configuration config;
    
    // Modify some values
    config.set_string("vm_directory", "/custom/vm/path");
    config.set_int("ssh_port_range_start", 5000);
    config.set_bool("enable_health_monitoring", false);
    
    // Save to file
    EXPECT_NO_THROW(config.save_to_file(config_file_path_.string()));
    
    // Verify file exists and is readable
    EXPECT_TRUE(std::filesystem::exists(config_file_path_));
    
    // Load into new configuration and verify
    Configuration loaded_config;
    loaded_config.load_from_file(config_file_path_.string());
    
    EXPECT_EQ(loaded_config.get_string("vm_directory"), "/custom/vm/path");
    EXPECT_EQ(loaded_config.get_int("ssh_port_range_start"), 5000);
    EXPECT_FALSE(loaded_config.get_bool("enable_health_monitoring"));
}

// Type-safe getter tests
TEST_F(ConfigurationTest, StringGetters) {
    Configuration config;
    
    // Test default values
    EXPECT_EQ(config.get_string("nonexistent_key", "default"), "default");
    EXPECT_EQ(config.get_string("nonexistent_key"), "");
    
    // Set and get values
    config.set_string("test_string", "hello world");
    EXPECT_EQ(config.get_string("test_string"), "hello world");
    
    // Test overriding
    config.set_string("test_string", "updated value");
    EXPECT_EQ(config.get_string("test_string"), "updated value");
}

TEST_F(ConfigurationTest, IntegerGetters) {
    Configuration config;
    
    // Test default values
    EXPECT_EQ(config.get_int("nonexistent_key", 42), 42);
    EXPECT_EQ(config.get_int("nonexistent_key"), 0);
    
    // Set and get values
    config.set_int("test_int", 123);
    EXPECT_EQ(config.get_int("test_int"), 123);
    
    // Test negative values
    config.set_int("negative_int", -456);
    EXPECT_EQ(config.get_int("negative_int"), -456);
}

TEST_F(ConfigurationTest, BooleanGetters) {
    Configuration config;
    
    // Test default values
    EXPECT_TRUE(config.get_bool("nonexistent_key", true));
    EXPECT_FALSE(config.get_bool("nonexistent_key", false));
    EXPECT_FALSE(config.get_bool("nonexistent_key"));
    
    // Set and get values
    config.set_bool("test_bool_true", true);
    config.set_bool("test_bool_false", false);
    
    EXPECT_TRUE(config.get_bool("test_bool_true"));
    EXPECT_FALSE(config.get_bool("test_bool_false"));
}

TEST_F(ConfigurationTest, DoubleGetters) {
    Configuration config;
    
    // Test default values
    EXPECT_DOUBLE_EQ(config.get_double("nonexistent_key", 3.14), 3.14);
    EXPECT_DOUBLE_EQ(config.get_double("nonexistent_key"), 0.0);
    
    // Set and get values
    config.set_double("test_double", 2.718);
    EXPECT_DOUBLE_EQ(config.get_double("test_double"), 2.718);
    
    // Test precision
    config.set_double("precise_double", 1.23456789);
    EXPECT_NEAR(config.get_double("precise_double"), 1.23456789, 1e-8);
}

// Array/vector getters tests
TEST_F(ConfigurationTest, StringArrayGetters) {
    Configuration config;
    
    // Test empty default
    auto empty_array = config.get_string_array("nonexistent_key");
    EXPECT_TRUE(empty_array.empty());
    
    // Test with default
    std::vector<std::string> default_array = {"default1", "default2"};
    auto result = config.get_string_array("nonexistent_key", default_array);
    EXPECT_EQ(result, default_array);
    
    // Set and get array
    std::vector<std::string> test_array = {"item1", "item2", "item3"};
    config.set_string_array("test_array", test_array);
    
    auto retrieved = config.get_string_array("test_array");
    EXPECT_EQ(retrieved, test_array);
}

// Environment variable override tests
TEST_F(ConfigurationTest, EnvironmentOverrides) {
    ScopedEnvVar env_vm_dir("SCRATCHPAD_VM_DIRECTORY", "/env/vm/path");
    ScopedEnvVar env_memory("SCRATCHPAD_DEFAULT_MEMORY", "4G");
    ScopedEnvVar env_port_start("SCRATCHPAD_SSH_PORT_START", "6000");
    
    Configuration config;
    config.load_environment_overrides();
    
    EXPECT_EQ(config.get_string("vm_directory"), "/env/vm/path");
    EXPECT_EQ(config.get_string("default_memory"), "4G");
    EXPECT_EQ(config.get_int("ssh_port_range_start"), 6000);
}

// Validation tests
TEST_F(ConfigurationTest, ValidationSuccess) {
    Configuration config;
    
    // Set valid configuration
    config.set_string("vm_directory", "/tmp/valid_vms");
    config.set_string("images_directory", "/tmp/valid_images");
    config.set_string("default_memory", "1G");
    config.set_int("ssh_port_range_start", 2222);
    config.set_int("ssh_port_range_end", 9999);
    
    EXPECT_TRUE(config.validate());
}

TEST_F(ConfigurationTest, ValidationFailures) {
    Configuration config;
    
    // Invalid port range (start >= end)
    config.set_int("ssh_port_range_start", 5000);
    config.set_int("ssh_port_range_end", 4000);
    EXPECT_THROW(config.validate(), ScratchpadError);
    
    // Invalid memory format
    config.set_int("ssh_port_range_end", 6000); // Fix port range
    config.set_string("default_memory", "invalid_memory");
    EXPECT_THROW(config.validate(), ScratchpadError);
    
    // Invalid port numbers
    config.set_string("default_memory", "1G"); // Fix memory
    config.set_int("ssh_port_range_start", 0);
    EXPECT_THROW(config.validate(), ScratchpadError);
    
    config.set_int("ssh_port_range_start", 65536);
    EXPECT_THROW(config.validate(), ScratchpadError);
}

// Key management tests
TEST_F(ConfigurationTest, KeyExistence) {
    Configuration config;
    
    EXPECT_FALSE(config.has_key("nonexistent_key"));
    
    config.set_string("test_key", "test_value");
    EXPECT_TRUE(config.has_key("test_key"));
    
    config.remove_key("test_key");
    EXPECT_FALSE(config.has_key("test_key"));
}

TEST_F(ConfigurationTest, GetAllKeys) {
    Configuration config;
    
    // Clear default keys for clean test
    config.clear();
    
    config.set_string("key1", "value1");
    config.set_int("key2", 42);
    config.set_bool("key3", true);
    
    auto keys = config.get_all_keys();
    EXPECT_EQ(keys.size(), 3);
    EXPECT_NE(std::find(keys.begin(), keys.end(), "key1"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "key2"), keys.end());
    EXPECT_NE(std::find(keys.begin(), keys.end(), "key3"), keys.end());
}

// Nested configuration tests
TEST_F(ConfigurationTest, NestedConfiguration) {
    std::string nested_config = R"({
        "database": {
            "host": "localhost",
            "port": 5432,
            "credentials": {
                "username": "admin",
                "password": "secret"
            }
        },
        "logging": {
            "level": "debug",
            "file": "/var/log/scratchpad.log"
        }
    })";
    
    create_test_config_file(nested_config);
    
    Configuration config;
    config.load_from_file(config_file_path_.string());
    
    // Test dot notation access
    EXPECT_EQ(config.get_string("database.host"), "localhost");
    EXPECT_EQ(config.get_int("database.port"), 5432);
    EXPECT_EQ(config.get_string("database.credentials.username"), "admin");
    EXPECT_EQ(config.get_string("logging.level"), "debug");
}

// Configuration merging tests
TEST_F(ConfigurationTest, ConfigurationMerging) {
    Configuration base_config;
    base_config.set_string("vm_directory", "/base/vms");
    base_config.set_int("ssh_port_range_start", 2000);
    base_config.set_bool("enable_monitoring", true);
    
    Configuration override_config;
    override_config.set_string("vm_directory", "/override/vms");
    override_config.set_int("ssh_port_range_end", 3000);
    
    // Merge configurations
    base_config.merge(override_config);
    
    // Overridden values
    EXPECT_EQ(base_config.get_string("vm_directory"), "/override/vms");
    
    // Original values preserved
    EXPECT_EQ(base_config.get_int("ssh_port_range_start"), 2000);
    EXPECT_TRUE(base_config.get_bool("enable_monitoring"));
    
    // New values added
    EXPECT_EQ(base_config.get_int("ssh_port_range_end"), 3000);
}

// Configuration sections tests
TEST_F(ConfigurationTest, ConfigurationSections) {
    Configuration config;
    
    // Set values in different sections
    config.set_string("vm.default_memory", "2G");
    config.set_int("vm.max_instances", 10);
    config.set_string("ssh.key_directory", "/keys");
    config.set_int("ssh.timeout", 30);
    
    // Get section
    auto vm_section = config.get_section("vm");
    EXPECT_EQ(vm_section.get_string("default_memory"), "2G");
    EXPECT_EQ(vm_section.get_int("max_instances"), 10);
    
    auto ssh_section = config.get_section("ssh");
    EXPECT_EQ(ssh_section.get_string("key_directory"), "/keys");
    EXPECT_EQ(ssh_section.get_int("timeout"), 30);
}

// Type conversion tests
TEST_F(ConfigurationTest, TypeConversions) {
    Configuration config;
    
    // String to int conversion
    config.set_string("string_number", "42");
    EXPECT_EQ(config.get_int("string_number"), 42);
    
    // String to bool conversion
    config.set_string("string_bool_true", "true");
    config.set_string("string_bool_false", "false");
    EXPECT_TRUE(config.get_bool("string_bool_true"));
    EXPECT_FALSE(config.get_bool("string_bool_false"));
    
    // Invalid conversions should use defaults
    config.set_string("invalid_int", "not_a_number");
    EXPECT_EQ(config.get_int("invalid_int", 999), 999);
}

// Thread safety tests (basic)
TEST_F(ConfigurationTest, ThreadSafetyBasic) {
    Configuration config;
    
    // Test concurrent reads and writes
    std::vector<std::thread> threads;
    std::atomic<int> operation_count{0};
    
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&config, &operation_count, i]() {
            for (int j = 0; j < 10; ++j) {
                config.set_string("thread_" + std::to_string(i), "value_" + std::to_string(j));
                auto value = config.get_string("thread_" + std::to_string(i));
                operation_count++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(operation_count.load(), 50);
    
    // Verify final state
    for (int i = 0; i < 5; ++i) {
        auto value = config.get_string("thread_" + std::to_string(i));
        EXPECT_FALSE(value.empty());
    }
}