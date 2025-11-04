#pragma once

#include "scratchpad/types.hpp"
#include <string>
#include <chrono>
#include <optional>
#include <deque>
#include <vector>
#include <string_view>

namespace scratchpad {

/**
 * Connection status enumeration
 */
enum class ConnectionStatus {
    Disconnected,   // Not connected
    Connecting,     // Connection in progress
    Connected,      // Connected and ready
    Failed          // Connection failed
};

/**
 * Entity representing an SSH connection to a VM
 * 
 * Tracks connection state, credentials, and provides connection lifecycle
 * management with monitoring and error tracking capabilities.
 */
class SSHConnection {
public:
    /**
     * Error history entry
     */
    struct ErrorEntry {
        std::chrono::system_clock::time_point timestamp;
        std::string message;
        
        ErrorEntry(std::chrono::system_clock::time_point when, const std::string& msg)
            : timestamp(when), message(msg) {}
    };

    /**
     * Status change history entry
     */
    struct StatusChange {
        ConnectionStatus from_status;
        ConnectionStatus to_status;
        std::chrono::system_clock::time_point timestamp;
        
        StatusChange(ConnectionStatus from, ConnectionStatus to, std::chrono::system_clock::time_point when)
            : from_status(from), to_status(to), timestamp(when) {}
    };

    /**
     * Construct SSH connection with credentials
     * @param credentials SSH connection credentials
     * @throws SSHError if credentials are invalid
     */
    explicit SSHConnection(const SSHCredentials& credentials);

    /**
     * Destructor - automatically disconnects if connected
     */
    ~SSHConnection();

    // Non-copyable, movable
    SSHConnection(const SSHConnection&) = delete;
    SSHConnection& operator=(const SSHConnection&) = delete;
    SSHConnection(SSHConnection&&) = default;
    SSHConnection& operator=(SSHConnection&&) = default;

    // ========== Connection Management ==========

    /**
     * Connect to SSH server
     * @throws SSHError if connection fails
     */
    virtual void connect() = 0;

    /**
     * Disconnect from SSH server
     */
    virtual void disconnect() = 0;

    /**
     * Reconnect (disconnect and connect again)
     * @throws SSHError if reconnection fails
     */
    virtual void reconnect() {
        if (is_connected()) {
            disconnect();
        }
        connect();
    }

    // ========== Basic Properties ==========

    /**
     * Get SSH credentials
     * @return Connection credentials
     */
    const SSHCredentials& credentials() const { return credentials_; }

    /**
     * Get connection status
     * @return Current connection status
     */
    ConnectionStatus status() const { return status_; }

    /**
     * Get connection creation time
     * @return Creation timestamp
     */
    std::chrono::system_clock::time_point created_at() const { return created_at_; }

    // ========== Status Management ==========

    /**
     * Get time of last status change
     * @return Last status change timestamp
     */
    std::chrono::system_clock::time_point last_status_change() const { 
        return last_status_change_; 
    }

    /**
     * Get last activity time
     * @return Last activity timestamp
     */
    std::chrono::system_clock::time_point last_activity() const { return last_activity_; }

    /**
     * Update last activity timestamp
     */
    void update_last_activity();

    // ========== Error Tracking ==========

    /**
     * Get last error message
     * @return Last error if any occurred
     */
    std::optional<std::string> last_error() const { return last_error_; }

    /**
     * Get time of last error
     * @return Last error timestamp if any error occurred
     */
    std::optional<std::chrono::system_clock::time_point> last_error_time() const { 
        return last_error_time_; 
    }

    /**
     * Get error history
     * @return All error entries
     */
    const std::deque<ErrorEntry>& error_history() const { return error_history_; }

    /**
     * Get recent error entries
     * @param count Maximum number of errors to return
     * @return Recent error entries
     */
    std::vector<ErrorEntry> get_recent_errors(size_t count = 10) const;

    /**
     * Get errors since specific time
     * @param since Timestamp to filter from
     * @return Filtered error entries
     */
    std::vector<ErrorEntry> get_errors_since(std::chrono::system_clock::time_point since) const;

    // ========== Connection Statistics ==========

    /**
     * Get number of successful connections
     * @return Connection count
     */
    uint32_t connection_count() const { return connection_count_; }

