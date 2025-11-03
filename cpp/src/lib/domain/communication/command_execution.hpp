#pragma once

#include "scratchpad/types.hpp"
#include <string>
#include <chrono>
#include <optional>
#include <deque>
#include <map>
#include <mutex>
#include <atomic>
#include <string_view>

namespace scratchpad {

/**
 * Execution status enumeration
 */
enum class ExecutionStatus {
    NotStarted,     // Execution not yet started
    Running,        // Execution in progress
    Completed,      // Execution completed successfully
    Failed,         // Execution failed with error
    Timeout,        // Execution timed out
    Cancelled       // Execution was cancelled
};

/**
 * Entity representing a command execution instance
 * 
 * Tracks the lifecycle, status, and output of a command execution.
 * Provides real-time monitoring and thread-safe output capture.
 */
class CommandExecution {
public:
    /**
     * Status change history entry
     */
    struct StatusChange {
        ExecutionStatus from_status;
        ExecutionStatus to_status;
        std::chrono::system_clock::time_point timestamp;
        
        StatusChange(ExecutionStatus from, ExecutionStatus to, std::chrono::system_clock::time_point when)
            : from_status(from), to_status(to), timestamp(when) {}
    };

    /**
     * Construct command execution
     * @param command Command to execute
     * @param context Execution context
     * @throws SSHError if command is empty
     */
    CommandExecution(const std::string& command, const ExecutionContext& context = ExecutionContext::create_default());

    // Copy and move semantics
    CommandExecution(const CommandExecution&) = delete;
    CommandExecution& operator=(const CommandExecution&) = delete;
    CommandExecution(CommandExecution&&) = default;
    CommandExecution& operator=(CommandExecution&&) = default;

    // ========== Basic Properties ==========

    /**
     * Get execution ID
     * @return Unique execution identifier
     */
    const std::string& execution_id() const { return execution_id_; }

    /**
     * Get command string
     * @return Command to execute
     */
    const std::string& command() const { return command_; }

    /**
     * Get execution context
     * @return Execution context
     */
    const ExecutionContext& context() const { return context_; }

    /**
     * Get formatted command with context
     * @return Command with environment and directory context
     */
    std::string get_formatted_command() const;

    /**
     * Get creation timestamp
     * @return Execution creation time
     */
    std::chrono::system_clock::time_point created_at() const { return created_at_; }

    // ========== Status Management ==========

    /**
     * Get current execution status
     * @return Current status
     */
    ExecutionStatus status() const { return status_; }

    /**
     * Set execution status (validates transitions)
     * @param new_status New status
     * @throws SSHError if transition is invalid
     */
    void set_status(ExecutionStatus new_status);

    /**
     * Get exit code
     * @return Process exit code (0 if not finished)
     */
    int exit_code() const { return exit_code_; }

    /**
     * Set exit code
     * @param code Process exit code
     */
    void set_exit_code(int code) { exit_code_ = code; }

    // ========== Output Management ==========

    /**
     * Append data to stdout buffer (thread-safe)
     * @param data Data to append
     */
    void append_stdout(const std::string& data);

    /**
     * Append data to stderr buffer (thread-safe)
     * @param data Data to append
     */
    void append_stderr(const std::string& data);

    /**
     * Get current stdout output (thread-safe)
     * @return Stdout content
     */
    std::string get_stdout() const;

    /**
     * Get current stderr output (thread-safe)
     * @return Stderr content
     */
    std::string get_stderr() const;

    /**
     * Clear output buffers
     */
    void clear_output() {
        std::lock_guard<std::mutex> lock(output_mutex_);
        stdout_buffer_.clear();
        stderr_buffer_.clear();
    }

    // ========== Result Management ==========

    /**
     * Set final execution result
     * @param result Command result
     */
    void set_result(const CommandResult& result);

    /**
     * Get execution result
     * @return Command result (may be partial if still running)
     */
    CommandResult get_result() const;

    /**
     * Check if result is available
     * @return true if execution has finished
     */
    bool has_result() const { return result_.has_value(); }

    // ========== Error Management ==========

    /**
     * Get error message
     * @return Error message if execution failed
     */
    std::optional<std::string> error_message() const { return error_message_; }

    /**
     * Set error message
     * @param error Error message
     */
    void set_error_message(const std::string& error);

    // ========== Timing Information ==========

    /**
     * Get started timestamp
     * @return Start time if execution has started
     */
    std::optional<std::chrono::system_clock::time_point> started_at() const { 
        return started_at_; 
    }

    /**
     * Get completed timestamp
     * @return Completion time if execution has finished
     */
    std::optional<std::chrono::system_clock::time_point> completed_at() const { 
        return completed_at_; 
    }

    /**
     * Get execution time
     * @return Duration of execution (current if still running)
     */
    std::chrono::milliseconds get_execution_time() const;

    /**
     * Get age (time since creation)
     * @return Time since execution was created
     */
    std::chrono::milliseconds get_age() const;

    // ========== Status Queries ==========

    /**
     * Check if execution has finished
     * @return true if in terminal state
     */
    bool is_finished() const;

    /**
     * Check if execution was successful
     * @return true if completed with exit code 0
     */
    bool is_successful() const;

    /**
     * Check if execution is currently running
     * @return true if status is Running
     */
    bool is_running() const;

    /**
     * Check if execution has not started
     * @return true if status is NotStarted
     */
    bool is_not_started() const { return status_ == ExecutionStatus::NotStarted; }

    /**
     * Check if execution failed
     * @return true if status is Failed
     */
    bool is_failed() const { return status_ == ExecutionStatus::Failed; }

    /**
     * Check if execution timed out
     * @return true if status is Timeout
     */
    bool is_timeout() const { return status_ == ExecutionStatus::Timeout; }

    /**
     * Check if execution was cancelled
     * @return true if status is Cancelled
     */
    bool is_cancelled() const { return status_ == ExecutionStatus::Cancelled; }

    /**
     * Check if execution has exceeded timeout
     * @param timeout Timeout threshold
     * @return true if execution time exceeds timeout
     */
    bool has_timed_out(std::chrono::milliseconds timeout) const;

    // ========== Status History ==========

    /**
     * Get complete status change history
     * @return Status change history
     */
    const std::deque<StatusChange>& get_status_history() const { return status_history_; }

    // ========== Static Utilities ==========

    /**
     * Check if status transition is valid
     * @param from Current status
     * @param to Target status
     * @return true if transition is allowed
     */
    static bool is_valid_status_transition(ExecutionStatus from, ExecutionStatus to);

    /**
     * Convert status to string
     * @param status Execution status
     * @return Status string
     */
    static std::string_view status_to_string(ExecutionStatus status);

    /**
     * Convert string to status
     * @param status_str Status string
     * @return Execution status
     * @throws SSHError if string is invalid
     */
    static ExecutionStatus status_from_string(std::string_view status_str);

private:
    // Core properties
    std::string execution_id_;
    std::string command_;
    ExecutionContext context_;
    
    // Status tracking
    ExecutionStatus status_;
    int exit_code_;
    std::optional<CommandResult> result_;
    std::optional<std::string> error_message_;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at_;
    std::optional<std::chrono::system_clock::time_point> started_at_;
    std::optional<std::chrono::system_clock::time_point> completed_at_;
    std::optional<std::chrono::milliseconds> execution_duration_;
    
    // Output buffers (thread-safe)
    mutable std::mutex output_mutex_;
    std::string stdout_buffer_;
    std::string stderr_buffer_;
    
    // History tracking
    std::deque<StatusChange> status_history_;

    static std::string generate_execution_id();
};

} // namespace scratchpad