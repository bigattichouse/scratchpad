#pragma once

#include "scratchpad/types.hpp"
#include "scratchpad/errors.hpp"
#include <memory>
#include <vector>
#include <optional>
#include <future>
#include <chrono>
#include <filesystem>
#include <map>
#include <string>

namespace scratchpad {

// Forward declarations
class VMConfiguration;

/**
 * QEMU adapter interface for VM management
 * This is a stub header for test compilation
 */
class QemuAdapter {
public:
    struct Options {
        std::chrono::milliseconds startup_timeout{30000};
        std::chrono::milliseconds shutdown_timeout{10000};
        bool enable_kvm = true;
        bool enable_debugging = false;
        std::string qemu_binary_path = "qemu-system-x86_64";
        std::vector<std::string> default_args;
    };

    struct StartResult {
        bool success;
        std::string error_message;
        ProcessId process_id;
    };

    QemuAdapter();
    explicit QemuAdapter(const Options& options);
    virtual ~QemuAdapter() = default;

    // VM lifecycle
    virtual std::future<StartResult> start_vm_async(const VMConfiguration& config);
    virtual std::future<bool> stop_vm_async(const VMId& vm_id);
    virtual std::future<bool> destroy_vm_async(const VMId& vm_id, bool force = false);
    virtual bool is_vm_running(const VMId& vm_id) const;
    virtual VMStatus get_vm_status(const VMId& vm_id) const;

    // Disk operations
    virtual bool create_disk_image(const std::filesystem::path& path, DiskSize size);
    virtual bool clone_disk_image(const std::filesystem::path& source, const std::filesystem::path& dest);
    virtual bool resize_disk_image(const std::filesystem::path& path, DiskSize new_size);

    // Process management
    virtual std::vector<ProcessId> get_running_vm_processes() const;
    virtual bool kill_vm_process(const ProcessId& pid, bool force = false);
    virtual std::optional<std::map<std::string, std::string>> get_process_info(const ProcessId& pid) const;

    // Hardware detection
    virtual AccelerationType detect_available_acceleration() const;
    virtual bool supports_nested_virtualization() const;
    virtual size_t get_max_supported_memory() const;

    // Configuration
    virtual const Options& get_options() const;
    virtual void update_options(const Options& new_options);

private:
    class Impl {
    public:
        virtual ~Impl() = default;
    };
    std::unique_ptr<Impl> impl_;
};

} // namespace scratchpad