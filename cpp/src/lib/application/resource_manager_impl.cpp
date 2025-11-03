#include "application/resource_manager_impl.hpp"
#include "scratchpad/errors.hpp"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <random>
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <unistd.h>

namespace scratchpad {

ResourceManagerImpl::ResourceManagerImpl() 
    : logger_(Logger::instance()) {
    
    logger_.info("Initializing Resource Manager");
    
    // Discover system resources
    discover_system_resources();
    
    // Set default resource limits (80% of system resources)
    resource_limits_.max_memory = MemoryAmount{static_cast<uint64_t>(system_resources_.total_memory.bytes * 0.8)};
    resource_limits_.max_disk = DiskSize{static_cast<uint64_t>(system_resources_.total_disk.bytes * 0.8)};
    resource_limits_.max_cpu_cores = static_cast<uint32_t>(system_resources_.total_cpu_cores * 0.8);
    resource_limits_.max_vms = 10;
    resource_limits_.max_ports = 1000;
    
    start_monitoring_thread();
    
    logger_.info("Resource Manager initialized - Total: {}MB memory, {}GB disk, {} CPUs", 
                system_resources_.total_memory.bytes / (1024 * 1024),
                system_resources_.total_disk.bytes / (1024 * 1024 * 1024),
                system_resources_.total_cpu_cores);
}

ResourceManagerImpl::~ResourceManagerImpl() {
    logger_.info("Shutting down Resource Manager");
    
    stop_monitoring_thread();
    
    // Cleanup all allocated resources
    cleanup_unused_resources();
    
    logger_.info("Resource Manager shutdown complete");
}

ResourceAllocation ResourceManagerImpl::allocate_resources(const ResourceRequest& request) {
    logger_.debug("Allocating resources: {}MB memory, {}GB disk, {} CPUs for VM {}", 
                 request.memory.bytes / (1024 * 1024),
                 request.disk.bytes / (1024 * 1024 * 1024),
                 request.cpu_cores,
                 request.vm_id);
    
    validate_resource_request(request);
    
    if (!can_allocate_resources(request)) {
        THROW_RESOURCE_ERROR(ErrorCode::InsufficientResources,
                           "Insufficient resources available for allocation", "system");
    }
    
    std::unique_lock lock(allocations_mutex_);
    
    // Check if VM already has an allocation
    auto vm_it = vm_allocations_.find(request.vm_id);
    if (vm_it != vm_allocations_.end()) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "VM already has resource allocation: " + request.vm_id, "vm");
    }
    
    // Allocate SSH port
    PortNumber ssh_port = find_available_port({22000, 22999});
    if (ssh_port == 0) {
        THROW_RESOURCE_ERROR(ErrorCode::PortUnavailable,
                           "No available SSH ports", "ssh_port");
    }
    
    // Create allocations
    VMResourceAllocation vm_allocation(request.vm_id, request);
    vm_allocation.ssh_port = ssh_port;
    
    PortAllocation port_allocation(ssh_port, request.vm_id);
    
    // Update tracking
    vm_allocations_[request.vm_id] = vm_allocation;
    port_allocations_[ssh_port] = port_allocation;
    
    allocated_memory_bytes_ += request.memory.bytes;
    allocated_disk_bytes_ += request.disk.bytes;
    allocated_cpu_cores_ += request.cpu_cores;
    allocated_port_count_++;
    
    lock.unlock();
    
    // Create result
    ResourceAllocation allocation;
    allocation.vm_id = request.vm_id;
    allocation.memory = request.memory;
    allocation.disk = request.disk;
    allocation.cpu_cores = request.cpu_cores;
    allocation.ssh_port = ssh_port;
    allocation.allocated_at = std::chrono::system_clock::now();
    
    logger_.info("Resources allocated successfully for VM {}: port {}", request.vm_id, ssh_port);
    
    // Check if we're approaching resource limits
    check_resource_thresholds();
    
    return allocation;
}

