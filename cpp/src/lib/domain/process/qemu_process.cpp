#include "scratchpad/domain/process/qemu_process.hpp"
#include "scratchpad/errors.hpp"
#include <algorithm>

namespace scratchpad {

QemuProcess::QemuProcess(ProcessId pid, const std::vector<std::string>& command_line)
    : process_id_(pid)
    , command_line_(command_line)
    , status_(ProcessStatus::Starting)
    , created_at_(std::chrono::system_clock::now())
    , last_check_(created_at_) {
    
    if (pid == 0) {
        THROW_PROCESS_ERROR(ErrorCode::InvalidArgument, "Process ID cannot be zero", pid);
    }
    
    if (command_line.empty()) {
        THROW_PROCESS_ERROR(ErrorCode::InvalidArgument, "Command line cannot be empty", pid);
    }
    
    // Validate that this looks like a QEMU command
    if (!is_qemu_command(command_line)) {
        THROW_PROCESS_ERROR(ErrorCode::InvalidArgument, 
                           "Command line does not appear to be a QEMU command", pid);
    }
}

void QemuProcess::set_status(ProcessStatus new_status) {
    if (new_status == status_) {
        return; // No change
    }
    
    auto now = std::chrono::system_clock::now();
    
    // Validate status transitions
    if (!is_valid_status_transition(status_, new_status)) {
        THROW_PROCESS_ERROR(ErrorCode::InvalidArgument,
                           "Invalid status transition from " + 
                           std::string(status_to_string(status_)) + " to " +
                           std::string(status_to_string(new_status)),
                           process_id_);
    }
    
    // Update runtime tracking
    if (status_ == ProcessStatus::Running && new_status != ProcessStatus::Running) {
        auto session_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_status_change_
        );
        total_runtime_ += session_duration;
    }
    
    ProcessStatus old_status = status_;
    status_ = new_status;
    last_status_change_ = now;
    
    // Update specific timestamps
    if (new_status == ProcessStatus::Running && old_status != ProcessStatus::Running) {
        started_at_ = now;
    } else if (new_status == ProcessStatus::Exited && old_status == ProcessStatus::Running) {
        exited_at_ = now;
    }
    
    // Add to status history
    status_history_.emplace_back(old_status, new_status, now);
    
    // Keep history limited
    if (status_history_.size() > 50) {
        status_history_.pop_front();
    }
}

void QemuProcess::set_exit_code(int exit_code) {
    if (status_ != ProcessStatus::Exited) {
        THROW_PROCESS_ERROR(ErrorCode::InvalidArgument,
                           "Cannot set exit code for process that hasn't exited",
                           process_id_);
    }
    
    exit_code_ = exit_code;
}

void QemuProcess::update_last_check() {
    last_check_ = std::chrono::system_clock::now();
}

void QemuProcess::add_log_entry(const std::string& message, LogLevel level) {
    auto now = std::chrono::system_clock::now();
    log_entries_.emplace_back(now, level, message);
    
    // Keep log limited to last 1000 entries
    if (log_entries_.size() > 1000) {
        log_entries_.pop_front();
    }
}

std::chrono::milliseconds QemuProcess::get_current_runtime() const {
    if (status_ == ProcessStatus::Running && started_at_.has_value()) {
        auto now = std::chrono::system_clock::now();
        auto current_session = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - started_at_.value()
        );
        return total_runtime_ + current_session;
    }
    
    return total_runtime_;
}

std::chrono::milliseconds QemuProcess::get_age() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - created_at_);
}

std::chrono::milliseconds QemuProcess::get_time_since_last_check() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_check_);
}

bool QemuProcess::is_running() const {
    return status_ == ProcessStatus::Running;
}

bool QemuProcess::is_stopped() const {
    return status_ == ProcessStatus::Exited || status_ == ProcessStatus::Killed;
}

bool QemuProcess::is_healthy() const {
    // Consider process healthy if:
    // 1. It's running
    // 2. Last check was recent (less than 30 seconds ago)
    // 3. No recent error log entries
    
    if (!is_running()) {
        return false;
    }
    
    auto time_since_check = get_time_since_last_check();
    if (time_since_check > std::chrono::seconds(30)) {
        return false; // Stale check
    }
    
    // Check for recent error log entries (last 5 minutes)
    auto now = std::chrono::system_clock::now();
    auto cutoff = now - std::chrono::minutes(5);
    
    for (const auto& entry : log_entries_) {
        if (entry.timestamp >= cutoff && entry.level == LogLevel::Error) {
            return false; // Recent error
        }
    }
    
    return true;
}

std::vector<QemuProcess::LogEntry> QemuProcess::get_recent_logs(size_t count) const {
    std::vector<LogEntry> recent;
    recent.reserve(std::min(count, log_entries_.size()));
    
    auto start = log_entries_.size() > count ? 
                 log_entries_.end() - count : log_entries_.begin();
    
    std::copy(start, log_entries_.end(), std::back_inserter(recent));
    return recent;
}

