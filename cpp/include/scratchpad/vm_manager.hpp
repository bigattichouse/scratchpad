#pragma once

#include "types.hpp"
#include "errors.hpp"
#include "domain/vm/vm_id.hpp"
#include "domain/vm/vm_configuration.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <future>
#include <map>
#include <optional>
#include <chrono>

namespace scratchpad {

// VM information structure for API responses
struct VMInfo {
    VMId vm_id;
    VMStatus status;
    VMConfiguration configuration;
    ResourceUsage statistics;
    std::chrono::system_clock::time_point created_at;
    std::optional<std::chrono::system_clock::time_point> started_at;
};

// Forward declarations
class VMLifecycleService;
class VMHealthService;
class SSHService;
class ResourceManager;

/**
 * Main interface for virtual machine management
 * 
 * This class provides the primary API for creating, managing, and interacting
 * with virtual machines. It encapsulates all VM lifecycle operations and
 * provides both synchronous and asynchronous interfaces.
 */
class VMManager {
public:
    /**
     * Configuration options for VMManager
     */
    struct Options {
        std::string vm_directory = "";
        std::string ssh_keys_directory = "";
        ResourceLimits default_limits{};
        bool enable_health_monitoring = true;
        std::chrono::milliseconds health_check_interval{30000};
        
        // Default constructor
        Options() = default;
    };

    /**
     * VM creation parameters
     */
    struct CreateParams {
        VMId vm_id;
        ImageType base_image;
        std::string image_name; // For compatibility with implementation
        MemoryAmount memory = MemoryAmount::megabytes(512);
        DiskSize disk_size = DiskSize::gigabytes(10);
        uint32_t cpu_cores = 1; // For compatibility with implementation
        DiskMode disk_mode = DiskMode::Ephemeral;
        std::optional<std::string> work_directory;
        NetworkConfiguration network_config{};
        bool auto_start = false;
    };

    /**
     * VM execution parameters for commands
     */
    struct ExecuteParams {
        std::string command;
        std::optional<std::string> working_directory;
        std::chrono::milliseconds timeout{300000}; // 5 minutes default
        bool capture_output = true;
        std::map<std::string, std::string> environment_vars;
    };

    // Status callback type for async operations
    using StatusCallback = std::function<void(const VMId&, VMStatus, const std::string&)>;

public:
    /**
     * Construct VMManager with default options
     */
    VMManager();
    
    /**
     * Construct VMManager with specified options
     */
    explicit VMManager(const Options& options);
    
    /**
     * Destructor - ensures clean shutdown of all VMs
     */
    virtual ~VMManager();

    // Non-copyable, movable
    VMManager(const VMManager&) = delete;
    VMManager& operator=(const VMManager&) = delete;
    VMManager(VMManager&&) noexcept;
    VMManager& operator=(VMManager&&) noexcept;

    // ========== VM Lifecycle Operations ==========

    /**
     * Create a new virtual machine
     * @param params VM creation parameters
     * @return VM identifier
     * @throws VMError if creation fails
     */
    virtual VMId create_vm(const CreateParams& params);

    /**
     * Start an existing virtual machine
     * @param vm_id VM identifier
     * @throws VMError if VM doesn't exist or start fails
     */
    virtual void start_vm(const VMId& vm_id);

    /**
     * Start VM asynchronously
     * @param vm_id VM identifier
     * @param callback Status update callback (optional)
     * @return Future that completes when VM is started
     */
    std::future<void> start_vm_async(const VMId& vm_id, StatusCallback callback = {});

    /**
     * Stop a running virtual machine
     * @param vm_id VM identifier
     * @param force Force shutdown without graceful stop
     * @throws VMError if VM doesn't exist or stop fails
     */
    virtual void stop_vm(const VMId& vm_id, bool force = false);
    
    /**
     * Stop a running virtual machine (implementation compatibility)
     * @param vm_id VM identifier
     * @throws VMError if VM doesn't exist or stop fails
     */
    virtual void stop_vm(const VMId& vm_id);

    /**
     * Stop VM asynchronously
     * @param vm_id VM identifier
     * @param force Force shutdown without graceful stop
     * @param callback Status update callback (optional)
     * @return Future that completes when VM is stopped
     */
    std::future<void> stop_vm_async(const VMId& vm_id, bool force = false, StatusCallback callback = {});

    /**
     * Destroy a virtual machine (delete all data)
     * @param vm_id VM identifier
     * @throws VMError if VM doesn't exist or is still running
     */
    virtual void destroy_vm(const VMId& vm_id);

    // ========== VM Communication ==========

    /**
     * Execute a command in the virtual machine
     * @param vm_id VM identifier
     * @param params Execution parameters
     * @return Command execution result
     * @throws VMError if VM is not running
     * @throws SSHError if communication fails
     */
    virtual CommandResult execute_command(const VMId& vm_id, const ExecuteParams& params);

