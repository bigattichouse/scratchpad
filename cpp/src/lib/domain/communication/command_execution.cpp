#include "scratchpad/domain/communication/command_execution.hpp"
#include "scratchpad/errors.hpp"

namespace scratchpad {

CommandExecution::CommandExecution(const std::string& command, 
                                   const ExecutionContext& context)
    : command_(command)
    , context_(context)
    , status_(ExecutionStatus::NotStarted)
    , created_at_(std::chrono::system_clock::now())
    , exit_code_(0) {
    
    if (command.empty()) {
        THROW_SSH_ERROR(ErrorCode::InvalidArgument, "Command cannot be empty");
    }
    
    // Generate unique execution ID
    execution_id_ = generate_execution_id();
}

void CommandExecution::set_status(ExecutionStatus new_status) {
    if (new_status == status_) {
        return; // No change
    }
    
    auto now = std::chrono::system_clock::now();
    
    // Validate status transitions
    if (!is_valid_status_transition(status_, new_status)) {
        THROW_SSH_ERROR(ErrorCode::InvalidArgument,
                       "Invalid execution status transition from " + 
                       std::string(status_to_string(status_)) + " to " +
                       std::string(status_to_string(new_status)));
    }
    
    ExecutionStatus old_status = status_;
    status_ = new_status;
    
    // Update timestamps based on status
    switch (new_status) {
        case ExecutionStatus::Running:
            if (!started_at_.has_value()) {
                started_at_ = now;
            }
            break;
            
        case ExecutionStatus::Completed:
        case ExecutionStatus::Failed:
        case ExecutionStatus::Timeout:
        case ExecutionStatus::Cancelled:
            completed_at_ = now;
            if (started_at_.has_value()) {
                execution_duration_ = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - started_at_.value()
                );
            }
            break;
            
        default:
            break;
    }
    
    // Add to status history
    status_history_.emplace_back(old_status, new_status, now);
    
    // Keep history limited
    if (status_history_.size() > 50) {
        status_history_.pop_front();
    }
}

void CommandExecution::set_result(const CommandResult& result) {
    result_ = result;
    exit_code_ = result.exit_code;
    
    // Set status based on exit code if not already set
    if (status_ == ExecutionStatus::Running) {
        if (result.exit_code == 0) {
            set_status(ExecutionStatus::Completed);
        } else {
            set_status(ExecutionStatus::Failed);
        }
    }
}

void CommandExecution::append_stdout(const std::string& data) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    stdout_buffer_ += data;
}

void CommandExecution::append_stderr(const std::string& data) {
    std::lock_guard<std::mutex> lock(output_mutex_);
    stderr_buffer_ += data;
}

void CommandExecution::set_error_message(const std::string& error) {
    error_message_ = error;
    if (status_ == ExecutionStatus::Running || status_ == ExecutionStatus::NotStarted) {
        set_status(ExecutionStatus::Failed);
    }
}

std::string CommandExecution::get_stdout() const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    return stdout_buffer_;
}

std::string CommandExecution::get_stderr() const {
    std::lock_guard<std::mutex> lock(output_mutex_);
    return stderr_buffer_;
}

std::chrono::milliseconds CommandExecution::get_execution_time() const {
    if (execution_duration_.has_value()) {
        return execution_duration_.value();
    }
    
    if (started_at_.has_value() && status_ == ExecutionStatus::Running) {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - started_at_.value()
        );
    }
    
    return std::chrono::milliseconds::zero();
}

std::chrono::milliseconds CommandExecution::get_age() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - created_at_);
}

bool CommandExecution::is_finished() const {
    return status_ == ExecutionStatus::Completed ||
           status_ == ExecutionStatus::Failed ||
           status_ == ExecutionStatus::Timeout ||
           status_ == ExecutionStatus::Cancelled;
}

bool CommandExecution::is_successful() const {
    return status_ == ExecutionStatus::Completed && exit_code_ == 0;
}

bool CommandExecution::is_running() const {
    return status_ == ExecutionStatus::Running;
}

bool CommandExecution::has_timed_out(std::chrono::milliseconds timeout) const {
    if (!started_at_.has_value()) {
        return false;
    }
    
    auto execution_time = get_execution_time();
    return execution_time > timeout;
}

CommandResult CommandExecution::get_result() const {
    if (result_.has_value()) {
        return result_.value();
    }
    
    // Create result from current state
    CommandResult result;
    result.exit_code = exit_code_;
    result.stdout_output = get_stdout();
    result.stderr_output = get_stderr();
    result.execution_time = get_execution_time();
    
    return result;
}

