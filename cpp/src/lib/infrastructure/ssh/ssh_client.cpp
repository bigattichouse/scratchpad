#include "infrastructure/ssh/ssh_client.hpp"
#include "scratchpad/errors.hpp"
#include <libssh/libssh.h>
#include <filesystem>
#include <fstream>

namespace scratchpad::infrastructure {

class SSHClientImpl : public SSHConnection {
public:
    SSHClientImpl(const SSHCredentials& credentials)
        : SSHConnection(credentials)
        , session_(nullptr)
        , channel_(nullptr) {
    }
    
    ~SSHClientImpl() override {
        cleanup();
    }
    
    void connect() override {
        if (is_connected()) {
            return; // Already connected
        }
        
        set_status(ConnectionStatus::Connecting);
        
        try {
            // Create SSH session
            session_ = ssh_new();
            if (!session_) {
                throw_ssh_error("Failed to create SSH session");
            }
            
            // Configure session
            configure_session();
            
            // Connect to server
            int rc = ssh_connect(session_);
            if (rc != SSH_OK) {
                throw_ssh_error("Connection failed: " + std::string(ssh_get_error(session_)));
            }
            
            // Verify server (for now, accept all hosts - in production, should verify)
            verify_server();
            
            // Authenticate
            authenticate();
            
            set_status(ConnectionStatus::Connected);
            update_last_activity();
            
        } catch (...) {
            set_status(ConnectionStatus::Failed);
            cleanup();
            throw;
        }
    }
    
    void disconnect() override {
        if (is_disconnected()) {
            return; // Already disconnected
        }
        
        cleanup();
        set_status(ConnectionStatus::Disconnected);
    }
    
    CommandResult execute_command(const std::string& command,
                                 const ExecutionContext& context) {
        if (!is_connected()) {
            THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed,
                           "Not connected to SSH server",
                           credentials().username,
                           credentials().port);
        }
        
        // Create execution channel
        ssh_channel channel = ssh_channel_new(session_);
        if (!channel) {
            throw_ssh_error("Failed to create SSH channel");
        }
        
        // Ensure cleanup on exit
        auto channel_guard = [channel]() { 
            ssh_channel_close(channel);
            ssh_channel_free(channel);
        };
        
        try {
            // Open session channel
            int rc = ssh_channel_open_session(channel);
            if (rc != SSH_OK) {
                throw_ssh_error("Failed to open SSH session channel");
            }
            
            // Set environment variables
            for (const auto& [name, value] : context.environment_vars) {
                ssh_channel_request_env(channel, name.c_str(), value.c_str());
            }
            
            // Build command with context
            std::string full_command = build_command_with_context(command, context);
            
            // Execute command
            auto start_time = std::chrono::steady_clock::now();
            rc = ssh_channel_request_exec(channel, full_command.c_str());
            if (rc != SSH_OK) {
                throw_ssh_error("Failed to execute command");
            }
            
            // Read output
            CommandResult result = read_command_output(channel, context.timeout);
            
            // Get exit status
            result.exit_code = ssh_channel_get_exit_status(channel);
            
            // Calculate execution time
            auto end_time = std::chrono::steady_clock::now();
            result.execution_time = std::chrono::duration_cast<std::chrono::milliseconds>(
                end_time - start_time
            );
            
            update_last_activity();
            channel_guard();
            
            return result;
            
        } catch (...) {
            channel_guard();
            throw;
        }
    }
    
    std::unique_ptr<ssh_channel, void(*)(ssh_channel)> create_shell_channel() {
        if (!is_connected()) {
            THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed,
                           "Not connected to SSH server",
                           credentials().username,
                           credentials().port);
        }
        
        ssh_channel channel = ssh_channel_new(session_);
        if (!channel) {
            throw_ssh_error("Failed to create SSH channel");
        }
        
        auto deleter = [](ssh_channel ch) {
            if (ch) {
                ssh_channel_close(ch);
                ssh_channel_free(ch);
            }
        };
        
        std::unique_ptr<ssh_channel, void(*)(ssh_channel)> channel_ptr(channel, deleter);
        
        // Open session channel
        int rc = ssh_channel_open_session(channel);
        if (rc != SSH_OK) {
            throw_ssh_error("Failed to open SSH session channel");
        }
        
        // Request PTY
        rc = ssh_channel_request_pty(channel);
        if (rc != SSH_OK) {
            throw_ssh_error("Failed to request PTY");
        }
        
