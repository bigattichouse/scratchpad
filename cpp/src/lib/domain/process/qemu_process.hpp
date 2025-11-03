#pragma once

#include "scratchpad/types.hpp"
#include <string>
#include <vector>
#include <chrono>
#include <optional>
#include <deque>
#include <string_view>

namespace scratchpad {

/**
 * Process status enumeration
 */
enum class ProcessStatus {
    Starting,    // Process is starting up
    Running,     // Process is running normally
    Exited,      // Process exited normally
    Killed       // Process was killed/terminated
};

/**
 * Entity representing a QEMU process
 * 
 * Tracks the lifecycle, status, and metadata of a running QEMU process.
 * Provides monitoring capabilities and command line analysis.
 */
class QemuProcess {
public:
    /**
     * Log entry for process monitoring
     */
    enum class LogLevel {
        Debug,
        Info,
        Warning,
        Error
    };

    struct LogEntry {
        std::chrono::system_clock::time_point timestamp;
        LogLevel level;
        std::string message;
        
        LogEntry(std::chrono::system_clock::time_point when, LogLevel lvl, const std::string& msg)
            : timestamp(when), level(lvl), message(msg) {}
    };

    /**
     * Status change history entry
     */
    struct StatusChange {
        ProcessStatus from_status;
        ProcessStatus to_status;
        std::chrono::system_clock::time_point timestamp;
        
        StatusChange(ProcessStatus from, ProcessStatus to, std::chrono::system_clock::time_point when)
            : from_status(from), to_status(to), timestamp(when) {}
    };

    /**
     * Construct QEMU process tracker
     * @param pid Process ID
     * @param command_line Command line arguments used to start process
     * @throws ProcessError if invalid parameters
     */
    QemuProcess(ProcessId pid, const std::vector<std::string>& command_line);

    // Copy and move semantics
    QemuProcess(const QemuProcess&) = default;
    QemuProcess(QemuProcess&&) = default;
    QemuProcess& operator=(const QemuProcess&) = default;
    QemuProcess& operator=(QemuProcess&&) = default;

    // ========== Basic Properties ==========

    /**
     * Get process ID
     * @return Process ID
     */
    ProcessId process_id() const { return process_id_; }

    /**
     * Get command line arguments
     * @return Command line vector
     */
    const std::vector<std::string>& command_line() const { return command_line_; }

    /**
     * Get command line as single string
     * @return Formatted command line string
     */
    std::string get_command_line_string() const;

    /**
     * Get process creation timestamp
     * @return Creation time
     */
    std::chrono::system_clock::time_point created_at() const { return created_at_; }

    // ========== Status Management ==========

    /**
     * Get current process status
     * @return Current status
     */
    ProcessStatus status() const { return status_; }

    /**
     * Set process status (validates transitions)
     * @param new_status New status
     * @throws ProcessError if transition is invalid
     */
    void set_status(ProcessStatus new_status);

    /**
     * Get time of last status change
     * @return Last status change timestamp
     */
    std::chrono::system_clock::time_point last_status_change() const { 
        return last_status_change_; 
    }

    /**
     * Get exit code (only valid if status is Exited)
     * @return Exit code if available
     */
    std::optional<int> exit_code() const { return exit_code_; }

    /**
     * Set exit code (only when status is Exited)
     * @param exit_code Process exit code
     * @throws ProcessError if process hasn't exited
     */
    void set_exit_code(int exit_code);

    // ========== Monitoring ==========

    /**
     * Get time of last health check
     * @return Last check timestamp
     */
    std::chrono::system_clock::time_point last_check() const { return last_check_; }

    /**
     * Update last check timestamp to now
     */
    void update_last_check();

    /**
     * Add log entry for monitoring
     * @param message Log message
     * @param level Log level
     */
    void add_log_entry(const std::string& message, LogLevel level = LogLevel::Info);

    /**
     * Get process log entries
     * @return All log entries
     */
    const std::deque<LogEntry>& log_entries() const { return log_entries_; }

    /**
     * Get recent log entries
     * @param count Maximum number of entries
     * @return Recent log entries
     */
    std::vector<LogEntry> get_recent_logs(size_t count = 50) const;