void ResourceManagerImpl::deallocate_resources(const ResourceAllocation& allocation) {
    logger_.debug("Deallocating resources for VM {}", allocation.vm_id);
    
    std::unique_lock lock(allocations_mutex_);
    
    // Remove VM allocation
    auto vm_it = vm_allocations_.find(allocation.vm_id);
    if (vm_it != vm_allocations_.end()) {
        allocated_memory_bytes_ -= vm_it->second.memory.bytes;
        allocated_disk_bytes_ -= vm_it->second.disk.bytes;
        allocated_cpu_cores_ -= vm_it->second.cpu_cores;
        vm_allocations_.erase(vm_it);
    }
    
    // Remove port allocation
    auto port_it = port_allocations_.find(allocation.ssh_port);
    if (port_it != port_allocations_.end()) {
        port_allocations_.erase(port_it);
        allocated_port_count_--;
    }
    
    logger_.info("Resources deallocated for VM {}", allocation.vm_id);
}

bool ResourceManagerImpl::can_allocate_resources(const ResourceRequest& request) const {
    std::shared_lock lock(allocations_mutex_);
    
    // Check memory
    uint64_t total_allocated_memory = allocated_memory_bytes_ + request.memory.bytes;
    if (total_allocated_memory > resource_limits_.max_memory.bytes) {
        return false;
    }
    
    // Check disk
    uint64_t total_allocated_disk = allocated_disk_bytes_ + request.disk.bytes;
    if (total_allocated_disk > resource_limits_.max_disk.bytes) {
        return false;
    }
    
    // Check CPU
    uint32_t total_allocated_cpus = allocated_cpu_cores_ + request.cpu_cores;
    if (total_allocated_cpus > resource_limits_.max_cpu_cores) {
        return false;
    }
    
    // Check VM count
    if (vm_allocations_.size() >= resource_limits_.max_vms) {
        return false;
    }
    
    // Check port availability
    if (allocated_port_count_ >= resource_limits_.max_ports) {
        return false;
    }
    
    return true;
}

SystemResources ResourceManagerImpl::get_system_resources() const {
    return system_resources_;
}

ResourceUsage ResourceManagerImpl::get_current_usage() const {
    std::shared_lock lock(allocations_mutex_);
    
    ResourceUsage usage;
    usage.allocated_memory = MemoryAmount{allocated_memory_bytes_.load()};
    usage.allocated_disk = DiskSize{allocated_disk_bytes_.load()};
    usage.allocated_cpu_cores = allocated_cpu_cores_.load();
    usage.allocated_ports = allocated_port_count_.load();
    usage.running_vms = vm_allocations_.size();
    usage.total_vms = vm_allocations_.size();
    
    return usage;
}

void ResourceManagerImpl::set_resource_limits(const ResourceLimits& limits) {
    // Validate limits don't exceed system resources
    if (limits.max_memory.bytes > system_resources_.total_memory.bytes) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Memory limit exceeds system capacity", "memory");
    }
    
    if (limits.max_disk.bytes > system_resources_.total_disk.bytes) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Disk limit exceeds system capacity", "disk");
    }
    
    if (limits.max_cpu_cores > system_resources_.total_cpu_cores) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "CPU limit exceeds system capacity", "cpu");
    }
    
    resource_limits_ = limits;
    
    logger_.info("Resource limits updated: {}MB memory, {}GB disk, {} CPUs, {} VMs",
                limits.max_memory.bytes / (1024 * 1024),
                limits.max_disk.bytes / (1024 * 1024 * 1024),
                limits.max_cpu_cores,
                limits.max_vms);
}

ResourceLimits ResourceManagerImpl::get_resource_limits() const {
    return resource_limits_;
}

PortNumber ResourceManagerImpl::allocate_port(PortRange range) {
    validate_port_range(range);
    
    std::unique_lock lock(allocations_mutex_);
    
    PortNumber port = find_available_port(range);
    if (port == 0) {
        THROW_RESOURCE_ERROR(ErrorCode::PortUnavailable,
                           "No available ports in range " + std::to_string(range.start) + 
                           "-" + std::to_string(range.end), "port");
    }
    
    PortAllocation allocation(port, "manual");
    port_allocations_[port] = allocation;
    allocated_port_count_++;
    
    logger_.debug("Port allocated: {}", port);
    
    return port;
}

void ResourceManagerImpl::deallocate_port(PortNumber port) {
    std::unique_lock lock(allocations_mutex_);
    
    auto it = port_allocations_.find(port);
    if (it != port_allocations_.end()) {
        port_allocations_.erase(it);
        allocated_port_count_--;
        logger_.debug("Port deallocated: {}", port);
    }
}

