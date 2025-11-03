#pragma once

#include "scratchpad/vm_manager.hpp"
#include "domain/vm/virtual_machine.hpp"
#include "domain/process/qemu_process.hpp"
#include "domain/communication/ssh_connection.hpp"
#include "infrastructure/qemu/qemu_adapter.hpp"
#include "infrastructure/ssh/ssh_client.hpp"
#include "logging/logger.hpp"

#include <memory>
#include <unordered_map>
#include <mutex>
#include <future>
#include <thread>

namespace scratchpad {

class VMManagerImpl : public VMManager {
public:
    VMManagerImpl();
    ~VMManagerImpl() override;

    // VM Lifecycle Management
    VMId create_vm(const CreateParams& params) override;
    void destroy_vm(const VMId& vm_id) override;
    void start_vm(const VMId& vm_id) override;
    void stop_vm(const VMId& vm_id) override;
    void restart_vm(const VMId& vm_id) override;

    // VM Status and Information
    VMStatus get_vm_status(const VMId& vm_id) const override;
    VMInfo get_vm_info(const VMId& vm_id) const override;
    std::vector<VMId> list_vms() const override;
    std::vector<VMInfo> list_vm_info() const override;

    // Command Execution
    CommandResult execute_command(const VMId& vm_id, const ExecuteParams& params) override;
    std::future<CommandResult> execute_command_async(const VMId& vm_id, const ExecuteParams& params) override;

    // File Operations
    void copy_file_to_vm(const VMId& vm_id, const CopyParams& params) override;
    void copy_file_from_vm(const VMId& vm_id, const CopyParams& params) override;

    // VM Monitoring
    void set_status_callback(StatusCallback callback) override;
    void remove_status_callback() override;

    // Resource Management
    ResourceUsage get_resource_usage() const override;
    std::vector<PortNumber> get_allocated_ports() const override;

private:
    struct VMState {
        std::unique_ptr<VirtualMachine> vm;
        std::unique_ptr<QemuProcess> process;
        std::unique_ptr<SSHConnection> ssh_connection;
        std::mutex state_mutex;
        
        VMState(std::unique_ptr<VirtualMachine> vm_ptr)
            : vm(std::move(vm_ptr)) {}
    };

    // Internal VM management
    VMState* find_vm_state(const VMId& vm_id) const;
    void ensure_vm_exists(const VMId& vm_id) const;
    void ensure_vm_running(const VMId& vm_id) const;
    void cleanup_vm_resources(const VMId& vm_id);

    // VM lifecycle helpers
    void start_qemu_process(VMState& state);
    void stop_qemu_process(VMState& state);
    void establish_ssh_connection(VMState& state);
    void close_ssh_connection(VMState& state);

    // Resource allocation
    PortNumber allocate_ssh_port();
    void deallocate_ssh_port(PortNumber port);
    void validate_resource_requirements(const CreateParams& params);

    // Status monitoring
    void start_monitoring_thread();
    void stop_monitoring_thread();
    void monitor_vm_status();
    void notify_status_change(const VMId& vm_id, VMStatus old_status, VMStatus new_status);

    // Configuration and validation
    VMConfiguration build_vm_configuration(const CreateParams& params);
    void validate_create_params(const CreateParams& params);
    void validate_execute_params(const ExecuteParams& params);
    void validate_copy_params(const CopyParams& params);

    // Member variables
    mutable std::shared_mutex vms_mutex_;
    std::unordered_map<VMId, std::unique_ptr<VMState>> vms_;
    
    std::unique_ptr<QemuAdapter> qemu_adapter_;
    std::unique_ptr<SSHClient> ssh_client_;
    
    StatusCallback status_callback_;
    std::mutex callback_mutex_;
    
    // Resource tracking
    std::set<PortNumber> allocated_ports_;
    mutable std::mutex ports_mutex_;
    
    // Monitoring
    std::atomic<bool> monitoring_active_{false};
    std::thread monitoring_thread_;
    
    // Configuration
    PortNumber base_ssh_port_{22000};
    PortNumber max_ssh_port_{22999};
    std::chrono::milliseconds status_check_interval_{5000};
    
    Logger& logger_;
};

} // namespace scratchpad