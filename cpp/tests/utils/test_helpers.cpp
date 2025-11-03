#include "test_helpers.hpp"

#include <random>
#include <fstream>
#include <chrono>
#include <thread>
#include <cstdlib>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>

namespace scratchpad::test {

// TempDirectory implementation
TempDirectory::TempDirectory() {
    path_ = std::filesystem::temp_directory_path() / ("scratchpad_test_" + TestHelpers::random_string(8));
    std::filesystem::create_directories(path_);
}

TempDirectory::~TempDirectory() {
    std::error_code ec;
    std::filesystem::remove_all(path_, ec);
}

void TempDirectory::create_file(const std::string& filename, const std::string& content) {
    std::ofstream file(path_ / filename);
    file << content;
}

std::filesystem::path TempDirectory::create_subdirectory(const std::string& dirname) {
    auto subdir = path_ / dirname;
    std::filesystem::create_directories(subdir);
    return subdir;
}

// TestHelpers implementation
std::string TestHelpers::random_string(size_t length) {
    static const std::string charset = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, charset.size() - 1);
    
    std::string result;
    result.reserve(length);
    
    for (size_t i = 0; i < length; ++i) {
        result += charset[dis(gen)];
    }
    
    return result;
}

std::string TestHelpers::random_vm_id() {
    return "test-vm-" + random_string(8);
}

std::pair<std::string, std::string> TestHelpers::create_temp_ssh_keys(const std::filesystem::path& dir) {
    auto private_key_path = dir / "test_rsa";
    auto public_key_path = dir / "test_rsa.pub";
    
    // Create minimal test SSH keys (these are not secure, only for testing)
    std::ofstream private_key(private_key_path);
    private_key << "-----BEGIN OPENSSH PRIVATE KEY-----\n"
                << "b3BlbnNzaC1rZXktdjEAAAAABG5vbmUAAAAEbm9uZQAAAAAAAAABAAAAFwAAAAdzc2gtcn\n"
                << "NhAAAAAwEAAQAAAQEA1234567890abcdefghijklmnopqrstuvwxyz\n"
                << "-----END OPENSSH PRIVATE KEY-----\n";
    
    std::ofstream public_key(public_key_path);
    public_key << "ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABAQDU1234567890abcdefghijklmnopqrstuvwxyz test@scratchpad\n";
    
    return {private_key_path.string(), public_key_path.string()};
}

bool TestHelpers::is_port_available(int port) {
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return false;
    }
    
    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(port);
    
    int result = bind(sock, (struct sockaddr*)&addr, sizeof(addr));
    close(sock);
    
    return result == 0;
}

int TestHelpers::find_available_port(int start_port, int end_port) {
    for (int port = start_port; port <= end_port; ++port) {
        if (is_port_available(port)) {
            return port;
        }
    }
    return -1;
}

// ScopedEnvVar implementation
ScopedEnvVar::ScopedEnvVar(const std::string& name, const std::string& value)
    : name_(name), had_value_(false) {
    
    const char* old_val = std::getenv(name.c_str());
    if (old_val) {
        old_value_ = old_val;
        had_value_ = true;
    }
    
    setenv(name_.c_str(), value.c_str(), 1);
}

ScopedEnvVar::~ScopedEnvVar() {
    if (had_value_) {
        setenv(name_.c_str(), old_value_.c_str(), 1);
    } else {
        unsetenv(name_.c_str());
    }
}

// VMTestFixture implementation
VMTestFixture::VMTestFixture() {
    temp_dir_ = std::make_unique<TempDirectory>();
    vm_dir_ = temp_dir_->create_subdirectory("vms");
    ssh_keys_dir_ = temp_dir_->create_subdirectory("ssh_keys");
    test_vm_id_ = TestHelpers::random_vm_id();
    
    // Create test SSH keys
    TestHelpers::create_temp_ssh_keys(ssh_keys_dir_);
}

VMTestFixture::~VMTestFixture() = default;

} // namespace scratchpad::test