#include "infrastructure/qemu/qemu_adapter.hpp"
#include "scratchpad/errors.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>

namespace scratchpad::infrastructure {

QemuAdapter::QemuAdapter() {
    detect_qemu_capabilities();
}

std::vector<std::string> QemuAdapter::build_command_line(const VMConfiguration& config,
                                                         const QemuOptions& options) {
    std::vector<std::string> cmd;
    
    // Base executable
    cmd.push_back(detect_qemu_executable());
    
    // VM name
    cmd.push_back("-name");
    cmd.push_back(config.vm_id().value());
    
    // Machine type
    cmd.push_back("-machine");
    cmd.push_back("pc");
    
    // Memory
    cmd.push_back("-m");
    cmd.push_back(config.memory().to_string());
    
    // CPU configuration
    add_cpu_configuration(cmd, config, options);
    
    // Acceleration
    add_acceleration_configuration(cmd, options.acceleration_type);
    
    // Disk configuration
    add_disk_configuration(cmd, config, options);
    
    // Network configuration
    add_network_configuration(cmd, config, options);
    
    // Display configuration
    add_display_configuration(cmd, config, options);
    
    // Serial and monitor configuration
    add_serial_configuration(cmd, options);
    
    // Boot configuration
    add_boot_configuration(cmd, options);
    
    // Work directory mounting
    if (config.has_work_directory()) {
        add_virtfs_configuration(cmd, config.work_directory(), options);
    }
    
    // Cloud-init configuration
    if (!options.cloud_init_iso_path.empty()) {
        add_cloud_init_configuration(cmd, options.cloud_init_iso_path);
    }
    
    return cmd;
}

std::string QemuAdapter::detect_qemu_executable() {
    static const std::vector<std::string> possible_executables = {
        "qemu-system-x86_64",
        "/usr/bin/qemu-system-x86_64",
        "/usr/local/bin/qemu-system-x86_64",
        "/opt/homebrew/bin/qemu-system-x86_64",  // macOS ARM Homebrew
        "/usr/libexec/qemu-system-x86_64"
    };
    
    for (const auto& executable : possible_executables) {
        if (std::filesystem::exists(executable)) {
            return executable;
        }
        
        // Check if it's in PATH
        std::string command = "which " + executable + " 2>/dev/null";
        if (std::system(command.c_str()) == 0) {
            return executable;
        }
    }
    
    THROW_SYSTEM_ERROR(ErrorCode::QemuNotFound, "QEMU executable not found");
}

AccelerationType QemuAdapter::detect_best_acceleration() {
    // Try accelerations in order of preference
    if (is_acceleration_available(AccelerationType::KVM)) {
        return AccelerationType::KVM;
    }
    
    if (is_acceleration_available(AccelerationType::HVF)) {
        return AccelerationType::HVF;
    }
    
    if (is_acceleration_available(AccelerationType::WHPX)) {
        return AccelerationType::WHPX;
    }
    
    // Fall back to TCG (always available)
    return AccelerationType::TCG;
}

bool QemuAdapter::is_acceleration_available(AccelerationType accel_type) {
    switch (accel_type) {
        case AccelerationType::KVM:
            // Check for /dev/kvm on Linux
            return std::filesystem::exists("/dev/kvm") && 
                   std::filesystem::is_char_file("/dev/kvm");
            
        case AccelerationType::HVF:
            // Check if running on macOS with HVF support
            #ifdef __APPLE__
            // TODO: Add proper HVF detection
            return true; // Assume available on macOS for now
            #else
            return false;
            #endif
            
        case AccelerationType::WHPX:
            // Check if running on Windows with WHPX support
            #ifdef _WIN32
            // TODO: Add proper WHPX detection
            return true; // Assume available on Windows for now
            #else
            return false;
            #endif
            
        case AccelerationType::TCG:
            return true; // TCG is always available
    }
    
    return false;
}

void QemuAdapter::detect_qemu_capabilities() {
    capabilities_.qemu_executable = detect_qemu_executable();
    capabilities_.best_acceleration = detect_best_acceleration();
    
    // Detect supported accelerations
    for (auto accel : {AccelerationType::KVM, AccelerationType::HVF, 
                       AccelerationType::WHPX, AccelerationType::TCG}) {
        if (is_acceleration_available(accel)) {
            capabilities_.supported_accelerations.push_back(accel);
        }
    }
    
    // Detect other capabilities
    capabilities_.supports_virtfs = detect_virtfs_support();
    capabilities_.supports_vnc = detect_vnc_support();
    capabilities_.supports_usb = detect_usb_support();
    
    // Get version information
    capabilities_.version = get_qemu_version();
}

void QemuAdapter::add_cpu_configuration(std::vector<std::string>& cmd,
                                        const VMConfiguration& config,
                                        const QemuOptions& options) {
    // CPU type
    cmd.push_back("-cpu");
    if (options.acceleration_type == AccelerationType::TCG) {
        cmd.push_back("qemu64"); // Generic CPU for TCG
    } else {
        cmd.push_back("host"); // Use host CPU features with acceleration
    }
    
    // SMP configuration
    cmd.push_back("-smp");
    cmd.push_back(std::to_string(config.cpu_cores()));
}

void QemuAdapter::add_acceleration_configuration(std::vector<std::string>& cmd,
                                                 AccelerationType accel_type) {
    cmd.push_back("-accel");
    
    switch (accel_type) {
        case AccelerationType::KVM:
            cmd.push_back("kvm");
            break;
        case AccelerationType::HVF:
            cmd.push_back("hvf");
            break;
        case AccelerationType::WHPX:
            cmd.push_back("whpx");
            break;
        case AccelerationType::TCG:
            cmd.push_back("tcg");
            break;
    }
}

void QemuAdapter::add_disk_configuration(std::vector<std::string>& cmd,
                                         const VMConfiguration& config,
                                         const QemuOptions& options) {
    cmd.push_back("-drive");
    
    std::string drive_config = "file=" + options.disk_image_path;
    drive_config += ",format=qcow2";
    drive_config += ",if=virtio";
    
    // Add snapshot option for ephemeral mode
    if (config.disk_mode() == DiskMode::Ephemeral) {
        drive_config += ",snapshot=on";
    }
    
    cmd.push_back(drive_config);
}

void QemuAdapter::add_network_configuration(std::vector<std::string>& cmd,
                                            const VMConfiguration& config,
                                            const QemuOptions& options) {
    if (!config.networking_enabled()) {
        cmd.push_back("-netdev");
        cmd.push_back("none");
        return;
    }
    
    // Network device
    cmd.push_back("-netdev");
    std::string netdev_config = "user,id=net0";
    
    // SSH port forwarding
    if (config.ssh_enabled() && options.ssh_port != 0) {
        netdev_config += ",hostfwd=tcp::" + std::to_string(options.ssh_port) + "-:22";
    }
    
    // VNC port forwarding (if enabled)
    if (config.vnc_enabled() && options.vnc_port != 0) {
        netdev_config += ",hostfwd=tcp::" + std::to_string(options.vnc_port) + "-:5900";
    }
    
    cmd.push_back(netdev_config);
    
    // Network interface
    cmd.push_back("-device");
    cmd.push_back("virtio-net-pci,netdev=net0");
}

void QemuAdapter::add_display_configuration(std::vector<std::string>& cmd,
                                           const VMConfiguration& config,
                                           const QemuOptions& options) {
    if (config.vnc_enabled() && options.vnc_port != 0) {
        cmd.push_back("-vnc");
        int display_num = options.vnc_port - 5900;
        cmd.push_back(":" + std::to_string(display_num));
    } else {
        cmd.push_back("-display");
        cmd.push_back("none");
    }
}

void QemuAdapter::add_serial_configuration(std::vector<std::string>& cmd,
                                          const QemuOptions& options) {
    if (options.enable_serial_output) {
        cmd.push_back("-serial");
        if (!options.serial_log_path.empty()) {
            cmd.push_back("file:" + options.serial_log_path);
        } else {
            cmd.push_back("stdio");
        }
    } else {
        cmd.push_back("-serial");
        cmd.push_back("null");
    }
    
    // Monitor
    cmd.push_back("-monitor");
    cmd.push_back("none");
}

void QemuAdapter::add_boot_configuration(std::vector<std::string>& cmd,
                                        const QemuOptions& options) {
    cmd.push_back("-boot");
    cmd.push_back("c"); // Boot from hard disk
    
    // BIOS/UEFI configuration
    if (!options.firmware_path.empty()) {
        cmd.push_back("-bios");
        cmd.push_back(options.firmware_path);
    }
}

void QemuAdapter::add_virtfs_configuration(std::vector<std::string>& cmd,
                                          const std::string& host_path,
                                          const QemuOptions& options) {
    if (!capabilities_.supports_virtfs) {
        return; // Skip if not supported
    }
    
    cmd.push_back("-virtfs");
    
    std::string virtfs_config = "local,path=" + host_path;
    virtfs_config += ",mount_tag=workdir";
    virtfs_config += ",security_model=mapped-xattr";
    virtfs_config += ",id=workdir";
    
    cmd.push_back(virtfs_config);
}

void QemuAdapter::add_cloud_init_configuration(std::vector<std::string>& cmd,
                                               const std::string& cloud_init_path) {
    cmd.push_back("-drive");
    cmd.push_back("file=" + cloud_init_path + ",format=raw,if=virtio,readonly=on");
}

bool QemuAdapter::detect_virtfs_support() {
    // VirtFS requires 9p support in QEMU
    // For simplicity, assume it's supported if QEMU is modern enough
    std::string version = get_qemu_version();
    
    // Parse version and check if >= 4.0 (when 9p became stable)
    // This is a simplified check
    return !version.empty();
}

bool QemuAdapter::detect_vnc_support() {
    // VNC is generally supported in all QEMU builds
    return true;
}

bool QemuAdapter::detect_usb_support() {
    // USB is generally supported in all QEMU builds
    return true;
}

std::string QemuAdapter::get_qemu_version() {
    try {
        std::string command = capabilities_.qemu_executable + " --version 2>/dev/null";
        
        // Use popen to capture output
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
        if (!pipe) {
            return "unknown";
        }
        
        std::string result;
        char buffer[128];
        while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
            result += buffer;
        }
        
        // Extract version from first line
        size_t newline = result.find('\n');
        if (newline != std::string::npos) {
            result = result.substr(0, newline);
        }
        
        return result;
    } catch (...) {
        return "unknown";
    }
}