std::vector<PortNumber> ResourceManagerImpl::get_allocated_ports() const {
    std::shared_lock lock(allocations_mutex_);
    
    std::vector<PortNumber> ports;
    ports.reserve(port_allocations_.size());
    
    for (const auto& [port, allocation] : port_allocations_) {
        ports.push_back(port);
    }
    
    return ports;
}

bool ResourceManagerImpl::is_port_available(PortNumber port) const {
    std::shared_lock lock(allocations_mutex_);
    return port_allocations_.find(port) == port_allocations_.end();
}

void* ResourceManagerImpl::allocate_memory(size_t size, const std::string& purpose) {
    if (size == 0) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Memory allocation size cannot be zero", "memory");
    }
    
    void* ptr = std::aligned_alloc(64, size); // 64-byte aligned
    if (!ptr) {
        THROW_RESOURCE_ERROR(ErrorCode::OutOfMemory,
                           "Failed to allocate " + std::to_string(size) + " bytes", "memory");
    }
    
    std::unique_lock lock(allocations_mutex_);
    
    MemoryAllocation allocation(ptr, size, purpose);
    memory_allocations_[ptr] = allocation;
    
    logger_.debug("Memory allocated: {} bytes for {}", size, purpose);
    
    return ptr;
}

void ResourceManagerImpl::deallocate_memory(void* ptr) {
    if (!ptr) {
        return;
    }
    
    std::unique_lock lock(allocations_mutex_);
    
    auto it = memory_allocations_.find(ptr);
    if (it != memory_allocations_.end()) {
        logger_.debug("Memory deallocated: {} bytes for {}", 
                     it->second.size, it->second.purpose);
        memory_allocations_.erase(it);
        lock.unlock();
        
        std::free(ptr);
    } else {
        logger_.warning("Attempted to deallocate unknown memory pointer");
    }
}

MemoryUsage ResourceManagerImpl::get_memory_usage() const {
    std::shared_lock lock(allocations_mutex_);
    
    MemoryUsage usage;
    usage.total_allocated = 0;
    usage.allocations_by_purpose.clear();
    
    for (const auto& [ptr, allocation] : memory_allocations_) {
        usage.total_allocated += allocation.size;
        usage.allocations_by_purpose[allocation.purpose] += allocation.size;
    }
    
    return usage;
}

void ResourceManagerImpl::set_resource_callback(ResourceCallback callback) {
    std::lock_guard lock(callback_mutex_);
    resource_callback_ = std::move(callback);
}

void ResourceManagerImpl::remove_resource_callback() {
    std::lock_guard lock(callback_mutex_);
    resource_callback_ = nullptr;
}

void ResourceManagerImpl::cleanup_unused_resources() {
    logger_.info("Cleaning up unused resources");
    
    cleanup_stale_allocations();
    cleanup_orphaned_ports();
    cleanup_leaked_memory();
    
    logger_.info("Resource cleanup complete");
}

void ResourceManagerImpl::defragment_resources() {
    logger_.info("Defragmenting resources");
    
    // For now, this is mainly about cleaning up and reorganizing allocations
    cleanup_unused_resources();
    
    logger_.info("Resource defragmentation complete");
}

// Private helper methods

bool ResourceManagerImpl::check_memory_availability(MemoryAmount requested) const {
    return (allocated_memory_bytes_ + requested.bytes) <= resource_limits_.max_memory.bytes;
}

bool ResourceManagerImpl::check_disk_availability(DiskSize requested) const {
    return (allocated_disk_bytes_ + requested.bytes) <= resource_limits_.max_disk.bytes;
}

bool ResourceManagerImpl::check_cpu_availability(uint32_t requested_cores) const {
    return (allocated_cpu_cores_ + requested_cores) <= resource_limits_.max_cpu_cores;
}

PortNumber ResourceManagerImpl::find_available_port(PortRange range) const {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    
    // Try random ports first to avoid clustering
    std::uniform_int_distribution<PortNumber> dist(range.start, range.end);
    
    for (int attempts = 0; attempts < 100; ++attempts) {
        PortNumber candidate = dist(gen);
        if (port_allocations_.find(candidate) == port_allocations_.end()) {
            return candidate;
        }
    }
    
    // Fall back to sequential search
    for (PortNumber port = range.start; port <= range.end; ++port) {
        if (port_allocations_.find(port) == port_allocations_.end()) {
            return port;
        }
    }
    
    return 0; // No available port
}

