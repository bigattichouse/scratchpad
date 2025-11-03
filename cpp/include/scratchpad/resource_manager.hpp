#pragma once

#include "types.hpp"
#include "errors.hpp"
#include <memory>
#include <vector>
#include <optional>
#include <chrono>

namespace scratchpad {

// Forward declarations
class PortAllocator;
class ResourceTracker;

/**
 * Interface for managing system resources - ports, memory, disk space, etc.
 * 
 * This class provides centralized resource management for the Scratchpad system,
 * ensuring proper allocation and tracking of system resources across multiple VMs.
 */
class ResourceManager {
public:
    /**
     * Configuration options for ResourceManager
     */
    struct Options {
        ResourceLimits system_limits;
        PortRange ssh_port_range{2222, 9999};
        PortRange vnc_port_range{5900, 5999};
        std::chrono::milliseconds resource_check_interval{10000}; // 10 seconds
        bool enable_resource_monitoring = true;
        double memory_reserve_percentage = 0.1; // Reserve 10% of system memory
        double disk_reserve_percentage = 0.1;   // Reserve 10% of disk space
    };

    /**
     * Resource allocation request
     */
    struct AllocationRequest {
        VMId vm_id;
        MemoryAmount memory_needed;
        DiskSize disk_needed;
        bool needs_ssh_port = true;
        bool needs_vnc_port = false;
        std::optional<PortNumber> preferred_ssh_port;
        std::optional<PortNumber> preferred_vnc_port;
    };

    /**
     * Resource allocation result
     */
    struct AllocationResult {
        bool success;
        std::string error_message;
        std::optional<PortNumber> allocated_ssh_port;
        std::optional<PortNumber> allocated_vnc_port;
        MemoryAmount allocated_memory;
        DiskSize allocated_disk;
    };

    /**
     * Resource quota for a VM or user
     */
    struct ResourceQuota {
        MemoryAmount max_memory;
        DiskSize max_disk_space;
        size_t max_concurrent_vms;
        size_t max_port_allocations;
    };

public:
    /**
     * Construct ResourceManager with specified options
     */
    explicit ResourceManager(const Options& options = {});
    
    /**
     * Destructor
     */
    ~ResourceManager();

    // Non-copyable, movable
    ResourceManager(const ResourceManager&) = delete;
    ResourceManager& operator=(const ResourceManager&) = delete;
    ResourceManager(ResourceManager&&) noexcept;
    ResourceManager& operator=(ResourceManager&&) noexcept;

    // ========== Resource Allocation ==========

    /**
     * Allocate resources for a VM
     * @param request Resource allocation request
     * @return Allocation result
     */
    AllocationResult allocate_resources(const AllocationRequest& request);

    /**
     * Release resources for a VM
     * @param vm_id VM identifier
     */
    void release_resources(const VMId& vm_id);

    /**
     * Update resource allocation for existing VM
     * @param vm_id VM identifier
     * @param new_request New resource requirements
     * @return Allocation result
     */
    AllocationResult update_allocation(const VMId& vm_id, const AllocationRequest& new_request);

    // ========== Port Management ==========

    /**
     * Allocate a single port from specified range
     * @param range Port range to allocate from
     * @param preferred_port Preferred port (optional)
     * @return Allocated port number
     * @throws ResourceError if no ports available
     */
    PortNumber allocate_port(const PortRange& range, std::optional<PortNumber> preferred_port = {});

    /**
     * Allocate multiple consecutive ports
     * @param range Port range to allocate from
     * @param count Number of consecutive ports needed
     * @return Vector of allocated port numbers
     * @throws ResourceError if insufficient consecutive ports
     */
    std::vector<PortNumber> allocate_port_range(const PortRange& range, size_t count);

    /**
     * Release a port back to the pool
     * @param port Port number to release
     */
    void release_port(PortNumber port);

    /**
     * Release multiple ports
     * @param ports Vector of port numbers to release
     */
    void release_ports(const std::vector<PortNumber>& ports);

    /**
     * Check if port is available
     * @param port Port number to check
     * @return true if port is available
     */
    bool is_port_available(PortNumber port) const;

    /**
     * Get list of allocated ports
     * @param vm_id Optional VM ID filter
     * @return Vector of allocated port numbers
     */
    std::vector<PortNumber> get_allocated_ports(const std::optional<VMId>& vm_id = {}) const;

    // ========== Resource Monitoring ==========

    /**
     * Get current system resource usage
     * @return Current resource usage
     */
    ResourceUsage get_current_usage() const;

