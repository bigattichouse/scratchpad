#pragma once

#include <string>
#include <vector>
#include <memory>
#include <chrono>
#include <cstdint>
#include <optional>

namespace scratchpad {

// Forward declarations
class VirtualMachine;
class VMConfiguration;
class BaseImage;
class PreparedImage;
class QemuProcess;
class SSHConnection;

// VMId forward declaration - include full definition when needed
class VMId;
using ProcessId = uint32_t;
using PreparedImageId = std::string;
using PortNumber = uint16_t;

// Resource types
struct MemoryAmount {
    uint64_t bytes = 0;
    
    // Default constructor
    MemoryAmount() = default;
    MemoryAmount(uint64_t b) : bytes(b) {}
    
    static MemoryAmount from_string(const std::string& str);
    static MemoryAmount from_bytes(uint64_t bytes) { return {bytes}; }
    std::string to_string() const;
    
    // Common constructors
    static MemoryAmount megabytes(uint64_t mb) { return {mb * 1024 * 1024}; }
    static MemoryAmount gigabytes(uint64_t gb) { return {gb * 1024 * 1024 * 1024}; }
};

struct DiskSize {
    uint64_t bytes = 0;
    
    // Default constructor
    DiskSize() = default;
    DiskSize(uint64_t b) : bytes(b) {}
    
    static DiskSize from_string(const std::string& str);
    static DiskSize from_bytes(uint64_t bytes) { return {bytes}; }
    std::string to_string() const;
    
    // Common constructors
    static DiskSize megabytes(uint64_t mb) { return {mb * 1024 * 1024}; }
    static DiskSize gigabytes(uint64_t gb) { return {gb * 1024 * 1024 * 1024}; }
};

// Port range for resource allocation
struct PortRange {
    PortNumber start = 0;
    PortNumber end = 0;
    
    // Default constructor
    PortRange() = default;
    PortRange(PortNumber s, PortNumber e) : start(s), end(e) {}
    
    bool contains(PortNumber port) const noexcept {
        return port >= start && port <= end;
    }
    
    size_t size() const noexcept {
        return end - start + 1;
    }
};

// VM execution modes
enum class DiskMode {
    Ephemeral,   // Changes discarded on shutdown
    Persistent   // Changes saved to disk
};

enum class VMStatus {
    Stopped,
    Starting,
    Running,
    Stopping,
    Crashed,
    Error
};

enum class AccelerationType {
    KVM,    // Linux KVM
    HVF,    // macOS Hypervisor Framework
    WHPX,   // Windows Hypervisor Platform
    TCG     // QEMU TCG (software emulation)
};

enum class ImageType {
    Ubuntu,
    Ubuntu2204,
    Alpine,
    Alpine317,
    Debian,
    CentOS8,
    WindowsServer2022
};

// Command execution results
struct CommandResult {
    int exit_code;
    std::string stdout_output;
    std::string stderr_output;
    std::chrono::milliseconds execution_time;
    
    bool success() const noexcept {
        return exit_code == 0;
    }
};

// Network configuration
struct NetworkConfiguration {
    PortNumber ssh_port;
    std::optional<PortNumber> vnc_port;
    bool enable_port_forwarding = true;
};

// Resource limits and usage
struct ResourceLimits {
    MemoryAmount max_memory = MemoryAmount::gigabytes(8);
    DiskSize max_disk_size = DiskSize::gigabytes(100);
    DiskSize max_disk = DiskSize::gigabytes(100); // Alias for compatibility
    uint32_t max_cpu_cores = 4;
    PortRange port_range = {2222, 9999};
    uint32_t max_vms = 10;
    uint32_t max_ports = 1000;
};

struct ResourceUsage {
    MemoryAmount memory_used;
    DiskSize disk_used;
    double cpu_percent = 0.0;
    uint64_t network_rx_bytes = 0;
    uint64_t network_tx_bytes = 0;
    uint32_t active_processes = 0;
    std::vector<PortNumber> allocated_ports;
    
    // Implementation compatibility fields
    uint32_t running_vms = 0;
    uint32_t total_vms = 0;
    uint64_t allocated_memory = 0;
    uint64_t allocated_disk = 0;
    uint32_t allocated_cpu_cores = 0;
};

// Health check results
struct HealthStatus {
    bool process_running;
    bool ssh_responsive;
    bool disk_accessible;
    std::optional<std::chrono::milliseconds> uptime;
    std::string status_message;
};

struct HealthReport {
    std::string vm_id;
    VMStatus status;
    HealthStatus health;
    std::chrono::system_clock::time_point last_check;
};

// System information
struct SystemLimits {
    ResourceLimits hardware_limits;
    AccelerationType available_acceleration;
    std::vector<ImageType> supported_images;
    bool has_kvm_support;
    bool has_ssh_client;
    bool has_qemu;
};

// Package management
using PackageList = std::vector<std::string>;

// Image management types
struct ImageInfo {
    std::string name;
    ImageType type;
    std::string description;
    DiskSize size;
    bool is_available = false;
    bool is_local = false;
    std::string checksum;
    std::string url;
    std::string version;
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point last_modified;
    std::string architecture;
    std::string os_family;
    bool is_downloaded = false;
    bool is_prepared = false;
    std::string local_path;
};

struct OverlayDisk {
    std::string name;
    std::string path;
    DiskSize size;
    std::string base_image;
    std::chrono::system_clock::time_point created_at;
    bool is_active = false;
};

struct ProgressInfo {
    size_t bytes_downloaded = 0;
    size_t total_bytes = 0;
    std::string status_message;
    double progress_percent = 0.0;
    
