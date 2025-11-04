#include "scratchpad/domain/vm/virtual_machine.hpp"
#include "scratchpad/errors.hpp"
#include <chrono>
#include <algorithm>

namespace scratchpad {

VirtualMachine::VirtualMachine(const VMConfiguration& config)
    : config_(config)
    , status_(VMStatus::Stopped)
    , process_id_(0)
    , created_at_(std::chrono::system_clock::now())
    , last_status_change_(created_at_)
    , start_count_(0)
    , total_uptime_(std::chrono::milliseconds::zero())
    , current_resource_usage_({})
    , is_persistent_(false) {
    
    // Validate configuration
    config_.validate();
}

VirtualMachine::VirtualMachine(VMConfiguration&& config)
    : config_(std::move(config))
    , status_(VMStatus::Stopped)
    , process_id_(0)
    , created_at_(std::chrono::system_clock::now())
    , last_status_change_(created_at_)
    , start_count_(0)
    , total_uptime_(std::chrono::milliseconds::zero())
    , current_resource_usage_({})
    , is_persistent_(false) {
    
    // Validate configuration
    config_.validate();
}

void VirtualMachine::set_status(VMStatus new_status) {
    if (new_status == status_) {
        return; // No change
    }
    
    auto now = std::chrono::system_clock::now();
    
    // Update uptime if transitioning from running to non-running
    if (status_ == VMStatus::Running && new_status != VMStatus::Running) {
        auto session_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_status_change_
        );
        total_uptime_ += session_duration;
    }
    
    // Validate status transitions
    if (!is_valid_status_transition(status_, new_status)) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Invalid status transition from " + 
                      std::string(status_to_string(status_)) + " to " +
                      std::string(status_to_string(new_status)),
                      config_.vm_id().value());
    }
    
    VMStatus old_status = status_;
    status_ = new_status;
    last_status_change_ = now;
    
    // Update counters
    if (new_status == VMStatus::Running && old_status != VMStatus::Running) {
        start_count_++;
        last_started_at_ = now;
    } else if (new_status == VMStatus::Stopped && old_status == VMStatus::Running) {
        last_stopped_at_ = now;
    }
    
    // Add status change to history
    status_history_.emplace_back(old_status, new_status, now);
    
    // Keep history limited to last 100 entries
    if (status_history_.size() > 100) {
        status_history_.pop_front();
    }
    
    // Clear error message when transitioning to normal state
    if (new_status != VMStatus::Error && new_status != VMStatus::Crashed) {
        last_error_message_.reset();
    }
}

void VirtualMachine::set_process_id(ProcessId pid) {
    if (pid == 0 && (status_ == VMStatus::Running || status_ == VMStatus::Starting)) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Cannot clear process ID while VM is running or starting",
                      config_.vm_id().value());
    }
    
    process_id_ = pid;
}

void VirtualMachine::set_ssh_port(PortNumber port) {
    if (port != 0 && port < 1024) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "SSH port must be between 1024 and 65535",
                      config_.vm_id().value());
    }
    
    allocated_ssh_port_ = port;
}

void VirtualMachine::set_vnc_port(PortNumber port) {
    if (port != 0 && (port < 5900 || port > 5999)) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "VNC port must be between 5900 and 5999",
                      config_.vm_id().value());
    }
    
    allocated_vnc_port_ = port;
}

void VirtualMachine::update_configuration(const VMConfiguration& new_config) {
    // Can only update configuration when VM is stopped
    if (status_ != VMStatus::Stopped) {
        THROW_VM_ERROR(ErrorCode::VMNotRunning,
                      "Cannot update configuration while VM is not stopped",
                      config_.vm_id().value());
    }
    
    // Validate new configuration
    new_config.validate();
    
    // VM ID cannot be changed
    if (new_config.vm_id().value() != config_.vm_id().value()) {
        THROW_VM_ERROR(ErrorCode::InvalidArgument,
                      "Cannot change VM ID in configuration update",
                      config_.vm_id().value());
    }
    
    config_ = new_config;
}

std::chrono::milliseconds VirtualMachine::get_current_uptime() const {
    if (status_ == VMStatus::Running) {
        auto now = std::chrono::system_clock::now();
        auto current_session = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_status_change_
        );
        return total_uptime_ + current_session;
    }
    
    return total_uptime_;
}

std::chrono::milliseconds VirtualMachine::get_current_session_duration() const {
    if (status_ == VMStatus::Running && last_started_at_.has_value()) {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_started_at_.value()
        );
    }
    
    return std::chrono::milliseconds::zero();
}

std::chrono::system_clock::time_point VirtualMachine::get_last_activity() const {
    return last_status_change_;
}

bool VirtualMachine::is_running() const {
    return status_ == VMStatus::Running;
}

bool VirtualMachine::is_stopped() const {
    return status_ == VMStatus::Stopped;
}

bool VirtualMachine::is_transitioning() const {
    return status_ == VMStatus::Starting || status_ == VMStatus::Stopping;
}