std::string QemuAdapter::create_overlay_disk(const std::string& base_image_path,
                                           const std::string& overlay_path) {
    // Create qemu-img command to create overlay
    std::string command = "qemu-img create -f qcow2 -b ";
    command += base_image_path;
    command += " -F qcow2 ";
    command += overlay_path;
    
    int result = std::system(command.c_str());
    if (result != 0) {
        THROW_SYSTEM_ERROR(ErrorCode::FileSystemError,
                          "Failed to create overlay disk: " + overlay_path);
    }
    
    return overlay_path;
}

void QemuAdapter::remove_overlay_disk(const std::string& overlay_path) {
    try {
        std::filesystem::remove(overlay_path);
    } catch (const std::filesystem::filesystem_error& e) {
        THROW_SYSTEM_ERROR(ErrorCode::FileSystemError,
                          "Failed to remove overlay disk: " + std::string(e.what()));
    }
}

bool QemuAdapter::validate_disk_image(const std::string& image_path) {
    if (!std::filesystem::exists(image_path)) {
        return false;
    }
    
    // Use qemu-img info to validate the image
    std::string command = "qemu-img info ";
    command += image_path;
    command += " >/dev/null 2>&1";
    
    return std::system(command.c_str()) == 0;
}

QemuAdapter::DiskInfo QemuAdapter::get_disk_info(const std::string& image_path) {
    DiskInfo info;
    info.path = image_path;
    info.exists = std::filesystem::exists(image_path);
    
    if (!info.exists) {
        return info;
    }
    
    try {
        auto file_size = std::filesystem::file_size(image_path);
        info.size_bytes = file_size;
    } catch (...) {
        info.size_bytes = 0;
    }
    
    // Get detailed info using qemu-img info
    try {
        std::string command = "qemu-img info --output=json ";
        command += image_path;
        command += " 2>/dev/null";
        
        std::unique_ptr<FILE, decltype(&pclose)> pipe(popen(command.c_str(), "r"), pclose);
        if (pipe) {
            std::string json_output;
            char buffer[1024];
            while (fgets(buffer, sizeof(buffer), pipe.get()) != nullptr) {
                json_output += buffer;
            }
            
            // Parse basic info from JSON (simplified)
            if (json_output.find("\"format\": \"qcow2\"") != std::string::npos) {
                info.format = "qcow2";
            } else if (json_output.find("\"format\": \"raw\"") != std::string::npos) {
                info.format = "raw";
            }
            
            info.valid = !info.format.empty();
        }
    } catch (...) {
        info.valid = false;
    }
    
    return info;
}

