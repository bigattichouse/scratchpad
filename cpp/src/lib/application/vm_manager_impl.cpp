#include "vm_manager_impl.hpp"
#include "scratchpad/domain/vm/vm_id.hpp"
#include "scratchpad/domain/vm/vm_configuration.hpp"
#include "scratchpad/errors.hpp"

#include <algorithm>
#include <filesystem>
#include <random>

namespace scratchpad {

VMManagerImpl::VMManagerImpl() 
    : qemu_adapter_(std::make_unique<QemuAdapter>())
    , ssh_client_(std::make_unique<SSHClient>())
    , logger_(Logger::instance()) {
    
    logger_.info("Initializing VM Manager");
    
    // Validate QEMU availability
    if (!qemu_adapter_->is_qemu_available()) {
        THROW_SYSTEM_ERROR(ErrorCode::QemuNotFound, 
                          "QEMU not found on system. Please install QEMU.", 0);
    }
    
    start_monitoring_thread();
    logger_.info("VM Manager initialized successfully");
}

VMManagerImpl::~VMManagerImpl() {
    logger_.info("Shutting down VM Manager");
    
    stop_monitoring_thread();
    
    // Stop all running VMs
    std::vector<VMId> vm_ids;
    {
        std::shared_lock lock(vms_mutex_);
        vm_ids.reserve(vms_.size());
        for (const auto& [id, state] : vms_) {
            if (state->vm->status() == VMStatus::Running) {
                vm_ids.push_back(id);
            }
        }
    }
    
    for (const auto& vm_id : vm_ids) {
        try {
            stop_vm(vm_id);
        } catch (const std::exception& e) {
            logger_.warning("Failed to stop VM {} during shutdown: {}", vm_id.value(), e.what());
        }
    }
    
    logger_.info("VM Manager shutdown complete");
}

VMId VMManagerImpl::create_vm(const CreateParams& params) {
    logger_.info("Creating VM with image: {}", params.image_name);
    
    validate_create_params(params);
    validate_resource_requirements(params);
    
    // Generate unique VM ID
    VMId vm_id = VMId::generate();
    
    // Build VM configuration
    VMConfiguration config = build_vm_configuration(params);
    
    // Allocate SSH port and configure it
    PortNumber ssh_port = allocate_ssh_port();
    NetworkConfiguration net_config = config.network_config();
    net_config.ssh_port = ssh_port;
    config.set_network_configuration(net_config);
    
    // Create VM entity (VMConfiguration already contains VMId)
    auto vm = std::make_unique<VirtualMachine>(config);
    auto vm_state = std::make_unique<VMState>(std::move(vm));
    
    {
        std::unique_lock lock(vms_mutex_);
        vms_[vm_id] = std::move(vm_state);
    }
    
    logger_.info("VM created successfully: {}", vm_id.value());
    notify_status_change(vm_id, VMStatus::Stopped, VMStatus::Stopped);
    
    return vm_id;
}

void VMManagerImpl::destroy_vm(const VMId& vm_id) {
    logger_.info("Destroying VM: {}", vm_id.value());
    
    ensure_vm_exists(vm_id);
    
    VMState* state = find_vm_state(vm_id);
    if (!state) {
        THROW_VM_ERROR(ErrorCode::VMNotFound, "VM not found", vm_id);
    }
    
    {
        std::lock_guard state_lock(state->state_mutex);
        
        // Stop VM if running
        if (state->vm->status() == VMStatus::Running) {
            stop_qemu_process(*state);
        }
        
        // Cleanup resources
        cleanup_vm_resources(vm_id);
    }
    
    // Remove from manager
    {
        std::unique_lock lock(vms_mutex_);
        vms_.erase(vm_id);
    }
    
    logger_.info("VM destroyed: {}", vm_id.value());
}

void VMManagerImpl::start_vm(const VMId& vm_id) {
    logger_.info("Starting VM: {}", vm_id.value());
    
    ensure_vm_exists(vm_id);
    
    VMState* state = find_vm_state(vm_id);
    std::lock_guard state_lock(state->state_mutex);
    
    if (state->vm->status() == VMStatus::Running) {
        logger_.warning("VM {} is already running", vm_id.value());
        return;
    }
    
    try {
        state->vm->set_status(VMStatus::Starting);
        notify_status_change(vm_id, VMStatus::Stopped, VMStatus::Starting);
        
        start_qemu_process(*state);
        establish_ssh_connection(*state);
        
        state->vm->set_status(VMStatus::Running);
        notify_status_change(vm_id, VMStatus::Starting, VMStatus::Running);
        
        logger_.info("VM started successfully: {}", vm_id.value());
        
    } catch (const std::exception& e) {
        logger_.error("Failed to start VM {}: {}", vm_id.value(), e.what());
        state->vm->set_status(VMStatus::Error);
        notify_status_change(vm_id, VMStatus::Starting, VMStatus::Error);
        throw;
    }
}

void VMManagerImpl::stop_vm(const VMId& vm_id) {
    logger_.info("Stopping VM: {}", vm_id.value());
    
    ensure_vm_exists(vm_id);
    ensure_vm_running(vm_id);
    
    VMState* state = find_vm_state(vm_id);
    std::lock_guard state_lock(state->state_mutex);
    
    try {
        state->vm->set_status(VMStatus::Stopping);
        notify_status_change(vm_id, VMStatus::Running, VMStatus::Stopping);
        
        close_ssh_connection(*state);
        stop_qemu_process(*state);
        
        state->vm->set_status(VMStatus::Stopped);
        notify_status_change(vm_id, VMStatus::Stopping, VMStatus::Stopped);
        
        logger_.info("VM stopped successfully: {}", vm_id.value());
        
    } catch (const std::exception& e) {
        logger_.error("Failed to stop VM {}: {}", vm_id.value(), e.what());
        state->vm->set_status(VMStatus::Error);
        notify_status_change(vm_id, VMStatus::Stopping, VMStatus::Error);
        throw;
    }
}

void VMManagerImpl::restart_vm(const VMId& vm_id) {
    logger_.info("Restarting VM: {}", vm_id.value());
    
    stop_vm(vm_id);
    
    // Wait a moment for cleanup
    std::this_thread::sleep_for(std::chrono::milliseconds(1000));
    
    start_vm(vm_id);
}

VMStatus VMManagerImpl::get_vm_status(const VMId& vm_id) const {
    ensure_vm_exists(vm_id);
    
    VMState* state = find_vm_state(vm_id);
    std::shared_lock state_lock(state->state_mutex);
    
    return state->vm->status();
}

VMInfo VMManagerImpl::get_vm_info(const VMId& vm_id) const {
    ensure_vm_exists(vm_id);
    
    VMState* state = find_vm_state(vm_id);
    std::shared_lock state_lock(state->state_mutex);
    
    VMInfo info;
    info.vm_id = vm_id;
    info.status = state->vm->status();
    info.configuration = state->vm->configuration();
    info.statistics = state->vm->statistics();
    info.created_at = state->vm->created_at();
    
    if (state->vm->started_at().has_value()) {
        info.started_at = state->vm->started_at().value();
    }
    
    return info;
}

std::vector<VMId> VMManagerImpl::list_vms() const {
    std::shared_lock lock(vms_mutex_);
    
    std::vector<VMId> vm_ids;
    vm_ids.reserve(vms_.size());
    
    for (const auto& [id, state] : vms_) {
        vm_ids.push_back(id);
    }
    
    return vm_ids;
}

std::vector<VMInfo> VMManagerImpl::list_vm_info() const {
    std::shared_lock lock(vms_mutex_);
    
    std::vector<VMInfo> vm_infos;
    vm_infos.reserve(vms_.size());
    
    for (const auto& [id, state] : vms_) {
        std::shared_lock state_lock(state->state_mutex);
        
        VMInfo info;
        info.vm_id = id;
        info.status = state->vm->status();
        info.configuration = state->vm->configuration();
        info.statistics = state->vm->statistics();
        info.created_at = state->vm->created_at();
        
        if (state->vm->started_at().has_value()) {
            info.started_at = state->vm->started_at().value();
        }
        
        vm_infos.push_back(info);
    }
    
    return vm_infos;
}

CommandResult VMManagerImpl::execute_command(const VMId& vm_id, const ExecuteParams& params) {
    logger_.debug("Executing command in VM {}: {}", vm_id.value(), params.command);
    
    ensure_vm_exists(vm_id);
    ensure_vm_running(vm_id);
    validate_execute_params(params);
    
    VMState* state = find_vm_state(vm_id);
    std::shared_lock state_lock(state->state_mutex);
    
    if (!state->ssh_connection || !state->ssh_connection->is_connected()) {
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed, 
                       "SSH connection not available for VM", "localhost", 0);
    }
    