    /**
     * Execute command asynchronously
     * @param vm_id VM identifier
     * @param params Execution parameters
     * @return Future containing command result
     */
    virtual std::future<CommandResult> execute_command_async(const VMId& vm_id, const ExecuteParams& params);

    /**
     * Get SSH connection for interactive use
     * @param vm_id VM identifier
     * @return SSH connection handle
     * @throws VMError if VM is not running
     * @throws SSHError if connection fails
     */
    std::unique_ptr<SSHConnection> connect_ssh(const VMId& vm_id);

    // ========== File Operations ==========

    /**
     * Copy file from host to VM
     * @param vm_id VM identifier
     * @param params Copy parameters (source, destination, options)
     * @throws VMError if VM is not running
     * @throws SSHError if copy fails
     */
    virtual void copy_file_to_vm(const VMId& vm_id, const CopyParams& params);

    /**
     * Copy file from VM to host
     * @param vm_id VM identifier
     * @param params Copy parameters (source, destination, options)
     * @throws VMError if VM is not running
     * @throws SSHError if copy fails
     */
    virtual void copy_file_from_vm(const VMId& vm_id, const CopyParams& params);

    // ========== VM Information and Management ==========

    /**
     * List all virtual machines
     * @param status_filter Optional status filter
     * @return Vector of VM information
     */
    virtual std::vector<VirtualMachine> list_vms(std::optional<VMStatus> status_filter = {}) const;

    /**
     * List all VM IDs (implementation compatibility)
     * @return Vector of VM identifiers
     */
    virtual std::vector<VMId> list_vms() const;

    /**
     * Get detailed information about a specific VM
     * @param vm_id VM identifier
     * @return VM information
     * @throws VMError if VM doesn't exist
     */
    virtual VirtualMachine get_vm(const VMId& vm_id) const;

    /**
     * Get VM information structure (implementation compatibility)
     * @param vm_id VM identifier
     * @return VM information structure
     * @throws VMError if VM doesn't exist
     */
    virtual VMInfo get_vm_info(const VMId& vm_id) const;

    /**
     * List all VM information structures (implementation compatibility)
     * @return Vector of VM information structures
     */
    virtual std::vector<VMInfo> list_vm_info() const;

    /**
     * Check if VM exists
     * @param vm_id VM identifier
     * @return true if VM exists
     */
    bool vm_exists(const VMId& vm_id) const;

    /**
     * Get current status of VM
     * @param vm_id VM identifier
     * @return Current VM status
     * @throws VMError if VM doesn't exist
     */
    VMStatus get_vm_status(const VMId& vm_id) const;

    // ========== Health Monitoring ==========

    /**
     * Perform health check on specific VM
     * @param vm_id VM identifier
     * @return Health report
     * @throws VMError if VM doesn't exist
     */
    HealthReport check_health(const VMId& vm_id) const;

    /**
     * Perform health check on all VMs
     * @return Vector of health reports
     */
    std::vector<HealthReport> check_all_health() const;

    /**
     * Enable/disable automatic health monitoring
     * @param enabled Enable health monitoring
     */
    void set_health_monitoring(bool enabled);

    /**
     * Set health monitoring interval
     * @param interval Check interval
     */
    void set_health_check_interval(std::chrono::milliseconds interval);

    // ========== Configuration and Utilities ==========

    /**
     * Update VM configuration (for stopped VMs)
     * @param vm_id VM identifier
     * @param config New configuration
     * @throws VMError if VM is running or doesn't exist
     */
    void update_vm_config(const VMId& vm_id, const VMConfiguration& config);

    /**
     * Get VM configuration
     * @param vm_id VM identifier
     * @return VM configuration
     * @throws VMError if VM doesn't exist
     */
    VMConfiguration get_vm_config(const VMId& vm_id) const;

    /**
     * Clean up crashed or orphaned VMs
     * @return Number of VMs cleaned up
     */
    size_t cleanup_crashed_vms();

    /**
     * Get system resource usage
     * @return Current resource usage
     */
    virtual ResourceUsage get_resource_usage() const;

    /**
     * Get system limits and capabilities
     * @return System limits
     */
    SystemLimits get_system_limits() const;

    /**
     * Get allocated ports (implementation compatibility)
     * @return Vector of allocated port numbers
     */
    virtual std::vector<PortNumber> get_allocated_ports() const;

    // ========== Event Handling ==========

    /**
     * Register callback for VM status changes
     * @param callback Function to call on status changes
     * @return Callback ID for later removal
     */
    size_t register_status_callback(StatusCallback callback);

    /**
     * Unregister status callback
     * @param callback_id ID returned by register_status_callback
     */
    void unregister_status_callback(size_t callback_id);

    /**
     * Set status callback (implementation compatibility)
     * @param callback Status update callback
     */
    virtual void set_status_callback(StatusCallback callback);

    /**
     * Remove status callback (implementation compatibility)
     */
    virtual void remove_status_callback();

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scratchpad