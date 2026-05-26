#pragma once

#include "scratchpad/types.hpp"
#include "scratchpad/errors.hpp"
#include <memory>
#include <vector>
#include <optional>
#include <future>
#include <chrono>
#include <functional>

namespace scratchpad {

// Forward declarations
class VMManager;

/**
 * SSH client interface for remote command execution and file transfer
 * This is a stub header for test compilation
 */
class SSHClient {
public:
    enum class ConnectionStatus {
        Disconnected,
        Connecting,
        Connected,
        Failed
    };

    struct Options {
        std::chrono::seconds connection_timeout{30};
        std::chrono::seconds command_timeout{300};
        size_t max_retries = 3;
        std::chrono::milliseconds retry_delay{1000};
        std::chrono::seconds keepalive_interval{60};
        bool compression_enabled = true;
        std::vector<std::string> key_exchange_algorithms;
        std::vector<std::string> host_key_algorithms;
        std::vector<std::string> ciphers;
    };

    struct ConnectionInfo {
        std::string host;
        PortNumber port = 22;
        std::string username;
        std::string private_key_path;
        std::string known_hosts_file;
        std::optional<std::string> password;
    };

    struct ConnectionResult {
        bool success;
        std::string error_message;
        ConnectionStatus status;
    };

    // Using global CommandResult type from types.hpp for compatibility

    struct FileTransferResult {
        bool success;
        std::string error_message;
        size_t bytes_transferred = 0;
        std::chrono::duration<double> transfer_time{0};
    };

    struct ConnectionState {
        ConnectionStatus status;
        std::string host;
        PortNumber port = 0;
        std::string username;
        std::chrono::system_clock::time_point connected_since;
    };

    struct ConnectionStats {
        size_t bytes_sent = 0;
        size_t bytes_received = 0;
        size_t commands_executed = 0;
        size_t files_transferred = 0;
        std::chrono::duration<double> total_connection_time{0};
    };

    using ProgressCallback = std::function<void(const std::string& operation, size_t current, size_t total, const std::string& message)>;

    SSHClient();
    explicit SSHClient(const Options& options);
    virtual ~SSHClient() = default;

    // Connection management
    virtual ConnectionResult connect(const ConnectionInfo& info);
    virtual void disconnect();
    virtual bool is_connected() const;
    virtual ConnectionState get_connection_state() const;
    virtual void validate_connection_info(const ConnectionInfo& info) const;
    
    // Factory methods (implementation compatibility)
    virtual std::unique_ptr<SSHConnection> create_connection(const SSHCredentials& credentials);

    // Command execution
    virtual CommandResult execute_command(const std::string& command);
    virtual CommandResult execute_command(const std::string& command, std::chrono::milliseconds timeout);
    virtual std::future<CommandResult> execute_command_async(const std::string& command);
    
    // Implementation compatibility - command execution with connection and params
    virtual CommandResult execute_command(SSHConnection& connection, const VMManager::ExecuteParams& params);
    virtual void copy_file_to_remote(SSHConnection& connection, const std::string& source, const std::string& destination);
    virtual void copy_file_from_remote(SSHConnection& connection, const std::string& source, const std::string& destination);

    // File transfer
    virtual FileTransferResult upload_file(const std::string& local_path, const std::string& remote_path);
    virtual FileTransferResult download_file(const std::string& remote_path, const std::string& local_path);
    virtual FileTransferResult upload_directory(const std::string& local_dir, const std::string& remote_dir);
    virtual FileTransferResult download_directory(const std::string& remote_dir, const std::string& local_dir);

    // Progress tracking
    virtual void set_progress_callback(ProgressCallback callback);

    // Information and debugging
    virtual std::string get_last_error() const;
    virtual ConnectionStats get_connection_stats() const;
    virtual const Options& get_options() const;

private:
    class Impl {
    public:
        virtual ~Impl() = default;
    };
    std::unique_ptr<Impl> impl_;
};

} // namespace scratchpad