    return ssh_client_->execute_command(*state->ssh_connection, params);
}

std::future<CommandResult> VMManagerImpl::execute_command_async(const VMId& vm_id, const ExecuteParams& params) {
    return std::async(std::launch::async, [this, vm_id, params]() {
        return execute_command(vm_id, params);
    });
}

void VMManagerImpl::copy_file_to_vm(const VMId& vm_id, const CopyParams& params) {
    logger_.debug("Copying file to VM {}: {} -> {}", vm_id.value(), params.source, params.destination);
    
    ensure_vm_exists(vm_id);
    ensure_vm_running(vm_id);
    validate_copy_params(params);
    
    VMState* state = find_vm_state(vm_id);
    std::shared_lock state_lock(state->state_mutex);
    
    if (!state->ssh_connection || !state->ssh_connection->is_connected()) {
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed, 
                       "SSH connection not available for VM", "localhost", 0);
    }
    
    ssh_client_->copy_file_to_remote(*state->ssh_connection, params.source, params.destination);
}

void VMManagerImpl::copy_file_from_vm(const VMId& vm_id, const CopyParams& params) {
    logger_.debug("Copying file from VM {}: {} -> {}", vm_id.value(), params.source, params.destination);
    
    ensure_vm_exists(vm_id);
    ensure_vm_running(vm_id);
    validate_copy_params(params);
    
    VMState* state = find_vm_state(vm_id);
    std::shared_lock state_lock(state->state_mutex);
    
    if (!state->ssh_connection || !state->ssh_connection->is_connected()) {
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed, 
                       "SSH connection not available for VM", "localhost", 0);
    }
    
    ssh_client_->copy_file_from_remote(*state->ssh_connection, params.source, params.destination);
}

