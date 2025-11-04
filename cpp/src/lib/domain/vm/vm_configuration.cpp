#include "scratchpad/domain/vm/vm_configuration.hpp"
#include "scratchpad/errors.hpp"
#include <algorithm>

namespace scratchpad {

VMConfiguration::VMConfiguration()
    : memory_(MemoryAmount::megabytes(512))
    , disk_size_(DiskSize::gigabytes(10))
    , disk_mode_(DiskMode::Ephemeral)
    , base_image_(ImageType::Ubuntu)
    , cpu_cores_(2)
    , enable_acceleration_(true)
    , enable_networking_(true)
    , enable_ssh_(true)
    , enable_vnc_(false) {
    
    // Set default network configuration
    network_config_.ssh_port = 0; // Will be allocated automatically
    network_config_.enable_port_forwarding = true;
}

VMConfiguration::VMConfiguration(const VMId& vm_id, ImageType base_image)
    : VMConfiguration() {
    vm_id_ = vm_id;
    base_image_ = base_image;
}

void VMConfiguration::set_memory(const MemoryAmount& memory) {
    if (memory.bytes == 0) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument, 
                      "Memory allocation cannot be zero", vm_id_.value());
    }
    
    // Minimum 64MB, maximum 64GB for safety
    static const uint64_t MIN_MEMORY = 64ULL * 1024 * 1024;
    static const uint64_t MAX_MEMORY = 64ULL * 1024 * 1024 * 1024;
    
    if (memory.bytes < MIN_MEMORY) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Memory allocation must be at least 64MB", vm_id_.value());
    }
    
    if (memory.bytes > MAX_MEMORY) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Memory allocation cannot exceed 64GB", vm_id_.value());
    }
    
    memory_ = memory;
}

void VMConfiguration::set_disk_size(const DiskSize& disk_size) {
    if (disk_size.bytes == 0) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Disk size cannot be zero", vm_id_.value());
    }
    
    // Minimum 1GB, maximum 1TB for safety
    static const uint64_t MIN_DISK = 1ULL * 1024 * 1024 * 1024;
    static const uint64_t MAX_DISK = 1024ULL * 1024 * 1024 * 1024;
    
    if (disk_size.bytes < MIN_DISK) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Disk size must be at least 1GB", vm_id_.value());
    }
    
    if (disk_size.bytes > MAX_DISK) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Disk size cannot exceed 1TB", vm_id_.value());
    }
    
    disk_size_ = disk_size;
}

void VMConfiguration::set_cpu_cores(uint32_t cores) {
    if (cores == 0) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "CPU cores cannot be zero", vm_id_.value());
    }
    
    if (cores > 32) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "CPU cores cannot exceed 32", vm_id_.value());
    }
    
    cpu_cores_ = cores;
}

void VMConfiguration::set_network_configuration(const NetworkConfiguration& config) {
    // Validate port numbers if specified
    if (config.ssh_port != 0) {
        if (config.ssh_port < 1024 || config.ssh_port > 65535) {
            THROW_VM_ERROR(ErrorCode::InvalidArgument,
                          "SSH port must be between 1024 and 65535", vm_id_.value());
        }
    }
    
    if (config.vnc_port.has_value()) {
        auto vnc_port = config.vnc_port.value();
        if (vnc_port < 5900 || vnc_port > 5999) {
            THROW_VM_ERROR(ErrorCode::InvalidArgument,
                          "VNC port must be between 5900 and 5999", vm_id_.value());
        }
    }
    
    network_config_ = config;
}

void VMConfiguration::add_package(const std::string& package) {
    if (package.empty()) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Package name cannot be empty", vm_id_.value());
    }
    
    // Check if package already exists
    auto it = std::find(packages_.begin(), packages_.end(), package);
    if (it == packages_.end()) {
        packages_.push_back(package);
    }
}

void VMConfiguration::remove_package(const std::string& package) {
    packages_.erase(
        std::remove(packages_.begin(), packages_.end(), package),
        packages_.end()
    );
}

void VMConfiguration::set_packages(const PackageList& packages) {
    // Validate all package names
    for (const auto& package : packages) {
        if (package.empty()) {
            THROW_VM_ERROR(ErrorCode::InvalidArgument,
                          "Package name cannot be empty", vm_id_.value());
        }
    }
    
    packages_ = packages;
}

void VMConfiguration::add_environment_variable(const std::string& name, const std::string& value) {
    if (name.empty()) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Environment variable name cannot be empty", vm_id_.value());
    }
    
    environment_variables_[name] = value;
}

void VMConfiguration::remove_environment_variable(const std::string& name) {
    environment_variables_.erase(name);
}

void VMConfiguration::add_custom_command(const std::string& command) {
    if (command.empty()) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Custom command cannot be empty", vm_id_.value());
    }
    
    custom_commands_.push_back(command);
}

void VMConfiguration::set_work_directory(const std::string& directory) {
    if (!directory.empty()) {
        // Basic validation - should be absolute path
        if (directory[0] != '/') {
            THROW_VM_ERROR(ErrorCode::InvalidArgument,
                          "Work directory must be an absolute path", vm_id_.value());
        }
    }
    
    work_directory_ = directory;
}