std::string CommandExecution::get_formatted_command() const {
    std::string formatted = command_;
    
    // Add environment variables if any
    if (!context_.environment_vars.empty()) {
        std::string env_prefix;
        for (const auto& [name, value] : context_.environment_vars) {
            env_prefix += name + "=" + value + " ";
        }
        formatted = env_prefix + formatted;
    }
    
    // Add working directory context if specified
    if (context_.working_directory.has_value()) {
        formatted = "cd " + context_.working_directory.value() + " && " + formatted;
    }
    
    return formatted;
}

std::string CommandExecution::generate_execution_id() {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()
    ).count();
    
    static std::atomic<uint32_t> counter{0};
    uint32_t id = counter.fetch_add(1);
    
    return "exec_" + std::to_string(timestamp) + "_" + std::to_string(id);
}

bool CommandExecution::is_valid_status_transition(ExecutionStatus from, ExecutionStatus to) {
    // Same status is always valid (no-op)
    if (from == to) {
        return true;
    }
    
    switch (from) {
        case ExecutionStatus::NotStarted:
            return to == ExecutionStatus::Running || 
                   to == ExecutionStatus::Cancelled;
            
        case ExecutionStatus::Running:
            return to == ExecutionStatus::Completed ||
                   to == ExecutionStatus::Failed ||
                   to == ExecutionStatus::Timeout ||
                   to == ExecutionStatus::Cancelled;
            
        case ExecutionStatus::Completed:
        case ExecutionStatus::Failed:
        case ExecutionStatus::Timeout:
        case ExecutionStatus::Cancelled:
            return false; // Terminal states
    }
    
    return false;
}

std::string_view CommandExecution::status_to_string(ExecutionStatus status) {
    switch (status) {
        case ExecutionStatus::NotStarted:   return "not_started";
        case ExecutionStatus::Running:      return "running";
        case ExecutionStatus::Completed:    return "completed";
        case ExecutionStatus::Failed:       return "failed";
        case ExecutionStatus::Timeout:      return "timeout";
        case ExecutionStatus::Cancelled:    return "cancelled";
    }
    return "unknown";
}

ExecutionStatus CommandExecution::status_from_string(std::string_view status_str) {
    if (status_str == "not_started")    return ExecutionStatus::NotStarted;
    if (status_str == "running")        return ExecutionStatus::Running;
    if (status_str == "completed")      return ExecutionStatus::Completed;
    if (status_str == "failed")         return ExecutionStatus::Failed;
    if (status_str == "timeout")        return ExecutionStatus::Timeout;
    if (status_str == "cancelled")      return ExecutionStatus::Cancelled;
    
    THROW_SSH_ERROR(ErrorCode::InvalidArgument,
                   "Unknown execution status: " + std::string(status_str));
}

// ExecutionContext implementation

ExecutionContext ExecutionContext::create_default() {
    ExecutionContext context;
    context.timeout = std::chrono::milliseconds(300000); // 5 minutes
    context.capture_output = true;
    return context;
}

ExecutionContext ExecutionContext::create_with_timeout(std::chrono::milliseconds timeout) {
    auto context = create_default();
    context.timeout = timeout;
    return context;
}

ExecutionContext ExecutionContext::create_with_directory(const std::string& working_dir) {
    auto context = create_default();
    context.working_directory = working_dir;
    return context;
}

ExecutionContext ExecutionContext::create_with_environment(
    const std::map<std::string, std::string>& env_vars) {
    auto context = create_default();
    context.environment_vars = env_vars;
    return context;
}

bool ExecutionContext::validate() const {
    // Check timeout is positive
    if (timeout <= std::chrono::milliseconds::zero()) {
        return false;
    }
    
    // Check working directory is absolute if specified
    if (working_directory.has_value()) {
        const auto& dir = working_directory.value();
        if (dir.empty() || dir[0] != '/') {
            return false;
        }
    }
    
    // Check environment variable names are not empty
    for (const auto& [name, value] : environment_vars) {
        if (name.empty()) {
            return false;
        }
    }
    
    return true;
}

void ExecutionContext::add_environment_variable(const std::string& name, const std::string& value) {
    if (!name.empty()) {
        environment_vars[name] = value;
    }
}

void ExecutionContext::remove_environment_variable(const std::string& name) {
    environment_vars.erase(name);
}

std::optional<std::string> ExecutionContext::get_environment_variable(const std::string& name) const {
    auto it = environment_vars.find(name);
    if (it != environment_vars.end()) {
        return it->second;
    }
    return {};
}

} // namespace scratchpad