        // Start shell
        rc = ssh_channel_request_shell(channel);
        if (rc != SSH_OK) {
            throw_ssh_error("Failed to start shell");
        }
        
        update_last_activity();
        
        return channel_ptr;
    }
    
private:
    ssh_session session_;
    ssh_channel channel_;
    
    void configure_session() {
        const auto& creds = credentials();
        
        // Set hostname/IP - for VM connections, always localhost
        ssh_options_set(session_, SSH_OPTIONS_HOST, "localhost");
        
        // Set port
        ssh_options_set(session_, SSH_OPTIONS_PORT, &creds.port);
        
        // Set username
        ssh_options_set(session_, SSH_OPTIONS_USER, creds.username.c_str());
        
        // Set timeout
        auto timeout_seconds = static_cast<long>(creds.timeout.count() / 1000);
        ssh_options_set(session_, SSH_OPTIONS_TIMEOUT, &timeout_seconds);
        
        // Disable host key checking for VM connections (they use ephemeral keys)
        int no_hostkey_check = 1;
        ssh_options_set(session_, SSH_OPTIONS_STRICTHOSTKEYCHECK, &no_hostkey_check);
        
        // Set connection timeout
        ssh_options_set(session_, SSH_OPTIONS_TIMEOUT_USEC, &creds.timeout);
    }
    
    void verify_server() {
        // For VM connections, we typically don't verify the host key
        // since VMs use ephemeral keys. In a production environment,
        // you might want to implement proper host key verification.
        
        // Get server public key
        ssh_key srv_pubkey = nullptr;
        int rc = ssh_get_server_publickey(session_, &srv_pubkey);
        if (rc < 0) {
            throw_ssh_error("Failed to get server public key");
        }
        
        // For now, just accept the key
        ssh_key_free(srv_pubkey);
    }
    
    void authenticate() {
        const auto& creds = credentials();
        
        // Check what authentication methods are available
        int rc = ssh_userauth_none(session_, nullptr);
        if (rc == SSH_AUTH_ERROR) {
            throw_ssh_error("Authentication failed: " + std::string(ssh_get_error(session_)));
        }
        
        int method = ssh_userauth_list(session_, nullptr);
        
        // Try public key authentication
        if (method & SSH_AUTH_METHOD_PUBLICKEY) {
            rc = ssh_userauth_publickey_auto(session_, nullptr, nullptr);
            if (rc == SSH_AUTH_SUCCESS) {
                return; // Success
            }
            
            // Try with explicit key file
            if (!creds.private_key_path.empty()) {
                ssh_key private_key;
                rc = ssh_pki_import_privkey_file(creds.private_key_path.c_str(),
                                               nullptr, nullptr, nullptr, &private_key);
                if (rc == SSH_OK) {
                    rc = ssh_userauth_publickey(session_, nullptr, private_key);
                    ssh_key_free(private_key);
                    
                    if (rc == SSH_AUTH_SUCCESS) {
                        return; // Success
                    }
                }
            }
        }
        
        throw_ssh_error("Authentication failed - no suitable method found");
    }
    
    std::string build_command_with_context(const std::string& command,
                                         const ExecutionContext& context) {
        std::string full_command;
        
        // Add working directory if specified
        if (context.working_directory.has_value()) {
            full_command += "cd " + context.working_directory.value() + " && ";
        }
        
        // Add environment variables (already set via SSH channel, but add as backup)
        for (const auto& [name, value] : context.environment_vars) {
            full_command += name + "=" + value + " ";
        }
        
        full_command += command;
        
        return full_command;
    }
    
    CommandResult read_command_output(ssh_channel channel, std::chrono::milliseconds timeout) {
        CommandResult result;
        result.exit_code = 0;
        
        const size_t buffer_size = 8192;
        char buffer[buffer_size];
        
        auto start_time = std::chrono::steady_clock::now();
        
        // Read stdout and stderr
        while (ssh_channel_is_open(channel) && !ssh_channel_is_eof(channel)) {
            // Check timeout
            auto elapsed = std::chrono::steady_clock::now() - start_time;
            if (elapsed > timeout) {
                break;
            }
            
            // Read stdout
            int nbytes = ssh_channel_read_timeout(channel, buffer, buffer_size, 0, 100);
            if (nbytes > 0) {
                result.stdout_output.append(buffer, nbytes);
            }
            
            // Read stderr
            nbytes = ssh_channel_read_timeout(channel, buffer, buffer_size, 1, 100);
            if (nbytes > 0) {
                result.stderr_output.append(buffer, nbytes);
            }
            
            // Small delay to prevent busy waiting
            if (nbytes <= 0) {
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        }
        
        return result;
    }
    
    void cleanup() {
        if (channel_) {
            ssh_channel_close(channel_);
            ssh_channel_free(channel_);
            channel_ = nullptr;
        }
        
        if (session_) {
            ssh_disconnect(session_);
            ssh_free(session_);
            session_ = nullptr;
        }
    }
    
    void throw_ssh_error(const std::string& message) {
        std::string full_message = message;
        if (session_) {
            full_message += " (" + std::string(ssh_get_error(session_)) + ")";
        }
        
        set_last_error(full_message);
        THROW_SSH_ERROR(ErrorCode::SSHConnectionFailed,
                       full_message,
                       credentials().username,
                       credentials().port);
    }
};

