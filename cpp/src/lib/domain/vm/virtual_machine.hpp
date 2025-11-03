#pragma once

#include "vm_configuration.hpp"
#include "scratchpad/types.hpp"
#include <chrono>
#include <optional>
#include <deque>
#include <vector>
#include <string_view>

namespace scratchpad {

/**
 * Entity representing a virtual machine instance
 * 
 * This is the main VM entity that tracks state, configuration, and lifecycle
 * information for a virtual machine. It maintains status history, uptime
 * tracking, and resource allocation details.
 */
class VirtualMachine {
public:
    /**
     * Status change history entry
     */
    struct StatusChange {
        VMStatus from_status;
        VMStatus to_status;
        std::chrono::system_clock::time_point timestamp;
        
        StatusChange(VMStatus from, VMStatus to, std::chrono::system_clock::time_point when)
            : from_status(from), to_status(to), timestamp(when) {}
    };

    /**
     * Construct VM with configuration
     * @param config VM configuration
     * @throws VMError if configuration is invalid
     */
    explicit VirtualMachine(const VMConfiguration& config);

    /**
     * Construct VM with configuration (move semantics)
     * @param config VM configuration
     * @throws VMError if configuration is invalid
     */
    explicit VirtualMachine(VMConfiguration&& config);

    // Copy and move semantics
    VirtualMachine(const VirtualMachine&) = default;
    VirtualMachine(VirtualMachine&&) = default;
    VirtualMachine& operator=(const VirtualMachine&) = default;
    VirtualMachine& operator=(VirtualMachine&&) = default;

    // ========== Identity and Configuration ==========

    /**
     * Get VM identifier
     * @return VM ID
     */
    const VMId& id() const { return config_.vm_id(); }

    /**
     * Get VM configuration
     * @return VM configuration
     */
    const VMConfiguration& configuration() const { return config_; }

    /**
     * Update VM configuration (only when stopped)
     * @param new_config New configuration
     * @throws VMError if VM is not stopped or config is invalid
     */
    void update_configuration(const VMConfiguration& new_config);

    // ========== Status Management ==========

    /**
     * Get current status
     * @return Current VM status
     */
    VMStatus status() const { return status_; }

    /**
     * Set VM status (validates transitions)
     * @param new_status New status
     * @throws VMError if transition is invalid
     */
    void set_status(VMStatus new_status);

    /**
     * Get time of last status change
     * @return Timestamp of last status change
     */
    std::chrono::system_clock::time_point last_status_change() const { 
        return last_status_change_; 
    }

    /**
     * Get creation timestamp
     * @return VM creation time
     */
    std::chrono::system_clock::time_point created_at() const { 
        return created_at_; 
    }

    // ========== Process Management ==========

    /**
     * Get process ID
     * @return Process ID (0 if no process)
     */
    ProcessId process_id() const { return process_id_; }

    /**
     * Set process ID
     * @param pid Process ID
     * @throws VMError if setting invalid PID for current status
     */
    void set_process_id(ProcessId pid);

    /**
     * Check if VM has an associated process
     * @return true if process ID is set
     */
    bool has_process() const;

    // ========== Network Configuration ==========

    /**
     * Get allocated SSH port
     * @return SSH port (0 if not allocated)
     */
    PortNumber ssh_port() const { return allocated_ssh_port_; }

    /**
     * Set allocated SSH port
     * @param port SSH port number
     * @throws VMError if port is invalid
     */
    void set_ssh_port(PortNumber port);

    /**
     * Check if SSH port is allocated
     * @return true if SSH port is set
     */
    bool has_ssh_port() const;

    /**
     * Get allocated VNC port
     * @return VNC port (0 if not allocated)
     */
    PortNumber vnc_port() const { return allocated_vnc_port_; }

    /**
     * Set allocated VNC port
     * @param port VNC port number
     * @throws VMError if port is invalid
     */
    void set_vnc_port(PortNumber port);