void ResourceManagerImpl::discover_system_resources() {
    system_resources_.total_memory = get_total_system_memory();
    system_resources_.total_disk = get_total_system_disk();
    system_resources_.total_cpu_cores = get_total_system_cpus();
    
    logger_.debug("Discovered system resources: {}MB memory, {}GB disk, {} CPUs",
                 system_resources_.total_memory.bytes / (1024 * 1024),
                 system_resources_.total_disk.bytes / (1024 * 1024 * 1024),
                 system_resources_.total_cpu_cores);
}

MemoryAmount ResourceManagerImpl::get_total_system_memory() const {
    struct sysinfo info;
    if (sysinfo(&info) == 0) {
        return MemoryAmount{info.totalram * info.mem_unit};
    }
    
    // Fallback: try reading from /proc/meminfo
    std::ifstream meminfo("/proc/meminfo");
    if (meminfo.is_open()) {
        std::string line;
        while (std::getline(meminfo, line)) {
            if (line.starts_with("MemTotal:")) {
                std::istringstream iss(line);
                std::string label;
                uint64_t value;
                std::string unit;
                if (iss >> label >> value >> unit) {
                    return MemoryAmount{value * 1024}; // kB to bytes
                }
            }
        }
    }
    
    // Default fallback
    return MemoryAmount::from_string("8G");
}

DiskSize ResourceManagerImpl::get_total_system_disk() const {
    struct statvfs stat;
    if (statvfs("/", &stat) == 0) {
        uint64_t total_bytes = stat.f_blocks * stat.f_frsize;
        return DiskSize{total_bytes};
    }
    
    // Default fallback
    return DiskSize::from_string("100G");
}

uint32_t ResourceManagerImpl::get_total_system_cpus() const {
    return static_cast<uint32_t>(sysconf(_SC_NPROCESSORS_ONLN));
}

void ResourceManagerImpl::start_monitoring_thread() {
    monitoring_active_ = true;
    monitoring_thread_ = std::thread(&ResourceManagerImpl::monitor_resources, this);
}

