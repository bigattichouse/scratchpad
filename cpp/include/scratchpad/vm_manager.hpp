#pragma once

#include "types.hpp"
#include "errors.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <future>

namespace scratchpad {

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
        std::string vm_directory;
        std::string ssh_keys_directory;
        ResourceLimits default_limits;
        bool enable_health_monitoring = true;
        std::chrono::milliseconds health_check_interval{30000};
    };

    /**
     * VM creation parameters
     */
    struct CreateParams {
        VMId vm_id;
        ImageType base_image;
        MemoryAmount memory = MemoryAmount::megabytes(512);
        DiskSize disk_size = DiskSize::gigabytes(10);
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
     * Construct VMManager with specified options
     */
    explicit VMManager(const Options& options = {});
    
    /**
     * Destructor - ensures clean shutdown of all VMs
     */
    ~VMManager();

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
    VMId create_vm(const CreateParams& params);

    /**
     * Start an existing virtual machine
     * @param vm_id VM identifier
     * @throws VMError if VM doesn't exist or start fails
     */
    void start_vm(const VMId& vm_id);

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
    void stop_vm(const VMId& vm_id, bool force = false);

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
    void destroy_vm(const VMId& vm_id);

    // ========== VM Communication ==========

    /**
     * Execute a command in the virtual machine
     * @param vm_id VM identifier
     * @param params Execution parameters
     * @return Command execution result
     * @throws VMError if VM is not running
     * @throws SSHError if communication fails
     */
    CommandResult execute_command(const VMId& vm_id, const ExecuteParams& params);

    /**
     * Execute command asynchronously
     * @param vm_id VM identifier
     * @param params Execution parameters
     * @return Future containing command result
     */
    std::future<CommandResult> execute_command_async(const VMId& vm_id, const ExecuteParams& params);

    /**
     * Get SSH connection for interactive use
     * @param vm_id VM identifier
     * @return SSH connection handle
     * @throws VMError if VM is not running
     * @throws SSHError if connection fails
     */
    std::unique_ptr<SSHConnection> connect_ssh(const VMId& vm_id);

    // ========== VM Information and Management ==========

    /**
     * List all virtual machines
     * @param status_filter Optional status filter
     * @return Vector of VM information
     */
    std::vector<VirtualMachine> list_vms(std::optional<VMStatus> status_filter = {}) const;

    /**
     * Get detailed information about a specific VM
     * @param vm_id VM identifier
     * @return VM information
     * @throws VMError if VM doesn't exist
     */
    VirtualMachine get_vm(const VMId& vm_id) const;

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
    ResourceUsage get_resource_usage() const;

    /**
     * Get system limits and capabilities
     * @return System limits
     */
    SystemLimits get_system_limits() const;

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

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scratchpad