    /**
     * Get system resource limits
     * @return System limits
     */
    SystemLimits get_system_limits() const;

    /**
     * Get resource usage for specific VM
     * @param vm_id VM identifier
     * @return VM resource usage
     * @throws ResourceError if VM not found
     */
    ResourceUsage get_vm_usage(const VMId& vm_id) const;

    /**
     * Get resource usage history
     * @param duration Time period for history
     * @return Vector of resource usage samples
     */
    std::vector<std::pair<std::chrono::system_clock::time_point, ResourceUsage>> 
    get_usage_history(std::chrono::duration<double> duration) const;

    // ========== Resource Quotas ==========

    /**
     * Set resource quota for VM or user
     * @param identifier VM ID or user identifier
     * @param quota Resource quota limits
     */
    void set_quota(const std::string& identifier, const ResourceQuota& quota);

    /**
     * Get resource quota
     * @param identifier VM ID or user identifier
     * @return Resource quota
     */
    std::optional<ResourceQuota> get_quota(const std::string& identifier) const;

    /**
     * Remove resource quota
     * @param identifier VM ID or user identifier
     */
    void remove_quota(const std::string& identifier);

    /**
     * Check if allocation would exceed quota
     * @param identifier VM ID or user identifier
     * @param request Resource allocation request
     * @return true if within quota limits
     */
    bool check_quota_compliance(const std::string& identifier, const AllocationRequest& request) const;

    // ========== Resource Availability ==========

    /**
     * Check if resources are available for allocation
     * @param request Resource allocation request
     * @return true if resources are available
     */
    bool are_resources_available(const AllocationRequest& request) const;

    /**
     * Get available memory
     * @return Available memory amount
     */
    MemoryAmount get_available_memory() const;

    /**
     * Get available disk space
     * @return Available disk space
     */
    DiskSize get_available_disk_space() const;

    /**
     * Get number of available ports in range
     * @param range Port range to check
     * @return Number of available ports
     */
    size_t get_available_port_count(const PortRange& range) const;

    /**
     * Estimate resource availability for future allocation
     * @param when Time point for estimation
     * @param request Resource allocation request
     * @return Estimated availability
     */
    bool estimate_future_availability(
        std::chrono::system_clock::time_point when,
        const AllocationRequest& request
    ) const;

    // ========== Resource Optimization ==========

    /**
     * Suggest optimal resource allocation
     * @param requests Vector of allocation requests
     * @return Optimized allocation suggestions
     */
    std::vector<AllocationResult> optimize_allocations(const std::vector<AllocationRequest>& requests) const;

    /**
     * Defragment port allocations
     * @return Number of ports moved for defragmentation
     */
    size_t defragment_port_allocations();

    /**
     * Compact resource allocations to free up resources
     * @return Amount of resources freed
     */
    ResourceUsage compact_allocations();

    // ========== System Information ==========

    /**
     * Detect system capabilities and limits
     * @return Detected system limits
     */
    static SystemLimits detect_system_limits();

    /**
     * Check if system has required capabilities
     * @param requirements Required capabilities
     * @return true if system meets requirements
     */
    bool check_system_requirements(const SystemLimits& requirements) const;

    /**
     * Get system acceleration support
     * @return Available acceleration type
     */
    AccelerationType get_available_acceleration() const;

    /**
     * Test system performance
     * @return Performance metrics and recommendations
     */
    std::map<std::string, std::string> run_performance_test() const;

    // ========== Resource Cleanup ==========

    /**
     * Clean up orphaned resource allocations
     * @return Number of allocations cleaned up
     */
    size_t cleanup_orphaned_allocations();

    /**
     * Force release all resources for crashed/missing VMs
     * @return Number of VMs cleaned up
     */
    size_t force_cleanup_vm_resources();

    /**
     * Reset all resource allocations (emergency use only)
     */
    void reset_all_allocations();

    // ========== Configuration ==========

    /**
     * Update resource limits
     * @param new_limits New system limits
     */
    void update_system_limits(const ResourceLimits& new_limits);

    /**
     * Enable/disable resource monitoring
     * @param enabled Enable monitoring
     */
    void set_monitoring_enabled(bool enabled);

    /**
     * Set monitoring interval
     * @param interval Check interval
     */
    void set_monitoring_interval(std::chrono::milliseconds interval);

    /**
     * Get current configuration
     * @return Current options
     */
    const Options& get_options() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scratchpad