#pragma once

#include <string>
#include <filesystem>
#include <memory>
#include <vector>
#include <chrono>
#include <thread>
#include <set>
#include <unordered_map>

namespace scratchpad::test {

class TempDirectory {
public:
    TempDirectory();
    ~TempDirectory();
    
    std::filesystem::path path() const { return path_; }
    std::string string() const { return path_.string(); }
    
    // Create a file in the temp directory
    void create_file(const std::string& filename, const std::string& content = "");
    
    // Create a subdirectory
    std::filesystem::path create_subdirectory(const std::string& dirname);

private:
    std::filesystem::path path_;
};

// Test helpers for common operations
class TestHelpers {
public:
    // Generate a random string for testing
    static std::string random_string(size_t length = 10);
    
    // Generate a random VM ID
    static std::string random_vm_id();
    
    // Create a temporary SSH key pair for testing
    static std::pair<std::string, std::string> create_temp_ssh_keys(const std::filesystem::path& dir);
    
    // Wait for a condition with timeout
    template<typename Predicate>
    static bool wait_for(Predicate pred, std::chrono::milliseconds timeout = std::chrono::milliseconds{5000}) {
        const auto start = std::chrono::steady_clock::now();
        while (std::chrono::steady_clock::now() - start < timeout) {
            if (pred()) {
                return true;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds{100});
        }
        return false;
    }
    
    // Check if a port is available
    static bool is_port_available(int port);
    
    // Find an available port in a range
    static int find_available_port(int start_port = 2222, int end_port = 9999);
    
    // Get test configuration from environment variables
    static std::string get_test_temp_dir();
    static int get_test_ssh_port_start();
    static int get_test_timeout_ms();
};

// RAII helper for environment variables
class ScopedEnvVar {
public:
    ScopedEnvVar(const std::string& name, const std::string& value);
    ~ScopedEnvVar();
    
private:
    std::string name_;
    std::string old_value_;
    bool had_value_;
};

// Test fixture for VM-related tests
class VMTestFixture {
public:
    VMTestFixture();
    virtual ~VMTestFixture();
    
protected:
    std::unique_ptr<TempDirectory> temp_dir_;
    std::filesystem::path vm_dir_;
    std::filesystem::path ssh_keys_dir_;
    std::string test_vm_id_;
};

} // namespace scratchpad::test