    // Convenience constructor
    ProgressInfo(double percent, const std::string& message) 
        : status_message(message), progress_percent(percent) {}
    
    // Default constructor
    ProgressInfo() = default;
};

// Resource management types
struct ResourceRequest {
    VMId vm_id;
    MemoryAmount memory;
    DiskSize disk;
    uint32_t cpu_cores;
    PortRange port_range = {20000, 30000};
};

struct ResourceAllocation {
    VMId vm_id;
    MemoryAmount memory;
    DiskSize disk;
    uint32_t cpu_cores;
    PortNumber ssh_port = 0;
    std::optional<PortNumber> vnc_port;
    std::chrono::system_clock::time_point allocated_at;
};

struct SystemResources {
    MemoryAmount total_memory;
    DiskSize total_disk;
    uint32_t total_cpu_cores;
    PortRange available_port_range;
    double cpu_usage_percent = 0.0;
    double memory_usage_percent = 0.0;
    double disk_usage_percent = 0.0;
};

struct MemoryUsage {
    size_t used_bytes = 0;
    size_t total_bytes = 0;
    double usage_percent = 0.0;
    std::vector<std::string> top_consumers;
};

enum class ResourceEvent {
    Allocated,
    Deallocated,
    LimitExceeded,
    SystemLow,
    SystemCritical
};

using ResourceCallback = std::function<void(ResourceEvent event, const std::string& details)>;

// SSH credentials and configuration
struct SSHCredentials {
    std::string username;
    std::string private_key_path;
    std::string public_key_path;
    PortNumber port;
    std::chrono::milliseconds timeout{30000};
    
    // Factory methods
    static SSHCredentials create_with_key_files(
        const std::string& username,
        const std::string& private_key_path,
        const std::string& public_key_path,
        PortNumber port,
        std::chrono::milliseconds timeout = std::chrono::milliseconds{30000}
    );
    
    static SSHCredentials create_default(
        const std::string& username,
        PortNumber port
    );
};

// Image source information
struct ImageSource {
    std::string url;
    std::string filename;
    std::string checksum;
    ImageType type;
    std::string default_username;
};

// Provisioning configuration
struct ProvisioningConfig {
    ImageType base_image;
    PackageList packages;
    std::vector<std::string> custom_commands;
    std::string timezone = "UTC";
    bool update_packages = true;
    bool install_essential_tools = true;
};

// Forward declaration for VMInfo - defined in vm_manager.hpp
struct VMInfo;

// File copy parameters
struct CopyParams {
    std::string source;
    std::string destination;
    bool recursive = false;
    bool preserve_permissions = true;
    std::chrono::milliseconds timeout{30000};
};

// Error types and codes
enum class ErrorCode {
    Success = 0,
    
    // VM errors (100-199)
    VMNotFound = 100,
    VMAlreadyExists = 101,
    VMNotRunning = 102,
    VMStartupFailed = 103,
    VMShutdownFailed = 104,
    
    // Process errors (200-299)
    ProcessNotFound = 200,
    ProcessStartFailed = 201,
    ProcessKillFailed = 202,
    QemuNotFound = 203,
    
    // SSH errors (300-399)
    SSHConnectionFailed = 300,
    SSHAuthenticationFailed = 301,
    SSHCommandFailed = 302,
    SSHKeyNotFound = 303,
    
    // Image errors (400-499)
    ImageNotFound = 400,
    ImageDownloadFailed = 401,
    ImageCorrupted = 402,
    ProvisioningFailed = 403,
    
    // Resource errors (500-599)
    OutOfMemory = 500,
    OutOfDiskSpace = 501,
    PortUnavailable = 502,
    InsufficientResources = 503,
    
    // System errors (600-699)
    FileSystemError = 600,
    NetworkError = 601,
    PermissionDenied = 602,
    ConfigurationError = 603,
    
    // Generic errors (900-999)
    InvalidArgument = 900,
    NotImplemented = 901,
    InternalError = 902,
    UnknownError = 999
};

} // namespace scratchpad