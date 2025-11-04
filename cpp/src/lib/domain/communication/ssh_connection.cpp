#include "scratchpad/domain/communication/ssh_connection.hpp"
#include "scratchpad/errors.hpp"

namespace scratchpad {

SSHConnection::SSHConnection(const SSHCredentials& credentials)
    : credentials_(credentials)
    , status_(ConnectionStatus::Disconnected)
    , created_at_(std::chrono::system_clock::now())
    , last_activity_(created_at_) {
    
    validate_credentials();
}

SSHConnection::~SSHConnection() {
    if (status_ == ConnectionStatus::Connected) {
        try {
            disconnect();
        } catch (...) {
            // Ignore errors during destruction
        }
    }
}

void SSHConnection::set_status(ConnectionStatus new_status) {
    if (new_status == status_) {
        return; // No change
    }
    
    auto now = std::chrono::system_clock::now();
    
    // Validate status transitions
    if (!is_valid_status_transition(status_, new_status)) {
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed,
                       "Invalid status transition from " + 
                       std::string(status_to_string(status_)) + " to " +
                       std::string(status_to_string(new_status)),
                       credentials_.username + "@" + std::to_string(credentials_.port),
                       credentials_.port);
    }
    
    // Update connection time tracking
    if (status_ == ConnectionStatus::Connected && new_status != ConnectionStatus::Connected) {
        auto session_duration = std::chrono::duration_cast<std::chrono::milliseconds>(
            now - last_status_change_
        );
        total_connected_time_ += session_duration;
    }
    
    ConnectionStatus old_status = status_;
    status_ = new_status;
    last_status_change_ = now;
    last_activity_ = now;
    
    // Update specific timestamps
    if (new_status == ConnectionStatus::Connected && old_status != ConnectionStatus::Connected) {
        connected_at_ = now;
        connection_count_++;
    } else if (new_status == ConnectionStatus::Disconnected && old_status == ConnectionStatus::Connected) {
        disconnected_at_ = now;
    }
    
    // Add to status history
    status_history_.emplace_back(old_status, new_status, now);
    
    // Keep history limited
    if (status_history_.size() > 100) {
        status_history_.pop_front();
    }
}

void SSHConnection::set_last_error(const std::string& error_message) {
    last_error_ = error_message;
    last_error_time_ = std::chrono::system_clock::now();
    
    // Add to error history
    error_history_.emplace_back(last_error_time_.value(), error_message);
    
    // Keep error history limited
    if (error_history_.size() > 50) {
        error_history_.pop_front();
    }
}

void SSHConnection::update_last_activity() {
    last_activity_ = std::chrono::system_clock::now();
}

std::chrono::milliseconds SSHConnection::get_current_connection_duration() const {
    if (status_ == ConnectionStatus::Connected && connected_at_.has_value()) {
        auto now = std::chrono::system_clock::now();
        return std::chrono::duration_cast<std::chrono::milliseconds>(
            now - connected_at_.value()
        );
    }
    
    return std::chrono::milliseconds::zero();
}

std::chrono::milliseconds SSHConnection::get_total_connection_time() const {
    auto total = total_connected_time_;
    
    // Add current session if connected
    if (status_ == ConnectionStatus::Connected) {
        total += get_current_connection_duration();
    }
    
    return total;
}

std::chrono::milliseconds SSHConnection::get_idle_time() const {
    auto now = std::chrono::system_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_activity_);
}

bool SSHConnection::is_idle_timeout(std::chrono::milliseconds timeout) const {
    return get_idle_time() > timeout;
}

bool SSHConnection::is_connected() const {
    return status_ == ConnectionStatus::Connected;
}

bool SSHConnection::is_healthy() const {
    // Connection is healthy if:
    // 1. Currently connected
    // 2. No recent errors (last 5 minutes)
    // 3. Not idle for too long (30 minutes)
    
    if (!is_connected()) {
        return false;
    }
    
    // Check for recent errors
    if (last_error_time_.has_value()) {
        auto now = std::chrono::system_clock::now();
        auto time_since_error = std::chrono::duration_cast<std::chrono::minutes>(
            now - last_error_time_.value()
        );
        if (time_since_error < std::chrono::minutes(5)) {
            return false;
        }
    }
    
    // Check idle time
    if (is_idle_timeout(std::chrono::minutes(30))) {
        return false;
    }
    
    return true;
}