void VMManagerImpl::set_status_callback(StatusCallback callback) {
    std::lock_guard lock(callback_mutex_);
    status_callback_ = std::move(callback);
}

void VMManagerImpl::remove_status_callback() {
    std::lock_guard lock(callback_mutex_);
    status_callback_ = nullptr;
}

ResourceUsage VMManagerImpl::get_resource_usage() const {
    std::shared_lock lock(vms_mutex_);
    
    ResourceUsage usage{};
    
    for (const auto& [id, state] : vms_) {
        std::shared_lock state_lock(state->state_mutex);
        
        if (state->vm->status() == VMStatus::Running) {
            usage.running_vms++;
            usage.allocated_memory += state->vm->configuration().memory().bytes;
            usage.allocated_disk += state->vm->configuration().disk_size().bytes;
        }
        usage.total_vms++;
    }
    
    std::lock_guard ports_lock(ports_mutex_);
    usage.allocated_ports.assign(allocated_ports_.begin(), allocated_ports_.end());
    
    return usage;
}

std::vector<PortNumber> VMManagerImpl::get_allocated_ports() const {
    std::lock_guard lock(ports_mutex_);
    return std::vector<PortNumber>(allocated_ports_.begin(), allocated_ports_.end());
}

// Private helper methods

VMManagerImpl::VMState* VMManagerImpl::find_vm_state(const VMId& vm_id) const {
    std::shared_lock lock(vms_mutex_);
    auto it = vms_.find(vm_id);
    return (it != vms_.end()) ? it->second.get() : nullptr;
}