bool VMConfiguration::validate() const {
    std::vector<std::string> errors;
    
    // Validate VM ID
    if (vm_id_.empty()) {
        errors.push_back("VM ID is required");
    }
    
    // Validate memory (already validated in setter, but double-check)
    if (memory_.bytes == 0) {
        errors.push_back("Memory allocation must be greater than zero");
    }
    
    // Validate disk size
    if (disk_size_.bytes == 0) {
        errors.push_back("Disk size must be greater than zero");
    }
    
    // Validate CPU cores
    if (cpu_cores_ == 0) {
        errors.push_back("CPU cores must be greater than zero");
    }
    
    // Validate network configuration
    if (enable_ssh_ && enable_networking_) {
        if (network_config_.ssh_port != 0) {
            if (network_config_.ssh_port < 1024 || network_config_.ssh_port > 65535) {
                errors.push_back("SSH port must be between 1024 and 65535");
            }
        }
    }
    
    if (enable_vnc_ && network_config_.vnc_port.has_value()) {
        auto vnc_port = network_config_.vnc_port.value();
        if (vnc_port < 5900 || vnc_port > 5999) {
            errors.push_back("VNC port must be between 5900 and 5999");
        }
    }
    
    // Validate packages
    for (const auto& package : packages_) {
        if (package.empty()) {
            errors.push_back("Package names cannot be empty");
            break;
        }
    }
    
    // Validate environment variables
    for (const auto& [name, value] : environment_variables_) {
        if (name.empty()) {
            errors.push_back("Environment variable names cannot be empty");
            break;
        }
    }
    
    // Validate work directory
    if (!work_directory_.empty()) {
        if (work_directory_[0] != '/') {
            errors.push_back("Work directory must be an absolute path");
        }
    }
    
    if (!errors.empty()) {
        std::string combined_errors;
        for (const auto& error : errors) {
            if (!combined_errors.empty()) combined_errors += "; ";
            combined_errors += error;
        }
        THROW_VM_ERROR(ErrorCode::ConfigurationError,
                      "VM configuration validation failed: " + combined_errors,
                      vm_id_.value());
    }
    
    return true;
}

VMConfiguration VMConfiguration::create_minimal(const VMId& vm_id, ImageType base_image) {
    VMConfiguration config(vm_id, base_image);
    config.set_memory(MemoryAmount::megabytes(256));
    config.set_disk_size(DiskSize::gigabytes(5));
    config.set_cpu_cores(1);
    config.set_disk_mode(DiskMode::Ephemeral);
    return config;
}

VMConfiguration VMConfiguration::create_development(const VMId& vm_id, ImageType base_image) {
    VMConfiguration config(vm_id, base_image);
    config.set_memory(MemoryAmount::gigabytes(2));
    config.set_disk_size(DiskSize::gigabytes(20));
    config.set_cpu_cores(4);
    config.set_disk_mode(DiskMode::Persistent);
    
    // Add common development packages
    if (base_image == ImageType::Ubuntu) {
        config.add_package("curl");
        config.add_package("wget");
        config.add_package("git");
        config.add_package("vim");
        config.add_package("build-essential");
    } else if (base_image == ImageType::Alpine) {
        config.add_package("curl");
        config.add_package("wget");
        config.add_package("git");
        config.add_package("vim");
        config.add_package("build-base");
    }
    
    return config;
}

VMConfiguration VMConfiguration::create_testing(const VMId& vm_id, ImageType base_image) {
    VMConfiguration config(vm_id, base_image);
    config.set_memory(MemoryAmount::megabytes(512));
    config.set_disk_size(DiskSize::gigabytes(10));
    config.set_cpu_cores(2);
    config.set_disk_mode(DiskMode::Ephemeral); // Always ephemeral for testing
    
    // Add testing-specific environment
    config.add_environment_variable("NODE_ENV", "test");
    config.add_environment_variable("PYTHONPATH", "/opt/test");
    
    return config;
}

MemoryAmount VMConfiguration::get_recommended_memory_for_image(ImageType image_type) {
    switch (image_type) {
        case ImageType::Alpine:
            return MemoryAmount::megabytes(256); // Alpine is very lightweight
        case ImageType::Ubuntu:
            return MemoryAmount::megabytes(512); // Ubuntu needs more memory
        case ImageType::Debian:
            return MemoryAmount::megabytes(512); // Similar to Ubuntu
        default:
            return MemoryAmount::megabytes(512); // Safe default
    }
}

DiskSize VMConfiguration::get_recommended_disk_for_image(ImageType image_type) {
    switch (image_type) {
        case ImageType::Alpine:
            return DiskSize::gigabytes(5); // Alpine is compact
        case ImageType::Ubuntu:
            return DiskSize::gigabytes(10); // Ubuntu needs more space
        case ImageType::Debian:
            return DiskSize::gigabytes(10); // Similar to Ubuntu
        default:
            return DiskSize::gigabytes(10); // Safe default
    }
}

} // namespace scratchpad