#pragma once

#include "domain/communication/ssh_connection.hpp"
#include "domain/communication/command_execution.hpp"
#include <memory>
#include <string>

// Forward declarations for libssh types
typedef struct ssh_channel_struct* ssh_channel;

namespace scratchpad::infrastructure {

/**
 * SSH key types for key generation
 */
enum class KeyType {
    RSA,
    ED25519
};

/**
 * Infrastructure adapter for SSH communication
 * 
 * Provides concrete implementation of SSH connections using libssh,
 * handles key management, and provides utility functions for SSH operations.
 */
class SSHClient {
public:
    /**
     * Constructor - initializes libssh
     */
    SSHClient();
    
    /**
     * Destructor - cleans up libssh
     */
    ~SSHClient();

    // Non-copyable, movable
    SSHClient(const SSHClient&) = delete;
    SSHClient& operator=(const SSHClient&) = delete;
    SSHClient(SSHClient&&) = default;
    SSHClient& operator=(SSHClient&&) = default;

    // ========== Connection Management ==========

    /**
     * Create new SSH connection
     * @param credentials SSH connection credentials
     * @return SSH connection instance
     * @throws SSHError if credentials are invalid
     */
    std::unique_ptr<SSHConnection> create_connection(const SSHCredentials& credentials);

    // ========== System Information ==========

    /**
     * Check if SSH client is available
     * @return true if libssh is properly initialized
     */
    static bool is_available();

    /**
     * Get libssh version information
     * @return Version string
     */
    static std::string get_version();

    // ========== Key Management ==========

    /**
     * Validate private key file
     * @param key_path Path to private key file
     * @return true if key is valid
     */
    static bool validate_private_key(const std::string& key_path);

    /**
     * Validate public key file
     * @param key_path Path to public key file
     * @return true if key is valid
     */
    static bool validate_public_key(const std::string& key_path);

    /**
     * Generate SSH key pair
     * @param private_key_path Path for private key file
     * @param public_key_path Path for public key file
     * @param key_type Type of key to generate
     * @param key_size Key size in bits (for RSA, ignored for ED25519)
     * @throws SSHError if key generation fails
     */
    static void generate_key_pair(const std::string& private_key_path,
                                 const std::string& public_key_path,
                                 KeyType key_type = KeyType::RSA,
                                 size_t key_size = 2048);

    // ========== Connection Testing ==========

    /**
     * Test SSH connection without keeping it open
     * @param credentials SSH connection credentials
     * @return true if connection successful
     */
    static bool test_connection(const SSHCredentials& credentials);
};

/**
 * Extended SSH connection interface with infrastructure-specific methods
 */
class ExtendedSSHConnection : public SSHConnection {
public:
    using SSHConnection::SSHConnection;

    /**
     * Execute command with full context support
     * @param command Command to execute
     * @param context Execution context
     * @return Command result
     * @throws SSHError if execution fails
     */
    virtual CommandResult execute_command(const std::string& command,
                                        const ExecutionContext& context = ExecutionContext::create_default()) = 0;

    /**
     * Create interactive shell channel
     * @return Shell channel for interactive use
     * @throws SSHError if shell creation fails
     */
    virtual std::unique_ptr<ssh_channel, void(*)(ssh_channel)> create_shell_channel() = 0;
};

} // namespace scratchpad::infrastructure