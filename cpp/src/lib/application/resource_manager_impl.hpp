#pragma once

#include "scratchpad/resource_manager.hpp"
#include "logging/logger.hpp"

#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <atomic>
#include <thread>

namespace scratchpad {

class ResourceManagerImpl : public ResourceManager {
public:
    ResourceManagerImpl();
    ~ResourceManagerImpl() override;

    // Resource Allocation and Management
    ResourceAllocation allocate_resources(const ResourceRequest& request) override;
    void deallocate_resources(const ResourceAllocation& allocation) override;
    bool can_allocate_resources(const ResourceRequest& request) const override;

    // Resource Monitoring and Limits
    SystemResources get_system_resources() const override;
    ResourceUsage get_current_usage() const override;
    void set_resource_limits(const ResourceLimits& limits) override;
    ResourceLimits get_resource_limits() const override;

    // Port Management
    PortNumber allocate_port(PortRange range = PortRange{20000, 30000}) override;
    void deallocate_port(PortNumber port) override;
    std::vector<PortNumber> get_allocated_ports() const override;
    bool is_port_available(PortNumber port) const override;

    // Memory Management
    void* allocate_memory(size_t size, const std::string& purpose) override;
    void deallocate_memory(void* ptr) override;
    MemoryUsage get_memory_usage() const override;

    // Resource Monitoring
    void set_resource_callback(ResourceCallback callback) override;
    void remove_resource_callback() override;

    // Cleanup and Maintenance
    void cleanup_unused_resources() override;
    void defragment_resources() override;

private:
    struct PortAllocation {
        PortNumber port;
        std::string vm_id;
        std::chrono::system_clock::time_point allocated_at;
        
        PortAllocation(PortNumber p, const std::string& id)
            : port(p), vm_id(id), allocated_at(std::chrono::system_clock::now()) {}
    };
    
    struct MemoryAllocation {
        void* ptr;
        size_t size;
        std::string purpose;
        std::chrono::system_clock::time_point allocated_at;
        
        MemoryAllocation(void* p, size_t s, const std::string& purpose)
            : ptr(p), size(s), purpose(purpose), allocated_at(std::chrono::system_clock::now()) {}
    };
    
    struct VMResourceAllocation {
        std::string vm_id;
        MemoryAmount memory;
        DiskSize disk;
        uint32_t cpu_cores;
        PortNumber ssh_port;
        std::chrono::system_clock::time_point allocated_at;
        
        VMResourceAllocation(const std::string& id, const ResourceRequest& request)
            : vm_id(id), memory(request.memory), disk(request.disk), 
              cpu_cores(request.cpu_cores), ssh_port(0),
              allocated_at(std::chrono::system_clock::now()) {}
    };

    // Resource allocation helpers
    bool check_memory_availability(MemoryAmount requested) const;
    bool check_disk_availability(DiskSize requested) const;
    bool check_cpu_availability(uint32_t requested_cores) const;
    PortNumber find_available_port(PortRange range) const;

    // System resource discovery
    void discover_system_resources();
    MemoryAmount get_total_system_memory() const;
    DiskSize get_total_system_disk() const;
    uint32_t get_total_system_cpus() const;
    
    // Resource monitoring
    void start_monitoring_thread();
    void stop_monitoring_thread();
    void monitor_resources();
    void check_resource_thresholds();
    void notify_resource_event(ResourceEvent event);

    // Resource cleanup
    void cleanup_stale_allocations();
    void cleanup_orphaned_ports();
    void cleanup_leaked_memory();

    // Validation
    void validate_resource_request(const ResourceRequest& request);
    void validate_port_range(PortRange range);

    // Member variables
    mutable std::shared_mutex allocations_mutex_;
    std::unordered_map<std::string, VMResourceAllocation> vm_allocations_;
    std::unordered_map<PortNumber, PortAllocation> port_allocations_;
    std::unordered_map<void*, MemoryAllocation> memory_allocations_;
    
    // System resources
    SystemResources system_resources_;
    ResourceLimits resource_limits_;
    
    // Resource tracking
    std::atomic<uint64_t> allocated_memory_bytes_{0};
    std::atomic<uint64_t> allocated_disk_bytes_{0};
    std::atomic<uint32_t> allocated_cpu_cores_{0};
    std::atomic<size_t> allocated_port_count_{0};
    
    // Monitoring
    ResourceCallback resource_callback_;
    std::mutex callback_mutex_;
    std::atomic<bool> monitoring_active_{false};
    std::thread monitoring_thread_;
    
    // Configuration
    std::chrono::milliseconds monitoring_interval_{10000}; // 10 seconds
    std::chrono::hours stale_allocation_timeout_{24}; // 24 hours
    double memory_warning_threshold_{0.8}; // 80%
    double disk_warning_threshold_{0.85}; // 85%
    double cpu_warning_threshold_{0.9}; // 90%
    
    Logger& logger_;
};

} // namespace scratchpad