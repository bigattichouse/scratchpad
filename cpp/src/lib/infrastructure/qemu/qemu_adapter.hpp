#pragma once

#include "scratchpad/types.hpp"
#include "domain/vm/vm_configuration.hpp"
#include <string>
#include <vector>

namespace scratchpad::infrastructure {

/**
 * QEMU configuration options for VM execution
 */
struct QemuOptions {
    // Core configuration
    AccelerationType acceleration_type = AccelerationType::TCG;
    std::string disk_image_path;
    std::string cloud_init_iso_path;
    
    // Network configuration
    PortNumber ssh_port = 0;
    PortNumber vnc_port = 0;
    
    // Display and I/O
    bool enable_serial_output = false;
    std::string serial_log_path;
    
    // Advanced options
    std::string firmware_path;
    std::vector<std::string> extra_args;
    
    /**
     * Create default QEMU options
     * @return Default options
     */
    static QemuOptions create_default();
    
    /**
     * Create QEMU options optimized for specific VM configuration
     * @param config VM configuration
     * @return Optimized options
     */
    static QemuOptions create_for_vm(const VMConfiguration& config);
    
    /**
     * Validate options
     * @return true if options are valid
     */
    bool validate() const;
};

/**
 * Infrastructure adapter for QEMU virtualization
 * 
 * Provides abstraction layer over QEMU command line interface,
 * handles system detection, and manages VM process lifecycle.
 */
class QemuAdapter {
public:
    /**
     * QEMU system capabilities
     */
    struct Capabilities {
        std::string qemu_executable;
        std::string version;
        AccelerationType best_acceleration;
        std::vector<AccelerationType> supported_accelerations;
        bool supports_virtfs = false;
        bool supports_vnc = false;
        bool supports_usb = false;
    };
    
    /**
     * Disk image information
     */
    struct DiskInfo {
        std::string path;
        bool exists = false;
        bool valid = false;
        std::string format;
        uint64_t size_bytes = 0;
    };

    /**
     * Constructor - automatically detects QEMU capabilities
     */
    QemuAdapter();

    // ========== Command Line Generation ==========

    /**
     * Build QEMU command line arguments
     * @param config VM configuration
     * @param options QEMU-specific options
     * @return Command line arguments vector
     * @throws SystemError if QEMU not found or configuration invalid
     */
    std::vector<std::string> build_command_line(const VMConfiguration& config,
                                               const QemuOptions& options);

    // ========== System Detection ==========

    /**
     * Detect QEMU executable path
     * @return Path to qemu-system-x86_64 executable
     * @throws SystemError if QEMU not found
     */
    static std::string detect_qemu_executable();

    /**
     * Detect best available acceleration
     * @return Best acceleration type for current system
     */
    static AccelerationType detect_best_acceleration();

    /**
     * Check if specific acceleration is available
     * @param accel_type Acceleration type to check
     * @return true if acceleration is available
     */
    static bool is_acceleration_available(AccelerationType accel_type);

    /**
     * Get detected QEMU capabilities
     * @return System capabilities
     */
    const Capabilities& get_capabilities() const { return capabilities_; }

    // ========== Disk Management ==========

    /**
     * Create overlay disk for ephemeral VMs
     * @param base_image_path Path to base image
     * @param overlay_path Path for new overlay disk
     * @return Path to created overlay disk
     * @throws SystemError if creation fails
     */
    static std::string create_overlay_disk(const std::string& base_image_path,
                                         const std::string& overlay_path);

    /**
     * Remove overlay disk file
     * @param overlay_path Path to overlay disk
     * @throws SystemError if removal fails
     */
    static void remove_overlay_disk(const std::string& overlay_path);

    /**
     * Validate disk image format and integrity
     * @param image_path Path to disk image
     * @return true if image is valid
     */
    static bool validate_disk_image(const std::string& image_path);

    /**
     * Get detailed disk image information
     * @param image_path Path to disk image
     * @return Disk information
     */
    static DiskInfo get_disk_info(const std::string& image_path);

private:
    Capabilities capabilities_;

    // Command line building helpers
    void add_cpu_configuration(std::vector<std::string>& cmd,
                              const VMConfiguration& config,
                              const QemuOptions& options);
    
    void add_acceleration_configuration(std::vector<std::string>& cmd,
                                      AccelerationType accel_type);
    
    void add_disk_configuration(std::vector<std::string>& cmd,
                               const VMConfiguration& config,
                               const QemuOptions& options);
    
    void add_network_configuration(std::vector<std::string>& cmd,
                                  const VMConfiguration& config,
                                  const QemuOptions& options);
    
    void add_display_configuration(std::vector<std::string>& cmd,
                                  const VMConfiguration& config,
                                  const QemuOptions& options);
    
    void add_serial_configuration(std::vector<std::string>& cmd,
                                 const QemuOptions& options);
    
    void add_boot_configuration(std::vector<std::string>& cmd,
                               const QemuOptions& options);
    
    void add_virtfs_configuration(std::vector<std::string>& cmd,
                                 const std::string& host_path,
                                 const QemuOptions& options);
    
    void add_cloud_init_configuration(std::vector<std::string>& cmd,
                                     const std::string& cloud_init_path);

    // Capability detection helpers
    void detect_qemu_capabilities();
    bool detect_virtfs_support();
    bool detect_vnc_support();
    bool detect_usb_support();
    std::string get_qemu_version();
};

} // namespace scratchpad::infrastructure