    /**
     * Check if VNC port is allocated
     * @return true if VNC port is set
     */
    bool has_vnc_port() const;

    // ========== Runtime Statistics ==========

    /**
     * Get number of times VM has been started
     * @return Start count
     */
    uint32_t start_count() const { return start_count_; }

    /**
     * Get total uptime across all sessions
     * @return Total uptime
     */
    std::chrono::milliseconds get_total_uptime() const { 
        return get_current_uptime(); 
    }

    /**
     * Get current uptime (including active session if running)
     * @return Current total uptime
     */
    std::chrono::milliseconds get_current_uptime() const;

    /**
     * Get duration of current session (if running)
     * @return Current session duration
     */
    std::chrono::milliseconds get_current_session_duration() const;

    /**
     * Get last activity timestamp
     * @return Last activity time
     */
    std::chrono::system_clock::time_point get_last_activity() const;

    /**
     * Get last started timestamp
     * @return Last start time (if ever started)
     */
    std::optional<std::chrono::system_clock::time_point> last_started_at() const {
        return last_started_at_;
    }

    /**
     * Get last stopped timestamp
     * @return Last stop time (if ever stopped after running)
     */
    std::optional<std::chrono::system_clock::time_point> last_stopped_at() const {
        return last_stopped_at_;
    }

    // ========== Status Queries ==========

    /**
     * Check if VM is currently running
     * @return true if status is Running
     */
    bool is_running() const;

    /**
     * Check if VM is stopped
     * @return true if status is Stopped
     */
    bool is_stopped() const;

    /**
     * Check if VM is in transitional state
     * @return true if status is Starting or Stopping
     */
    bool is_transitioning() const;

    /**
     * Check if VM is in error state
     * @return true if status is Crashed
     */
    bool is_crashed() const { return status_ == VMStatus::Crashed; }

    /**
     * Check if VM can be started
     * @return true if VM can transition to Starting
     */
    bool can_start() const { 
        return status_ == VMStatus::Stopped || status_ == VMStatus::Crashed; 
    }

    /**
     * Check if VM can be stopped
     * @return true if VM can transition to Stopping
     */
    bool can_stop() const { 
        return status_ == VMStatus::Running || status_ == VMStatus::Starting; 
    }

    // ========== Status History ==========

    /**
     * Get complete status change history
     * @return Status change history
     */
    const std::deque<StatusChange>& get_status_history() const;

    /**
     * Get recent status changes
     * @param count Maximum number of changes to return
     * @return Recent status changes
     */
    std::vector<StatusChange> get_recent_status_changes(size_t count = 10) const;

    // ========== Static Utilities ==========

    /**
     * Check if status transition is valid
     * @param from Current status
     * @param to Target status
     * @return true if transition is allowed
     */
    static bool is_valid_status_transition(VMStatus from, VMStatus to);

    /**
     * Convert status to string
     * @param status VM status
     * @return Status string
     */
    static std::string_view status_to_string(VMStatus status);

    /**
     * Convert string to status
     * @param status_str Status string
     * @return VM status
     * @throws VMError if string is invalid
     */
    static VMStatus status_from_string(std::string_view status_str);

    // ========== Comparison Operators ==========

    bool operator<(const VirtualMachine& other) const;
    bool operator==(const VirtualMachine& other) const;
    bool operator!=(const VirtualMachine& other) const;

private:
    // Core configuration and identity
    VMConfiguration config_;
    
    // Current state
    VMStatus status_;
    ProcessId process_id_;
    
    // Network allocation
    PortNumber allocated_ssh_port_ = 0;
    PortNumber allocated_vnc_port_ = 0;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point last_status_change_;
    std::optional<std::chrono::system_clock::time_point> last_started_at_;
    std::optional<std::chrono::system_clock::time_point> last_stopped_at_;
    
    // Statistics
    uint32_t start_count_;
    std::chrono::milliseconds total_uptime_;
    
    // History tracking
    std::deque<StatusChange> status_history_;
};

} // namespace scratchpad