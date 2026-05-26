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
 * Process resource usage information
 */
struct ProcessResourceUsage {
    uint64_t memory_rss_bytes = 0;  // Resident Set Size memory
    uint64_t memory_vms_bytes = 0;  // Virtual Memory Size
    double cpu_percent = 0.0;       // CPU usage percentage
    uint64_t io_read_bytes = 0;     // Total bytes read
    uint64_t io_write_bytes = 0;    // Total bytes written
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

    /**
     * Construct QEMU process tracker (implementation compatibility)
     * @param vm_id VM identifier (will use PID 0 until actual process starts)
     * @param command_line Command line arguments used to start process
     */
    QemuProcess(const VMId& vm_id, const std::vector<std::string>& command_line)
        : QemuProcess(0, command_line) {
        // Implementation compatibility constructor - real PID set later via start()
    }

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

    /**
     * Get process creation timestamp (alias for created_at)
     * @return Creation time
     */
    std::chrono::system_clock::time_point creation_time() const { return created_at_; }

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

    /**
     * Get termination signal (for killed processes)
     * @return Signal number if process was killed
     */
    std::optional<int> termination_signal() const { return termination_signal_; }

    /**
     * Set termination signal (for killed processes)
     * @param signal Signal number
     */
    void set_termination_signal(int signal) { termination_signal_ = signal; }

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

    /**
     * Add log entry (alias for add_log_entry)
     * @param level Log level
     * @param message Log message
     */
    void add_log(LogLevel level, const std::string& message) {
        add_log_entry(message, level);
    }

    /**
     * Get log entries (alias for log_entries)
     * @return All log entries
     */
    const std::deque<LogEntry>& logs() const { return log_entries_; }

    /**
     * Get filtered log entries by level
     * @param level Minimum log level
     * @param count Maximum number of entries
     * @return Filtered log entries
     */
    std::vector<LogEntry> logs(LogLevel level, size_t count = 50) const;

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

    // ========== Process Control (Implementation Compatibility) ==========

    /**
     * Start the process (implementation compatibility)
     * Note: This is a placeholder - actual process starting should be handled by QemuAdapter
     */
    void start() {
        // Implementation compatibility method - actual start logic in QemuAdapter
        set_status(ProcessStatus::Starting);
    }

    /**
     * Stop the process (implementation compatibility)
     * Note: This is a placeholder - actual process stopping should be handled by QemuAdapter
     */
    void stop() {
        // Implementation compatibility method - actual stop logic in QemuAdapter
        set_status(ProcessStatus::Exited);
    }

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
     * Check if process has exited (stopped)
     * @return true if status is Exited or Killed
     */
    bool has_exited() const { return is_stopped(); }

    /**
     * Check if process exited successfully (exit code 0)
     * @return true if exited with code 0
     */
    bool exited_successfully() const {
        return status_ == ProcessStatus::Exited && 
               exit_code_.has_value() && exit_code_.value() == 0;
    }

    /**
     * Check if process is healthy
     * @return true if process appears to be running normally
     */
    bool is_healthy() const;

    /**
     * Check if process is responsive
     * @return true if process is responding to health checks
     */
    bool is_responsive() const { return is_responsive_; }

    /**
     * Mark process as unresponsive
     */
    void mark_as_unresponsive() { is_responsive_ = false; }

    /**
     * Mark process as responsive
     */
    void mark_as_responsive() { is_responsive_ = true; }

    // ========== Resource Management ==========

    /**
     * Get current resource usage
     * @return Resource usage information
     */
    ProcessResourceUsage resource_usage() const { return current_resource_usage_; }

    /**
     * Set current resource usage
     * @param usage Resource usage information
     */
    void set_resource_usage(const ProcessResourceUsage& usage);

    /**
     * Get resource usage history
     * @return Historical resource usage data
     */
    const std::vector<std::pair<std::chrono::system_clock::time_point, ProcessResourceUsage>>& 
    resource_usage_history() const { return resource_usage_history_; }

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

    /**
     * Get status history (alias for get_status_history)
     * @return Status change history
     */
    const std::deque<StatusChange>& status_history() const { return get_status_history(); }

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
    std::optional<int> termination_signal_;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point last_status_change_;
    std::chrono::system_clock::time_point last_check_;
    std::optional<std::chrono::system_clock::time_point> started_at_;
    std::optional<std::chrono::system_clock::time_point> exited_at_;
    
    // Runtime tracking
    std::chrono::milliseconds total_runtime_{0};
    bool is_responsive_ = true;
    
    // History and logging
    std::deque<StatusChange> status_history_;
    std::deque<LogEntry> log_entries_;
    
    // Resource tracking
    ProcessResourceUsage current_resource_usage_;
    std::vector<std::pair<std::chrono::system_clock::time_point, ProcessResourceUsage>> resource_usage_history_;
};

} // namespace scratchpad