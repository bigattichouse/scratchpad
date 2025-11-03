#include "scratchpad/types.hpp"
#include "scratchpad/errors.hpp"
#include <regex>
#include <algorithm>
#include <cctype>

namespace scratchpad {

// MemoryAmount implementation

MemoryAmount MemoryAmount::from_string(const std::string& str) {
    if (str.empty()) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument, "Memory string cannot be empty", "memory");
    }
    
    std::string upper_str = str;
    std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);
    
    // Parse number and unit
    std::regex memory_regex(R"(^(\d+(?:\.\d+)?)\s*([KMGT]?B?)$)");
    std::smatch match;
    
    if (!std::regex_match(upper_str, match, memory_regex)) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument, 
                           "Invalid memory format: " + str + " (expected format: 512M, 2G, etc.)",
                           "memory");
    }
    
    double value = std::stod(match[1].str());
    std::string unit = match[2].str();
    
    if (unit.empty() || unit == "B") {
        return MemoryAmount{static_cast<uint64_t>(value)};
    } else if (unit == "K" || unit == "KB") {
        return MemoryAmount{static_cast<uint64_t>(value * 1024)};
    } else if (unit == "M" || unit == "MB") {
        return MemoryAmount{static_cast<uint64_t>(value * 1024 * 1024)};
    } else if (unit == "G" || unit == "GB") {
        return MemoryAmount{static_cast<uint64_t>(value * 1024 * 1024 * 1024)};
    } else if (unit == "T" || unit == "TB") {
        return MemoryAmount{static_cast<uint64_t>(value * 1024ULL * 1024 * 1024 * 1024)};
    }
    
    THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument, 
                       "Unknown memory unit: " + unit, "memory");
}

std::string MemoryAmount::to_string() const {
    if (bytes == 0) {
        return "0B";
    }
    
    const uint64_t KB = 1024;
    const uint64_t MB = KB * 1024;
    const uint64_t GB = MB * 1024;
    const uint64_t TB = GB * 1024;
    
    if (bytes >= TB && bytes % TB == 0) {
        return std::to_string(bytes / TB) + "T";
    } else if (bytes >= GB && bytes % GB == 0) {
        return std::to_string(bytes / GB) + "G";
    } else if (bytes >= MB && bytes % MB == 0) {
        return std::to_string(bytes / MB) + "M";
    } else if (bytes >= KB && bytes % KB == 0) {
        return std::to_string(bytes / KB) + "K";
    } else {
        return std::to_string(bytes) + "B";
    }
}

// DiskSize implementation

DiskSize DiskSize::from_string(const std::string& str) {
    if (str.empty()) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument, "Disk size string cannot be empty", "disk");
    }
    
    std::string upper_str = str;
    std::transform(upper_str.begin(), upper_str.end(), upper_str.begin(), ::toupper);
    
    // Parse number and unit
    std::regex disk_regex(R"(^(\d+(?:\.\d+)?)\s*([KMGT]?B?)$)");
    std::smatch match;
    
    if (!std::regex_match(upper_str, match, disk_regex)) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Invalid disk size format: " + str + " (expected format: 10G, 500M, etc.)",
                           "disk");
    }
    
    double value = std::stod(match[1].str());
    std::string unit = match[2].str();
    
    if (unit.empty() || unit == "B") {
        return DiskSize{static_cast<uint64_t>(value)};
    } else if (unit == "K" || unit == "KB") {
        return DiskSize{static_cast<uint64_t>(value * 1024)};
    } else if (unit == "M" || unit == "MB") {
        return DiskSize{static_cast<uint64_t>(value * 1024 * 1024)};
    } else if (unit == "G" || unit == "GB") {
        return DiskSize{static_cast<uint64_t>(value * 1024 * 1024 * 1024)};
    } else if (unit == "T" || unit == "TB") {
        return DiskSize{static_cast<uint64_t>(value * 1024ULL * 1024 * 1024 * 1024)};
    }
    
    THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                       "Unknown disk size unit: " + unit, "disk");
}