void VMManagerImpl::ensure_vm_exists(const VMId& vm_id) const {
    if (!find_vm_state(vm_id)) {
        THROW_VM_ERROR(ErrorCode::VMNotFound, "VM not found", vm_id);
    }
}

void VMManagerImpl::ensure_vm_running(const VMId& vm_id) const {
    VMState* state = find_vm_state(vm_id);
    if (!state || state->vm->status() != VMStatus::Running) {
        THROW_VM_ERROR(ErrorCode::VMNotRunning, "VM is not running", vm_id);
    }
}

void VMManagerImpl::cleanup_vm_resources(const VMId& vm_id) {
    VMState* state = find_vm_state(vm_id);
    if (!state) return;
    
    // Deallocate SSH port
    PortNumber ssh_port = state->vm->configuration().network_config().ssh_port;
    if (ssh_port > 0) {
        deallocate_ssh_port(ssh_port);
    }
    
    // Clean up any VM-specific files/directories
    // This would include overlay disks, temporary files, etc.
}

void VMManagerImpl::start_qemu_process(VMState& state) {
    logger_.debug("Starting QEMU process for VM: {}", state.vm->id().value());
    
    auto qemu_options = qemu_adapter_->build_options(state.vm->configuration());
    auto command_line = qemu_adapter_->build_command_line(state.vm->configuration(), qemu_options);
    
    state.process = std::make_unique<QemuProcess>(state.vm->id(), command_line);
    state.process->start();
    
    // Wait for QEMU to start up
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
}

void VMManagerImpl::stop_qemu_process(VMState& state) {
    if (state.process) {
        logger_.debug("Stopping QEMU process for VM: {}", state.vm->id().value());
        state.process->stop();
        state.process.reset();
    }
}

void VMManagerImpl::establish_ssh_connection(VMState& state) {
    logger_.debug("Establishing SSH connection for VM: {}", state.vm->id().value());
    
    SSHCredentials credentials = SSHCredentials::create_default(
        "scratchpad", 
        state.vm->configuration().network_config().ssh_port
    );
    
    state.ssh_connection = ssh_client_->create_connection(credentials);
    
    // Try to connect with retries
    int max_retries = 30;
    for (int i = 0; i < max_retries; ++i) {
        try {
            state.ssh_connection->connect();
            if (state.ssh_connection->is_connected()) {
                logger_.debug("SSH connection established for VM: {}", state.vm->id().value());
                return;
            }
        } catch (const std::exception& e) {
            if (i == max_retries - 1) {
                THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed, 
                               "Failed to establish SSH connection after retries: " + std::string(e.what()),
                               "localhost", state.vm->configuration().network_config().ssh_port);
            }
        }
        
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    }
}

void VMManagerImpl::close_ssh_connection(VMState& state) {
    if (state.ssh_connection) {
        logger_.debug("Closing SSH connection for VM: {}", state.vm->id().value());
        state.ssh_connection->disconnect();
        state.ssh_connection.reset();
    }
}

PortNumber VMManagerImpl::allocate_ssh_port() {
    std::lock_guard lock(ports_mutex_);
    
    for (PortNumber port = base_ssh_port_; port <= max_ssh_port_; ++port) {
        if (allocated_ports_.find(port) == allocated_ports_.end()) {
            allocated_ports_.insert(port);
            return port;
        }
    }
    
    THROW_RESOURCE_ERROR(ErrorCode::PortUnavailable, 
                        "No available SSH ports in range", "ssh_port");
}

void VMManagerImpl::deallocate_ssh_port(PortNumber port) {
    std::lock_guard lock(ports_mutex_);
    allocated_ports_.erase(port);
}