void ResourceManagerImpl::stop_monitoring_thread() {
    monitoring_active_ = false;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

void ResourceManagerImpl::monitor_resources() {
    while (monitoring_active_) {
        try {
            check_resource_thresholds();
            cleanup_stale_allocations();
        } catch (const std::exception& e) {
            logger_.error("Error in resource monitoring thread: {}", e.what());
        }
        
        std::this_thread::sleep_for(monitoring_interval_);
    }
}

void ResourceManagerImpl::check_resource_thresholds() {
    ResourceUsage usage = get_current_usage();
    
    // Check memory threshold
    double memory_ratio = static_cast<double>(usage.allocated_memory.bytes) / 
                         static_cast<double>(resource_limits_.max_memory.bytes);
    if (memory_ratio > memory_warning_threshold_) {
        ResourceEvent event;
        event.type = ResourceEventType::MemoryThresholdExceeded;
        event.message = "Memory usage: " + std::to_string(static_cast<int>(memory_ratio * 100)) + "%";
        event.current_usage = usage;
        notify_resource_event(event);
    }
    
    // Check disk threshold
    double disk_ratio = static_cast<double>(usage.allocated_disk.bytes) / 
                       static_cast<double>(resource_limits_.max_disk.bytes);
    if (disk_ratio > disk_warning_threshold_) {
        ResourceEvent event;
        event.type = ResourceEventType::DiskThresholdExceeded;
        event.message = "Disk usage: " + std::to_string(static_cast<int>(disk_ratio * 100)) + "%";
        event.current_usage = usage;
        notify_resource_event(event);
    }
    
    // Check CPU threshold
    double cpu_ratio = static_cast<double>(usage.allocated_cpu_cores) / 
                      static_cast<double>(resource_limits_.max_cpu_cores);
    if (cpu_ratio > cpu_warning_threshold_) {
        ResourceEvent event;
        event.type = ResourceEventType::CPUThresholdExceeded;
        event.message = "CPU usage: " + std::to_string(static_cast<int>(cpu_ratio * 100)) + "%";
        event.current_usage = usage;
        notify_resource_event(event);
    }
}

void ResourceManagerImpl::notify_resource_event(ResourceEvent event) {
    std::lock_guard lock(callback_mutex_);
    if (resource_callback_) {
        try {
            resource_callback_(event);
        } catch (const std::exception& e) {
            logger_.error("Error in resource callback: {}", e.what());
        }
    }
}

void ResourceManagerImpl::cleanup_stale_allocations() {
    auto now = std::chrono::system_clock::now();
    std::vector<std::string> stale_vms;
    
    {
        std::shared_lock lock(allocations_mutex_);
        
        for (const auto& [vm_id, allocation] : vm_allocations_) {
            auto age = std::chrono::duration_cast<std::chrono::hours>(now - allocation.allocated_at);
            if (age > stale_allocation_timeout_) {
                stale_vms.push_back(vm_id);
            }
        }
    }
    
    if (!stale_vms.empty()) {
        logger_.warning("Found {} stale VM allocations", stale_vms.size());
        
        std::unique_lock lock(allocations_mutex_);
        for (const auto& vm_id : stale_vms) {
            auto it = vm_allocations_.find(vm_id);
            if (it != vm_allocations_.end()) {
                allocated_memory_bytes_ -= it->second.memory.bytes;
                allocated_disk_bytes_ -= it->second.disk.bytes;
                allocated_cpu_cores_ -= it->second.cpu_cores;
                
                // Remove associated port
                auto port_it = port_allocations_.find(it->second.ssh_port);
                if (port_it != port_allocations_.end()) {
                    port_allocations_.erase(port_it);
                    allocated_port_count_--;
                }
                
                vm_allocations_.erase(it);
                logger_.info("Cleaned up stale allocation for VM: {}", vm_id);
            }
        }
    }
}

void ResourceManagerImpl::cleanup_orphaned_ports() {
    std::unique_lock lock(allocations_mutex_);
    
    std::vector<PortNumber> orphaned_ports;
    
    for (const auto& [port, allocation] : port_allocations_) {
        if (allocation.vm_id != "manual" && 
            vm_allocations_.find(allocation.vm_id) == vm_allocations_.end()) {
            orphaned_ports.push_back(port);
        }
    }
    
    for (PortNumber port : orphaned_ports) {
        port_allocations_.erase(port);
        allocated_port_count_--;
        logger_.debug("Cleaned up orphaned port: {}", port);
    }
}

void ResourceManagerImpl::cleanup_leaked_memory() {
    // For now, just log memory allocation statistics
    // In a more sophisticated implementation, we could track allocation ages
    // and clean up very old allocations
    
    std::shared_lock lock(allocations_mutex_);
    
    if (!memory_allocations_.empty()) {
        logger_.debug("Active memory allocations: {}", memory_allocations_.size());
        
        std::map<std::string, size_t> usage_by_purpose;
        for (const auto& [ptr, allocation] : memory_allocations_) {
            usage_by_purpose[allocation.purpose] += allocation.size;
        }
        
        for (const auto& [purpose, size] : usage_by_purpose) {
            logger_.debug("  {}: {} bytes", purpose, size);
        }
    }
}

void ResourceManagerImpl::validate_resource_request(const ResourceRequest& request) {
    if (request.vm_id.empty()) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "VM ID cannot be empty", "vm_id");
    }
    
    if (request.memory.bytes == 0) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Memory allocation cannot be zero", "memory");
    }
    
    if (request.disk.bytes == 0) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Disk allocation cannot be zero", "disk");
    }
    
    if (request.cpu_cores == 0) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "CPU cores cannot be zero", "cpu");
    }
    
    // Check minimum requirements
    if (request.memory.bytes < MemoryAmount::from_string("256M").bytes) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Minimum memory requirement is 256MB", "memory");
    }
    
    if (request.disk.bytes < DiskSize::from_string("1G").bytes) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Minimum disk requirement is 1GB", "disk");
    }
}

void ResourceManagerImpl::validate_port_range(PortRange range) {
    if (range.start > range.end) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Invalid port range: start > end", "port_range");
    }
    
    if (range.start < 1024) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Port range cannot include privileged ports (< 1024)", "port_range");
    }
    
    if (range.end > 65535) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Port range cannot exceed maximum port number (65535)", "port_range");
    }
}

} // namespace scratchpad