std::vector<SSHConnection::ErrorEntry> SSHConnection::get_recent_errors(size_t count) const {
    std::vector<ErrorEntry> recent;
    recent.reserve(std::min(count, error_history_.size()));
    
    auto start = error_history_.size() > count ? 
                 error_history_.end() - count : error_history_.begin();
    
    std::copy(start, error_history_.end(), std::back_inserter(recent));
    return recent;
}

std::vector<SSHConnection::ErrorEntry> SSHConnection::get_errors_since(
    std::chrono::system_clock::time_point since) const {
    
    std::vector<ErrorEntry> filtered;
    
    for (const auto& entry : error_history_) {
        if (entry.timestamp >= since) {
            filtered.push_back(entry);
        }
    }
    
    return filtered;
}

void SSHConnection::validate_credentials() const {
    if (credentials_.username.empty()) {
        THROW_SSH_ERROR(ErrorCode::SSHAuthenticationFailed,
                       "SSH username cannot be empty",
                       credentials_.username,
                       credentials_.port);
    }
    
    if (credentials_.private_key_path.empty()) {
        THROW_SSH_ERROR(ErrorCode::SSHKeyNotFound,
                       "SSH private key path cannot be empty",
                       credentials_.username,
                       credentials_.port);
    }
    
    if (credentials_.port == 0 || credentials_.port > 65535) {
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed,
                       "SSH port must be between 1 and 65535",
                       credentials_.username,
                       credentials_.port);
    }
    
    if (credentials_.timeout <= std::chrono::milliseconds::zero()) {
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed,
                       "SSH timeout must be positive",
                       credentials_.username,
                       credentials_.port);
    }
}

bool SSHConnection::is_valid_status_transition(ConnectionStatus from, ConnectionStatus to) {
    // Same status is always valid (no-op)
    if (from == to) {
        return true;
    }
    
    switch (from) {
        case ConnectionStatus::Disconnected:
            return to == ConnectionStatus::Connecting;
            
        case ConnectionStatus::Connecting:
            return to == ConnectionStatus::Connected || 
                   to == ConnectionStatus::Failed ||
                   to == ConnectionStatus::Disconnected;
            
        case ConnectionStatus::Connected:
            return to == ConnectionStatus::Disconnected || to == ConnectionStatus::Failed;
            
        case ConnectionStatus::Failed:
            return to == ConnectionStatus::Disconnected || to == ConnectionStatus::Connecting;
    }
    
    return false;
}

std::string_view SSHConnection::status_to_string(ConnectionStatus status) {
    switch (status) {
        case ConnectionStatus::Disconnected:    return "disconnected";
        case ConnectionStatus::Connecting:      return "connecting";
        case ConnectionStatus::Connected:       return "connected";
        case ConnectionStatus::Failed:          return "failed";
    }
    return "unknown";
}

ConnectionStatus SSHConnection::status_from_string(std::string_view status_str) {
    if (status_str == "disconnected")   return ConnectionStatus::Disconnected;
    if (status_str == "connecting")     return ConnectionStatus::Connecting;
    if (status_str == "connected")      return ConnectionStatus::Connected;
    if (status_str == "failed")         return ConnectionStatus::Failed;
    
    THROW_SSH_ERROR(ErrorCode::InvalidArgument,
                   "Unknown connection status: " + std::string(status_str));
}

SSHCredentials SSHCredentials::create_with_key_files(
    const std::string& username,
    const std::string& private_key_path,
    const std::string& public_key_path,
    PortNumber port,
    std::chrono::milliseconds timeout) {
    
    SSHCredentials creds;
    creds.username = username;
    creds.private_key_path = private_key_path;
    creds.public_key_path = public_key_path;
    creds.port = port;
    creds.timeout = timeout;
    
    return creds;
}

SSHCredentials SSHCredentials::create_default(
    const std::string& username,
    PortNumber port) {
    
    // Use default SSH key paths
    const char* home = std::getenv("HOME");
    std::string home_dir = home ? home : "/tmp";
    
    return create_with_key_files(
        username,
        home_dir + "/.scratchpad/keys/id_rsa",
        home_dir + "/.scratchpad/keys/id_rsa.pub",
        port,
        std::chrono::milliseconds(30000)
    );
}

bool SSHCredentials::validate() const {
    return !username.empty() && 
           !private_key_path.empty() && 
           port > 0 && port <= 65535 && 
           timeout > std::chrono::milliseconds::zero();
}

} // namespace scratchpad