// QemuOptions implementation

QemuOptions QemuOptions::create_default() {
    QemuOptions options;
    options.acceleration_type = AccelerationType::TCG; // Safe default
    options.ssh_port = 0; // Will be allocated
    options.vnc_port = 0; // Will be allocated
    options.enable_serial_output = false;
    return options;
}

QemuOptions QemuOptions::create_for_vm(const VMConfiguration& config) {
    QemuOptions options = create_default();
    
    // Set acceleration based on system capabilities
    QemuAdapter adapter;
    options.acceleration_type = adapter.get_capabilities().best_acceleration;
    
    // Configure based on VM config
    if (config.vnc_enabled()) {
        // VNC will be configured by caller with allocated port
    }
    
    return options;
}

bool QemuOptions::validate() const {
    // Check required paths exist
    if (!disk_image_path.empty() && !std::filesystem::exists(disk_image_path)) {
        return false;
    }
    
    if (!cloud_init_iso_path.empty() && !std::filesystem::exists(cloud_init_iso_path)) {
        return false;
    }
    
    if (!firmware_path.empty() && !std::filesystem::exists(firmware_path)) {
        return false;
    }
    
    // Validate port numbers
    if (ssh_port != 0 && (ssh_port < 1024 || ssh_port > 65535)) {
        return false;
    }
    
    if (vnc_port != 0 && (vnc_port < 5900 || vnc_port > 5999)) {
        return false;
    }
    
    return true;
}

} // namespace scratchpad::infrastructure