// SSHClient implementation

SSHClient::SSHClient() {
    // Initialize libssh
    ssh_init();
}

SSHClient::~SSHClient() {
    // Cleanup libssh
    ssh_finalize();
}

std::unique_ptr<SSHConnection> SSHClient::create_connection(const SSHCredentials& credentials) {
    return std::make_unique<SSHClientImpl>(credentials);
}

bool SSHClient::is_available() {
    // Check if libssh is properly initialized
    return true; // libssh should be available if we got here
}

std::string SSHClient::get_version() {
    return ssh_version(0);
}

bool SSHClient::validate_private_key(const std::string& key_path) {
    if (!std::filesystem::exists(key_path)) {
        return false;
    }
    
    ssh_key key;
    int rc = ssh_pki_import_privkey_file(key_path.c_str(), nullptr, nullptr, nullptr, &key);
    if (rc == SSH_OK) {
        ssh_key_free(key);
        return true;
    }
    
    return false;
}

bool SSHClient::validate_public_key(const std::string& key_path) {
    if (!std::filesystem::exists(key_path)) {
        return false;
    }
    
    ssh_key key;
    int rc = ssh_pki_import_pubkey_file(key_path.c_str(), &key);
    if (rc == SSH_OK) {
        ssh_key_free(key);
        return true;
    }
    
    return false;
}

void SSHClient::generate_key_pair(const std::string& private_key_path,
                                 const std::string& public_key_path,
                                 KeyType key_type,
                                 size_t key_size) {
    // Create directory for keys if it doesn't exist
    std::filesystem::create_directories(std::filesystem::path(private_key_path).parent_path());
    
    // Generate key pair
    ssh_key key;
    int rc;
    
    switch (key_type) {
        case KeyType::RSA:
            rc = ssh_pki_generate(SSH_KEYTYPE_RSA, key_size, &key);
            break;
        case KeyType::ED25519:
            rc = ssh_pki_generate(SSH_KEYTYPE_ED25519, 0, &key); // ED25519 doesn't use key_size
            break;
        default:
            THROW_SSH_ERROR(ErrorCode::InvalidArgument, "Unsupported key type");
    }
    
    if (rc != SSH_OK) {
        THROW_SSH_ERROR(ErrorCode::SSHKeyNotFound, "Failed to generate SSH key pair");
    }
    
    // Export private key
    rc = ssh_pki_export_privkey_file(key, nullptr, nullptr, nullptr, private_key_path.c_str());
    if (rc != SSH_OK) {
        ssh_key_free(key);
        THROW_SSH_ERROR(ErrorCode::SSHKeyNotFound, 
                       "Failed to export private key to " + private_key_path);
    }
    
    // Export public key
    rc = ssh_pki_export_pubkey_file(key, public_key_path.c_str());
    if (rc != SSH_OK) {
        ssh_key_free(key);
        THROW_SSH_ERROR(ErrorCode::SSHKeyNotFound,
                       "Failed to export public key to " + public_key_path);
    }
    
    ssh_key_free(key);
    
    // Set proper permissions on private key (600)
    std::filesystem::permissions(private_key_path,
                                std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
                                std::filesystem::perm_options::replace);
}

bool SSHClient::test_connection(const SSHCredentials& credentials) {
    try {
        auto connection = create_connection(credentials);
        connection->connect();
        connection->disconnect();
        return true;
    } catch (...) {
        return false;
    }
}

} // namespace scratchpad::infrastructure