std::string DiskSize::to_string() const {
    if (bytes == 0) {
        return "0B";
    }
    
    const uint64_t KB = 1024;
    const uint64_t MB = KB * 1024;
    const uint64_t GB = MB * 1024;
    const uint64_t TB = GB * 1024;
    
    if (bytes >= TB && bytes % TB == 0) {
        return std::to_string(bytes / TB) + "T";
    } else if (bytes >= GB && bytes % GB == 0) {
        return std::to_string(bytes / GB) + "G";
    } else if (bytes >= MB && bytes % MB == 0) {
        return std::to_string(bytes / MB) + "M";
    } else if (bytes >= KB && bytes % KB == 0) {
        return std::to_string(bytes / KB) + "K";
    } else {
        return std::to_string(bytes) + "B";
    }
}

// ExecutionContext implementation

ExecutionContext ExecutionContext::create_default() {
    ExecutionContext context;
    context.timeout = std::chrono::milliseconds(300000); // 5 minutes
    context.capture_output = true;
    return context;
}

ExecutionContext ExecutionContext::create_with_timeout(std::chrono::milliseconds timeout) {
    auto context = create_default();
    context.timeout = timeout;
    return context;
}

ExecutionContext ExecutionContext::create_with_directory(const std::string& working_dir) {
    auto context = create_default();
    context.working_directory = working_dir;
    return context;
}

ExecutionContext ExecutionContext::create_with_environment(
    const std::map<std::string, std::string>& env_vars) {
    auto context = create_default();
    context.environment_vars = env_vars;
    return context;
}

bool ExecutionContext::validate() const {
    // Check timeout is positive
    if (timeout <= std::chrono::milliseconds::zero()) {
        return false;
    }
    
    // Check working directory is absolute if specified
    if (working_directory.has_value()) {
        const auto& dir = working_directory.value();
        if (dir.empty() || dir[0] != '/') {
            return false;
        }
    }
    
    // Check environment variable names are not empty
    for (const auto& [name, value] : environment_vars) {
        if (name.empty()) {
            return false;
        }
    }
    
    return true;
}

void ExecutionContext::add_environment_variable(const std::string& name, const std::string& value) {
    if (!name.empty()) {
        environment_vars[name] = value;
    }
}

void ExecutionContext::remove_environment_variable(const std::string& name) {
    environment_vars.erase(name);
}

std::optional<std::string> ExecutionContext::get_environment_variable(const std::string& name) const {
    auto it = environment_vars.find(name);
    if (it != environment_vars.end()) {
        return it->second;
    }
    return {};
}

// SSHCredentials implementation

SSHCredentials SSHCredentials::create_with_key_files(
    const std::string& username,
    const std::string& private_key_path,
    const std::string& public_key_path,
    PortNumber port,
    std::chrono::milliseconds timeout) {
    
    SSHCredentials creds;
    creds.username = username;
    creds.private_key_path = private_key_path;
    creds.public_key_path = public_key_path;
    creds.port = port;
    creds.timeout = timeout;
    
    return creds;
}

SSHCredentials SSHCredentials::create_default(
    const std::string& username,
    PortNumber port) {
    
    // Use default SSH key paths
    const char* home = std::getenv("HOME");
    std::string home_dir = home ? home : "/tmp";
    
    return create_with_key_files(
        username,
        home_dir + "/.scratchpad/keys/id_rsa",
        home_dir + "/.scratchpad/keys/id_rsa.pub",
        port,
        std::chrono::milliseconds(30000)
    );
}

bool SSHCredentials::validate() const {
    return !username.empty() && 
           !private_key_path.empty() && 
           port > 0 && port <= 65535 && 
           timeout > std::chrono::milliseconds::zero();
}

} // namespace scratchpad