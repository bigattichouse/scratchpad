#pragma once

#include "scratchpad/domain/vm/vm_id.hpp"
#include "scratchpad/types.hpp"
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace scratchpad {

/**
 * Value object representing VM configuration
 * 
 * Contains all parameters needed to configure and create a virtual machine,
 * including resource allocation, base image, packages, and networking settings.
 */
class VMConfiguration {
public:
    /**
     * Default constructor with sensible defaults
     */
    VMConfiguration();

    /**
     * Constructor with VM ID and base image
     * @param vm_id Virtual machine identifier
     * @param base_image Base image type to use
     */
    VMConfiguration(const VMId& vm_id, ImageType base_image);

    // Copy and move semantics
    VMConfiguration(const VMConfiguration&) = default;
    VMConfiguration(VMConfiguration&&) = default;
    VMConfiguration& operator=(const VMConfiguration&) = default;
    VMConfiguration& operator=(VMConfiguration&&) = default;

    // ========== Basic Properties ==========

    /**
     * Get VM identifier
     * @return VM ID
     */
    const VMId& vm_id() const { return vm_id_; }

    /**
     * Set VM identifier
     * @param vm_id VM identifier
     */
    void set_vm_id(const VMId& vm_id) { vm_id_ = vm_id; }

    /**
     * Get base image type
     * @return Base image type
     */
    ImageType base_image() const { return base_image_; }

    /**
     * Set base image type
     * @param base_image Base image type
     */
    void set_base_image(ImageType base_image) { base_image_ = base_image; }

    // ========== Resource Configuration ==========

    /**
     * Get memory allocation
     * @return Memory amount
     */
    const MemoryAmount& memory() const { return memory_; }

    /**
     * Set memory allocation (validates range)
     * @param memory Memory amount (64MB - 64GB)
     * @throws VMError if invalid amount
     */
    void set_memory(const MemoryAmount& memory);

    /**
     * Get disk size
     * @return Disk size
     */
    const DiskSize& disk_size() const { return disk_size_; }

    /**
     * Set disk size (validates range)
     * @param disk_size Disk size (1GB - 1TB)
     * @throws VMError if invalid size
     */
    void set_disk_size(const DiskSize& disk_size);

    /**
     * Get CPU core count
     * @return Number of CPU cores
     */
    uint32_t cpu_cores() const { return cpu_cores_; }

    /**
     * Set CPU core count (validates range)
     * @param cores Number of CPU cores (1-32)
     * @throws VMError if invalid count
     */
    void set_cpu_cores(uint32_t cores);

    /**
     * Get disk mode
     * @return Disk mode (ephemeral or persistent)
     */
    DiskMode disk_mode() const { return disk_mode_; }

    /**
     * Set disk mode
     * @param mode Disk mode
     */
    void set_disk_mode(DiskMode mode) { disk_mode_ = mode; }

    // ========== Feature Flags ==========

    /**
     * Check if hardware acceleration is enabled
     * @return true if acceleration enabled
     */
    bool acceleration_enabled() const { return enable_acceleration_; }

    /**
     * Enable/disable hardware acceleration
     * @param enabled Enable acceleration
     */
    void set_acceleration_enabled(bool enabled) { enable_acceleration_ = enabled; }

    /**
     * Check if networking is enabled
     * @return true if networking enabled
     */
    bool networking_enabled() const { return enable_networking_; }

    /**
     * Enable/disable networking
     * @param enabled Enable networking
     */
    void set_networking_enabled(bool enabled) { enable_networking_ = enabled; }

    /**
     * Check if SSH is enabled
     * @return true if SSH enabled
     */
    bool ssh_enabled() const { return enable_ssh_; }

    /**
     * Enable/disable SSH
     * @param enabled Enable SSH
     */
    void set_ssh_enabled(bool enabled) { enable_ssh_ = enabled; }

    /**
     * Check if VNC is enabled
     * @return true if VNC enabled
     */
    bool vnc_enabled() const { return enable_vnc_; }

    /**
     * Enable/disable VNC
     * @param enabled Enable VNC
     */
    void set_vnc_enabled(bool enabled) { enable_vnc_ = enabled; }

    // ========== Network Configuration ==========

    /**
     * Get network configuration
     * @return Network configuration
     */
    const NetworkConfiguration& network_config() const { return network_config_; }

    /**
     * Set network configuration (validates ports)
     * @param config Network configuration
     * @throws VMError if invalid configuration
     */
    void set_network_configuration(const NetworkConfiguration& config);

    // ========== Package Management ==========

    /**
     * Get list of packages to install
     * @return Package list
     */
    const PackageList& packages() const { return packages_; }

    /**
     * Add package to install list
     * @param package Package name
     * @throws VMError if package name is empty
     */
    void add_package(const std::string& package);

    /**
     * Remove package from install list
     * @param package Package name
     */
    void remove_package(const std::string& package);

    /**
     * Set complete package list
     * @param packages List of packages
     * @throws VMError if any package name is empty
     */
    void set_packages(const PackageList& packages);

    /**
     * Check if package is in install list
     * @param package Package name
     * @return true if package will be installed
     */
    bool has_package(const std::string& package) const {
        return std::find(packages_.begin(), packages_.end(), package) != packages_.end();
    }

    // ========== Environment Variables ==========

    /**
     * Get environment variables
     * @return Environment variables map
     */
    const std::map<std::string, std::string>& environment_variables() const {
        return environment_variables_;
    }

    /**
     * Add environment variable
     * @param name Variable name
     * @param value Variable value
     * @throws VMError if name is empty
     */
    void add_environment_variable(const std::string& name, const std::string& value);

    /**
     * Remove environment variable
     * @param name Variable name
     */
    void remove_environment_variable(const std::string& name);

    /**
     * Get environment variable value
     * @param name Variable name
     * @return Variable value if exists
     */
    std::optional<std::string> get_environment_variable(const std::string& name) const {
        auto it = environment_variables_.find(name);
        if (it != environment_variables_.end()) {
            return it->second;
        }
        return {};
    }

    // ========== Custom Commands ==========

    /**
     * Get custom commands to run during setup
     * @return List of commands
     */
    const std::vector<std::string>& custom_commands() const { return custom_commands_; }

    /**
     * Add custom command to run during setup
     * @param command Command to execute
     * @throws VMError if command is empty
     */
    void add_custom_command(const std::string& command);

    /**
     * Set all custom commands
     * @param commands List of commands
     */
    void set_custom_commands(const std::vector<std::string>& commands) {
        custom_commands_ = commands;
    }

    /**
     * Clear all custom commands
     */
    void clear_custom_commands() { custom_commands_.clear(); }

    // ========== Work Directory ==========

    /**
     * Get work directory to mount
     * @return Work directory path (empty if none)
     */
    const std::string& work_directory() const { return work_directory_; }

    /**
     * Set work directory to mount in VM
     * @param directory Absolute path to directory
     * @throws VMError if path is not absolute
     */
    void set_work_directory(const std::string& directory);

    /**
     * Check if work directory is configured
     * @return true if work directory is set
     */
    bool has_work_directory() const { return !work_directory_.empty(); }

    // ========== Validation ==========

    /**
     * Validate the entire configuration
     * @return true if valid
     * @throws VMError if configuration is invalid
     */
    bool validate() const;

    /**
     * Check if configuration is suitable for the given resource limits
     * @param limits Resource limits to check against
     * @return true if configuration fits within limits
     */
    bool fits_within_limits(const ResourceLimits& limits) const {
        return memory_.bytes <= limits.max_memory.bytes &&
               disk_size_.bytes <= limits.max_disk_size.bytes &&
               cpu_cores_ <= limits.max_cpu_cores;
    }

    // ========== Factory Methods ==========

    /**
     * Create minimal configuration for testing/lightweight use
     * @param vm_id VM identifier
     * @param base_image Base image type
     * @return Minimal configuration
     */
    static VMConfiguration create_minimal(const VMId& vm_id, ImageType base_image);

    /**
     * Create development configuration with common tools
     * @param vm_id VM identifier
     * @param base_image Base image type
     * @return Development configuration
     */
    static VMConfiguration create_development(const VMId& vm_id, ImageType base_image);

    /**
     * Create testing configuration (always ephemeral)
     * @param vm_id VM identifier
     * @param base_image Base image type
     * @return Testing configuration
     */
    static VMConfiguration create_testing(const VMId& vm_id, ImageType base_image);

    /**
     * Create Linux configuration with custom specifications
     * @param image_name Base image name/type
     * @param memory Memory allocation
     * @param disk_size Disk size allocation
     * @param cpu_cores Number of CPU cores
     * @return Linux configuration
     */
    static VMConfiguration create_for_linux(const std::string& image_name, 
                                           const MemoryAmount& memory,
                                           const DiskSize& disk_size,
                                           uint32_t cpu_cores);

    // ========== Utility Methods ==========

    /**
     * Get recommended memory amount for image type
     * @param image_type Image type
     * @return Recommended memory amount
     */
    static MemoryAmount get_recommended_memory_for_image(ImageType image_type);

    /**
     * Get recommended disk size for image type
     * @param image_type Image type
     * @return Recommended disk size
     */
    static DiskSize get_recommended_disk_for_image(ImageType image_type);

private:
    // Basic properties
    VMId vm_id_;
    ImageType base_image_;

    // Resource configuration
    MemoryAmount memory_;
    DiskSize disk_size_;
    uint32_t cpu_cores_;
    DiskMode disk_mode_;

    // Feature flags
    bool enable_acceleration_;
    bool enable_networking_;
    bool enable_ssh_;
    bool enable_vnc_;

    // Network configuration
    NetworkConfiguration network_config_;

    // Setup configuration
    PackageList packages_;
    std::map<std::string, std::string> environment_variables_;
    std::vector<std::string> custom_commands_;
    std::string work_directory_;
};

} // namespace scratchpad