void VMManagerImpl::validate_resource_requirements(const CreateParams& params) {
    // Check memory requirements
    if (params.memory.bytes < MemoryAmount::from_string("256M").bytes) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument, 
                           "Minimum memory requirement is 256MB", "memory");
    }
    
    // Check disk space requirements  
    if (params.disk_size.bytes < DiskSize::from_string("1G").bytes) {
        THROW_RESOURCE_ERROR(ErrorCode::InvalidArgument,
                           "Minimum disk size requirement is 1GB", "disk");
    }
}

void VMManagerImpl::start_monitoring_thread() {
    monitoring_active_ = true;
    monitoring_thread_ = std::thread(&VMManagerImpl::monitor_vm_status, this);
}

void VMManagerImpl::stop_monitoring_thread() {
    monitoring_active_ = false;
    if (monitoring_thread_.joinable()) {
        monitoring_thread_.join();
    }
}

void VMManagerImpl::monitor_vm_status() {
    while (monitoring_active_) {
        try {
            std::vector<VMId> vm_ids;
            
            {
                std::shared_lock lock(vms_mutex_);
                vm_ids.reserve(vms_.size());
                for (const auto& [id, state] : vms_) {
                    vm_ids.push_back(id);
                }
            }
            
            for (const auto& vm_id : vm_ids) {
                VMState* state = find_vm_state(vm_id);
                if (!state) continue;
                
                std::lock_guard state_lock(state->state_mutex);
                
                // Check if QEMU process is still running
                if (state->vm->status() == VMStatus::Running && state->process) {
                    if (!state->process->is_running()) {
                        logger_.warning("QEMU process died for VM: {}", vm_id.value());
                        VMStatus old_status = state->vm->status();
                        state->vm->set_status(VMStatus::Error);
                        notify_status_change(vm_id, old_status, VMStatus::Error);
                    }
                }
            }
            
        } catch (const std::exception& e) {
            logger_.error("Error in VM monitoring thread: {}", e.what());
        }
        
        std::this_thread::sleep_for(status_check_interval_);
    }
}

void VMManagerImpl::notify_status_change(const VMId& vm_id, VMStatus old_status, VMStatus new_status) {
    std::lock_guard lock(callback_mutex_);
    if (status_callback_) {
        try {
            std::string status_message = "Status changed from " + std::to_string(static_cast<int>(old_status)) + 
                                        " to " + std::to_string(static_cast<int>(new_status));
            status_callback_(vm_id, new_status, status_message);
        } catch (const std::exception& e) {
            logger_.error("Error in status callback: {}", e.what());
        }
    }
}

VMConfiguration VMManagerImpl::build_vm_configuration(const CreateParams& params) {
    return VMConfiguration::create_for_linux(
        params.image_name,
        params.memory,
        params.disk_size,
        params.cpu_cores
    );
}

void VMManagerImpl::validate_create_params(const CreateParams& params) {
    if (params.image_name.empty()) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument, "Image name cannot be empty", "");
    }
    
    if (params.cpu_cores == 0) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument, "CPU cores must be greater than 0", "");
    }
    
    if (params.cpu_cores > std::thread::hardware_concurrency()) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument, 
                      "CPU cores cannot exceed system limit: " + 
                      std::to_string(std::thread::hardware_concurrency()), "");
    }
}

void VMManagerImpl::validate_execute_params(const ExecuteParams& params) {
    if (params.command.empty()) {
        THROW_SSH_ERROR(ErrorCode::InvalidArgument, "Command cannot be empty", "localhost", 0);
    }
}

void VMManagerImpl::validate_copy_params(const CopyParams& params) {
    if (params.source.empty()) {
        THROW_SSH_ERROR(ErrorCode::InvalidArgument, "Source path cannot be empty", "localhost", 0);
    }
    
    if (params.destination.empty()) {
        THROW_SSH_ERROR(ErrorCode::InvalidArgument, "Destination path cannot be empty", "localhost", 0);
    }
}

} // namespace scratchpad