    /**
     * Get log entries since specific time
     * @param since Timestamp to filter from
     * @return Filtered log entries
     */
    std::vector<LogEntry> get_logs_since(std::chrono::system_clock::time_point since) const;

    // ========== Runtime Statistics ==========

    /**
     * Get total runtime (including current session if running)
     * @return Total runtime
     */
    std::chrono::milliseconds get_current_runtime() const;

    /**
     * Get process age (time since creation)
     * @return Process age
     */
    std::chrono::milliseconds get_age() const;

    /**
     * Get time since last health check
     * @return Time since last check
     */
    std::chrono::milliseconds get_time_since_last_check() const;

    /**
     * Get started timestamp
     * @return Start time if process has been started
     */
    std::optional<std::chrono::system_clock::time_point> started_at() const { 
        return started_at_; 
    }

    /**
     * Get exited timestamp
     * @return Exit time if process has exited
     */
    std::optional<std::chrono::system_clock::time_point> exited_at() const { 
        return exited_at_; 
    }

    // ========== Status Queries ==========

    /**
     * Check if process is currently running
     * @return true if status is Running
     */
    bool is_running() const;

    /**
     * Check if process has stopped
     * @return true if status is Exited or Killed
     */
    bool is_stopped() const;

    /**
     * Check if process is starting
     * @return true if status is Starting
     */
    bool is_starting() const { return status_ == ProcessStatus::Starting; }

    /**
     * Check if process is healthy
     * @return true if process appears to be running normally
     */
    bool is_healthy() const;

    // ========== Command Line Analysis ==========

    /**
     * Extract VM name from command line
     * @return VM name if found in -name parameter
     */
    std::optional<std::string> get_vm_name() const;

    /**
     * Extract acceleration type from command line
     * @return Acceleration type if found in -accel parameter
     */
    std::optional<AccelerationType> get_acceleration_type() const;

    /**
     * Extract memory size from command line
     * @return Memory size string if found in -m parameter
     */
    std::optional<std::string> get_memory_size() const;

    // ========== Status History ==========

    /**
     * Get complete status change history
     * @return Status change history
     */
    const std::deque<StatusChange>& get_status_history() const { return status_history_; }

    // ========== Static Utilities ==========

    /**
     * Check if command line represents a QEMU process
     * @param command_line Command line to check
     * @return true if appears to be QEMU command
     */
    static bool is_qemu_command(const std::vector<std::string>& command_line);

    /**
     * Check if status transition is valid
     * @param from Current status
     * @param to Target status
     * @return true if transition is allowed
     */
    static bool is_valid_status_transition(ProcessStatus from, ProcessStatus to);

    /**
     * Convert status to string
     * @param status Process status
     * @return Status string
     */
    static std::string_view status_to_string(ProcessStatus status);

    /**
     * Convert string to status
     * @param status_str Status string
     * @return Process status
     * @throws ProcessError if string is invalid
     */
    static ProcessStatus status_from_string(std::string_view status_str);

    /**
     * Convert log level to string
     * @param level Log level
     * @return Level string
     */
    static std::string_view log_level_to_string(LogLevel level);

    /**
     * Convert string to log level
     * @param level_str Level string
     * @return Log level (defaults to Info for unknown)
     */
    static LogLevel log_level_from_string(std::string_view level_str);

    // ========== Comparison Operators ==========

    bool operator<(const QemuProcess& other) const;
    bool operator==(const QemuProcess& other) const;
    bool operator!=(const QemuProcess& other) const;

private:
    // Core properties
    ProcessId process_id_;
    std::vector<std::string> command_line_;
    
    // Status tracking
    ProcessStatus status_;
    std::optional<int> exit_code_;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point last_status_change_;
    std::chrono::system_clock::time_point last_check_;
    std::optional<std::chrono::system_clock::time_point> started_at_;
    std::optional<std::chrono::system_clock::time_point> exited_at_;
    
    // Runtime tracking
    std::chrono::milliseconds total_runtime_{0};
    
    // History and logging
    std::deque<StatusChange> status_history_;
    std::deque<LogEntry> log_entries_;
};

} // namespace scratchpad