std::vector<QemuProcess::LogEntry> QemuProcess::get_logs_since(
    std::chrono::system_clock::time_point since) const {
    
    std::vector<LogEntry> filtered;
    
    for (const auto& entry : log_entries_) {
        if (entry.timestamp >= since) {
            filtered.push_back(entry);
        }
    }
    
    return filtered;
}

std::string QemuProcess::get_command_line_string() const {
    std::string result;
    for (size_t i = 0; i < command_line_.size(); ++i) {
        if (i > 0) result += " ";
        
        // Quote arguments that contain spaces
        const auto& arg = command_line_[i];
        if (arg.find(' ') != std::string::npos) {
            result += "\"" + arg + "\"";
        } else {
            result += arg;
        }
    }
    return result;
}

std::optional<std::string> QemuProcess::get_vm_name() const {
    // Look for -name parameter in command line
    for (size_t i = 0; i < command_line_.size() - 1; ++i) {
        if (command_line_[i] == "-name") {
            return command_line_[i + 1];
        }
    }
    return {};
}

std::optional<AccelerationType> QemuProcess::get_acceleration_type() const {
    // Look for -accel parameter in command line
    for (size_t i = 0; i < command_line_.size() - 1; ++i) {
        if (command_line_[i] == "-accel") {
            const auto& accel = command_line_[i + 1];
            if (accel == "kvm") return AccelerationType::KVM;
            if (accel == "hvf") return AccelerationType::HVF;
            if (accel == "whpx") return AccelerationType::WHPX;
            if (accel == "tcg") return AccelerationType::TCG;
        }
    }
    return {};
}

std::optional<std::string> QemuProcess::get_memory_size() const {
    // Look for -m parameter in command line
    for (size_t i = 0; i < command_line_.size() - 1; ++i) {
        if (command_line_[i] == "-m") {
            return command_line_[i + 1];
        }
    }
    return {};
}

bool QemuProcess::is_qemu_command(const std::vector<std::string>& command_line) {
    if (command_line.empty()) {
        return false;
    }
    
    const auto& executable = command_line[0];
    
    // Check if executable name contains qemu-system
    return executable.find("qemu-system") != std::string::npos;
}

bool QemuProcess::is_valid_status_transition(ProcessStatus from, ProcessStatus to) {
    // Same status is always valid (no-op)
    if (from == to) {
        return true;
    }
    
    switch (from) {
        case ProcessStatus::Starting:
            return to == ProcessStatus::Running || 
                   to == ProcessStatus::Exited || 
                   to == ProcessStatus::Killed;
            
        case ProcessStatus::Running:
            return to == ProcessStatus::Exited || to == ProcessStatus::Killed;
            
        case ProcessStatus::Exited:
        case ProcessStatus::Killed:
            return false; // Terminal states
    }
    
    return false;
}

std::string_view QemuProcess::status_to_string(ProcessStatus status) {
    switch (status) {
        case ProcessStatus::Starting:   return "starting";
        case ProcessStatus::Running:    return "running";
        case ProcessStatus::Exited:     return "exited";
        case ProcessStatus::Killed:     return "killed";
    }
    return "unknown";
}

ProcessStatus QemuProcess::status_from_string(std::string_view status_str) {
    if (status_str == "starting")   return ProcessStatus::Starting;
    if (status_str == "running")    return ProcessStatus::Running;
    if (status_str == "exited")     return ProcessStatus::Exited;
    if (status_str == "killed")     return ProcessStatus::Killed;
    
    THROW_PROCESS_ERROR(ErrorCode::InvalidArgument,
                       "Unknown process status: " + std::string(status_str));
}

std::string_view QemuProcess::log_level_to_string(LogLevel level) {
    switch (level) {
        case LogLevel::Debug:   return "debug";
        case LogLevel::Info:    return "info";
        case LogLevel::Warning: return "warning";
        case LogLevel::Error:   return "error";
    }
    return "unknown";
}

QemuProcess::LogLevel QemuProcess::log_level_from_string(std::string_view level_str) {
    if (level_str == "debug")   return LogLevel::Debug;
    if (level_str == "info")    return LogLevel::Info;
    if (level_str == "warning") return LogLevel::Warning;
    if (level_str == "error")   return LogLevel::Error;
    
    return LogLevel::Info; // Default for unknown levels
}

// Comparison operators
bool QemuProcess::operator<(const QemuProcess& other) const {
    return process_id_ < other.process_id_;
}

bool QemuProcess::operator==(const QemuProcess& other) const {
    return process_id_ == other.process_id_;
}

bool QemuProcess::operator!=(const QemuProcess& other) const {
    return !(*this == other);
}

} // namespace scratchpad