    /**
     * Get current connection duration (if connected)
     * @return Duration of current connection
     */
    std::chrono::milliseconds get_current_connection_duration() const;

    /**
     * Get total connection time across all sessions
     * @return Total connected time
     */
    std::chrono::milliseconds get_total_connection_time() const;

    /**
     * Get idle time (time since last activity)
     * @return Idle duration
     */
    std::chrono::milliseconds get_idle_time() const;

    /**
     * Check if connection has been idle for too long
     * @param timeout Idle timeout threshold
     * @return true if idle time exceeds timeout
     */
    bool is_idle_timeout(std::chrono::milliseconds timeout) const;

    /**
     * Get connected timestamp
     * @return Connection time if currently or previously connected
     */
    std::optional<std::chrono::system_clock::time_point> connected_at() const { 
        return connected_at_; 
    }

    /**
     * Get disconnected timestamp
     * @return Disconnection time if previously connected
     */
    std::optional<std::chrono::system_clock::time_point> disconnected_at() const { 
        return disconnected_at_; 
    }

    // ========== Status Queries ==========

    /**
     * Check if currently connected
     * @return true if status is Connected
     */
    bool is_connected() const;

    /**
     * Check if connection is in progress
     * @return true if status is Connecting
     */
    bool is_connecting() const { return status_ == ConnectionStatus::Connecting; }

    /**
     * Check if connection is disconnected
     * @return true if status is Disconnected
     */
    bool is_disconnected() const { return status_ == ConnectionStatus::Disconnected; }

    /**
     * Check if connection failed
     * @return true if status is Failed
     */
    bool is_failed() const { return status_ == ConnectionStatus::Failed; }

    /**
     * Check if connection is healthy
     * @return true if connected with no recent errors or excessive idle time
     */
    bool is_healthy() const;

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
    static bool is_valid_status_transition(ConnectionStatus from, ConnectionStatus to);

    /**
     * Convert status to string
     * @param status Connection status
     * @return Status string
     */
    static std::string_view status_to_string(ConnectionStatus status);

    /**
     * Convert string to status
     * @param status_str Status string
     * @return Connection status
     * @throws SSHError if string is invalid
     */
    static ConnectionStatus status_from_string(std::string_view status_str);

protected:
    /**
     * Set connection status (for derived classes)
     * @param new_status New status
     * @throws SSHError if transition is invalid
     */
    void set_status(ConnectionStatus new_status);

    /**
     * Set last error (for derived classes)
     * @param error_message Error message
     */
    void set_last_error(const std::string& error_message);

private:
    // Core properties
    SSHCredentials credentials_;
    
    // Status tracking
    ConnectionStatus status_;
    std::optional<std::string> last_error_;
    std::optional<std::chrono::system_clock::time_point> last_error_time_;
    
    // Timestamps
    std::chrono::system_clock::time_point created_at_;
    std::chrono::system_clock::time_point last_status_change_;
    std::chrono::system_clock::time_point last_activity_;
    std::optional<std::chrono::system_clock::time_point> connected_at_;
    std::optional<std::chrono::system_clock::time_point> disconnected_at_;
    
    // Statistics
    uint32_t connection_count_ = 0;
    std::chrono::milliseconds total_connected_time_{0};
    
    // History tracking
    std::deque<StatusChange> status_history_;
    std::deque<ErrorEntry> error_history_;

    void validate_credentials() const;
};

// ========== SSH Credentials Helper Functions ==========

/**
 * SSH credentials factory functions
 */
namespace ssh_credentials {

/**
 * Create SSH credentials with explicit key file paths
 * @param username SSH username
 * @param private_key_path Path to private key file
 * @param public_key_path Path to public key file
 * @param port SSH port
 * @param timeout Connection timeout
 * @return SSH credentials
 */
inline SSHCredentials create_with_key_files(
    const std::string& username,
    const std::string& private_key_path,
    const std::string& public_key_path,
    PortNumber port,
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000)) {
    
    return SSHCredentials::create_with_key_files(username, private_key_path, public_key_path, port, timeout);
}

/**
 * Create SSH credentials with default key paths
 * @param username SSH username
 * @param port SSH port
 * @return SSH credentials with default paths
 */
inline SSHCredentials create_default(const std::string& username, PortNumber port) {
    return SSHCredentials::create_default(username, port);
}

} // namespace ssh_credentials

} // namespace scratchpad