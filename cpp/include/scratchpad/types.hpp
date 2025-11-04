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
    uint64_t bytes;
    
    static MemoryAmount from_string(const std::string& str);
    static MemoryAmount from_bytes(uint64_t bytes) { return {bytes}; }
    std::string to_string() const;
    
    // Common constructors
    static MemoryAmount megabytes(uint64_t mb) { return {mb * 1024 * 1024}; }
    static MemoryAmount gigabytes(uint64_t gb) { return {gb * 1024 * 1024 * 1024}; }
};

struct DiskSize {
    uint64_t bytes;
    
    static DiskSize from_string(const std::string& str);
    static DiskSize from_bytes(uint64_t bytes) { return {bytes}; }
    std::string to_string() const;
    
    // Common constructors
    static DiskSize megabytes(uint64_t mb) { return {mb * 1024 * 1024}; }
    static DiskSize gigabytes(uint64_t gb) { return {gb * 1024 * 1024 * 1024}; }
};

// Port range for resource allocation
struct PortRange {
    PortNumber start;
    PortNumber end;
    
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
    MemoryAmount max_memory;
    DiskSize max_disk_size;
    uint32_t max_cpu_cores;
    PortRange port_range;
};

struct ResourceUsage {
    MemoryAmount used_memory;
    DiskSize used_disk_space;
    uint32_t active_processes;
    std::vector<PortNumber> allocated_ports;
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

// SSH credentials and configuration
struct SSHCredentials {
    std::string username;
    std::string private_key_path;
    std::string public_key_path;
    PortNumber port;
    std::chrono::milliseconds timeout{30000};
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