bool VirtualMachine::has_process() const {
    return process_id_ != 0;
}

bool VirtualMachine::has_ssh_port() const {
    return allocated_ssh_port_ != 0;
}

bool VirtualMachine::has_vnc_port() const {
    return allocated_vnc_port_ != 0;
}

const std::deque<VirtualMachine::StatusChange>& VirtualMachine::get_status_history() const {
    return status_history_;
}

std::vector<VirtualMachine::StatusChange> VirtualMachine::get_recent_status_changes(size_t count) const {
    std::vector<StatusChange> recent;
    recent.reserve(std::min(count, status_history_.size()));
    
    auto start = status_history_.size() > count ? 
                 status_history_.end() - count : status_history_.begin();
    
    std::copy(start, status_history_.end(), std::back_inserter(recent));
    return recent;
}

bool VirtualMachine::is_valid_status_transition(VMStatus from, VMStatus to) {
    // Same status is always valid (no-op)
    if (from == to) {
        return true;
    }
    
    // Error state can be reached from any state
    if (to == VMStatus::Error || to == VMStatus::Crashed) {
        return true;
    }
    
    switch (from) {
        case VMStatus::Stopped:
            return to == VMStatus::Starting || to == VMStatus::Running;
            
        case VMStatus::Starting:
            return to == VMStatus::Running || to == VMStatus::Stopped;
            
        case VMStatus::Running:
            return to == VMStatus::Stopping || to == VMStatus::Stopped;
            
        case VMStatus::Stopping:
            return to == VMStatus::Stopped;
            
        case VMStatus::Crashed:
            return to == VMStatus::Stopped || to == VMStatus::Starting;
            
        case VMStatus::Error:
            return to == VMStatus::Stopped || to == VMStatus::Starting;
    }
    
    return false;
}

std::string_view VirtualMachine::status_to_string(VMStatus status) {
    switch (status) {
        case VMStatus::Stopped:     return "stopped";
        case VMStatus::Starting:    return "starting";
        case VMStatus::Running:     return "running";
        case VMStatus::Stopping:   return "stopping";
        case VMStatus::Crashed:     return "crashed";
        case VMStatus::Error:       return "error";
    }
    return "unknown";
}

VMStatus VirtualMachine::status_from_string(std::string_view status_str) {
    if (status_str == "stopped")    return VMStatus::Stopped;
    if (status_str == "starting")   return VMStatus::Starting;
    if (status_str == "running")    return VMStatus::Running;
    if (status_str == "stopping")  return VMStatus::Stopping;
    if (status_str == "crashed")    return VMStatus::Crashed;
    if (status_str == "error")      return VMStatus::Error;
    
    THROW_VM_ERROR(ErrorCode::InvalidArgument,
                  "Unknown VM status: " + std::string(status_str), "");
}

// Comparison operators for sorting/searching
bool VirtualMachine::operator<(const VirtualMachine& other) const {
    return config_.vm_id() < other.config_.vm_id();
}

bool VirtualMachine::operator==(const VirtualMachine& other) const {
    return config_.vm_id() == other.config_.vm_id();
}

bool VirtualMachine::operator!=(const VirtualMachine& other) const {
    return !(*this == other);
}

void VirtualMachine::set_error(const std::string& error_message) {
    last_error_message_ = error_message;
    status_ = VMStatus::Error;
    last_status_change_ = std::chrono::system_clock::now();
    
    // Add to status history
    status_history_.emplace_back(status_, VMStatus::Error, last_status_change_);
    
    // Keep history limited
    if (status_history_.size() > 100) {
        status_history_.pop_front();
    }
}

void VirtualMachine::set_resource_usage(const ResourceUsage& usage) {
    current_resource_usage_ = usage;
    
    // Add to history with timestamp
    resource_usage_history_.emplace_back(
        std::chrono::system_clock::now(), usage
    );
    
    // Keep history limited to last 1000 entries
    if (resource_usage_history_.size() > 1000) {
        resource_usage_history_.erase(resource_usage_history_.begin());
    }
}

bool VirtualMachine::can_be_persisted() const {
    // Cannot persist VMs in error states
    if (status_ == VMStatus::Error || status_ == VMStatus::Crashed) {
        return false;
    }
    
    // Cannot persist VMs that are transitioning
    if (status_ == VMStatus::Starting || status_ == VMStatus::Stopping) {
        return false;
    }
    
    return true;
}

bool VirtualMachine::is_in_valid_state() const {
    // Running VMs must have a process ID
    if (status_ == VMStatus::Running && process_id_ == 0) {
        return false;
    }
    
    // Stopped VMs should not have process IDs
    if (status_ == VMStatus::Stopped && process_id_ != 0) {
        return false;
    }
    
    // Port numbers must be valid if set
    if (allocated_ssh_port_ != 0 && allocated_ssh_port_ < 1024) {
        return false;
    }
    
    if (allocated_vnc_port_ != 0 && allocated_vnc_port_ < 5900) {
        return false;
    }
    
    return true;
}

} // namespace scratchpad