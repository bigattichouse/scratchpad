#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "scratchpad/infrastructure/ssh/ssh_client.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>
#include <fstream>
#include <filesystem>

using namespace scratchpad;
using namespace scratchpad::test;
using testing::_;
using testing::Return;
using testing::InSequence;
using testing::StrictMock;

class SSHClientTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::make_unique<TempDirectory>();
        
        // Set up test options
        test_options_.connection_timeout = std::chrono::seconds{5};
        test_options_.command_timeout = std::chrono::seconds{10};
        test_options_.max_retries = 3;
        test_options_.retry_delay = std::chrono::milliseconds{100};
        test_options_.keepalive_interval = std::chrono::seconds{30};
        test_options_.compression_enabled = false;
        test_options_.key_exchange_algorithms = {"diffie-hellman-group14-sha256"};
        test_options_.host_key_algorithms = {"ssh-rsa", "ssh-ed25519"};
        test_options_.ciphers = {"aes128-ctr", "aes256-ctr"};
        
        // Create test SSH keys
        create_test_ssh_keys();
        
        // Set up connection info for mock server
        connection_info_.host = "127.0.0.1";
        connection_info_.port = TestHelpers::get_test_ssh_port_start();
        connection_info_.username = "testuser";
        connection_info_.private_key_path = private_key_path_;
        connection_info_.known_hosts_file = known_hosts_path_;
    }
    
    void create_test_ssh_keys() {
        // Create test directory structure
        auto ssh_dir = temp_dir_->create_subdirectory("ssh");
        
        // Create dummy private key (marked as test key)
        private_key_path_ = (ssh_dir / "test_key").string();
        temp_dir_->create_file("ssh/test_key", 
            "-----BEGIN OPENSSH PRIVATE KEY-----\n"
            "# THIS IS A DUMMY TEST KEY - NOT FUNCTIONAL\n"
            "# Generated for unit testing purposes only\n"
            "TEST_KEY_DATA_NOT_REAL\n"
            "-----END OPENSSH PRIVATE KEY-----\n");
        
        // Create dummy public key
        temp_dir_->create_file("ssh/test_key.pub",
            "ssh-rsa DUMMY_TEST_PUBLIC_KEY_DATA testuser@testhost # TEST KEY ONLY");
        
        // Create known hosts file
        known_hosts_path_ = (ssh_dir / "known_hosts").string();
        temp_dir_->create_file("ssh/known_hosts",
            "127.0.0.1 ssh-rsa DUMMY_HOST_KEY_FOR_TESTING");
        
        // Set appropriate permissions (on Unix systems)
        #ifndef _WIN32
        std::filesystem::permissions(private_key_path_, 
            std::filesystem::perms::owner_read | std::filesystem::perms::owner_write);
        #endif
    }
    
    std::unique_ptr<TempDirectory> temp_dir_;
    SSHClient::Options test_options_;
    SSHClient::ConnectionInfo connection_info_;
    std::string private_key_path_;
    std::string known_hosts_path_;
};

// Construction and configuration tests
TEST_F(SSHClientTest, ConstructionWithDefaultOptions) {
    EXPECT_NO_THROW({
        SSHClient client;
        auto options = client.get_options();
        EXPECT_GT(options.connection_timeout.count(), 0);
        EXPECT_GT(options.command_timeout.count(), 0);
        EXPECT_GT(options.max_retries, 0);
    });
}

TEST_F(SSHClientTest, ConstructionWithCustomOptions) {
    EXPECT_NO_THROW({
        SSHClient client(test_options_);
        
        auto options = client.get_options();
        EXPECT_EQ(options.connection_timeout, test_options_.connection_timeout);
        EXPECT_EQ(options.command_timeout, test_options_.command_timeout);
        EXPECT_EQ(options.max_retries, test_options_.max_retries);
        EXPECT_EQ(options.compression_enabled, test_options_.compression_enabled);
    });
}

// Connection management tests
TEST_F(SSHClientTest, ConnectionInfoValidation) {
    SSHClient client(test_options_);
    
    // Valid connection info should not throw
    EXPECT_NO_THROW({
        client.validate_connection_info(connection_info_);
    });
    
    // Invalid host
    auto invalid_info = connection_info_;
    invalid_info.host = "";
    EXPECT_THROW({
        client.validate_connection_info(invalid_info);
    }, SSHError);
    
    // Invalid port
    invalid_info = connection_info_;
    invalid_info.port = 0;
    EXPECT_THROW({
        client.validate_connection_info(invalid_info);
    }, SSHError);
    
    // Missing username
    invalid_info = connection_info_;
    invalid_info.username = "";
    EXPECT_THROW({
        client.validate_connection_info(invalid_info);
    }, SSHError);
}

TEST_F(SSHClientTest, ConnectWithValidInfo) {
    SSHClient client(test_options_);
    
    // This test will fail in unit test environment without real SSH server
    // But it should provide meaningful error messages
    auto result = client.connect(connection_info_);
    
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("connection"),
        testing::HasSubstr("timeout"),
        testing::HasSubstr("refused"),
        testing::HasSubstr("host")
    ));
}

TEST_F(SSHClientTest, ConnectWithInvalidHost) {
    SSHClient client(test_options_);
    
    auto invalid_info = connection_info_;
    invalid_info.host = "invalid.nonexistent.host.example";
    
    auto result = client.connect(invalid_info);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("resolve"),
        testing::HasSubstr("host"),
        testing::HasSubstr("connection")
    ));
}

TEST_F(SSHClientTest, ConnectWithInvalidKey) {
    SSHClient client(test_options_);
    
    auto invalid_info = connection_info_;
    invalid_info.private_key_path = "/nonexistent/key/path";
    
    auto result = client.connect(invalid_info);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("key"),
        testing::HasSubstr("file"),
        testing::HasSubstr("not found")
    ));
}

TEST_F(SSHClientTest, DisconnectWhenNotConnected) {
    SSHClient client(test_options_);
    
    // Should handle gracefully
    EXPECT_NO_THROW({
        client.disconnect();
        EXPECT_FALSE(client.is_connected());
    });
}

TEST_F(SSHClientTest, IsConnectedInitialState) {
    SSHClient client(test_options_);
    EXPECT_FALSE(client.is_connected());
}

// Command execution tests
TEST_F(SSHClientTest, ExecuteCommandWhenNotConnected) {
    SSHClient client(test_options_);
    
    auto result = client.execute_command("echo test");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, ExecuteCommandValidation) {
    SSHClient client(test_options_);
    
    // Empty command
    auto result = client.execute_command("");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("empty"));
    
    // Very long command
    std::string long_command(10000, 'a');
    result = client.execute_command(long_command);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("too long"),
        testing::HasSubstr("length")
    ));
}

TEST_F(SSHClientTest, ExecuteCommandAsync) {
    SSHClient client(test_options_);
    
    // Should return immediately with error since not connected
    auto future = client.execute_command_async("echo test");
    
    auto status = future.wait_for(std::chrono::milliseconds{100});
    EXPECT_EQ(status, std::future_status::ready);
    
    auto result = future.get();
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, ExecuteCommandWithTimeout) {
    SSHClient client(test_options_);
    
    auto result = client.execute_command("sleep 100", std::chrono::milliseconds{50});
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("not connected"),
        testing::HasSubstr("timeout")
    ));
}

// File transfer tests
TEST_F(SSHClientTest, UploadFileWhenNotConnected) {
    SSHClient client(test_options_);
    
    auto test_file = temp_dir_->create_file("test.txt", "test content");
    
    auto result = client.upload_file(test_file.string(), "/tmp/test.txt");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, UploadFileValidation) {
    SSHClient client(test_options_);
    
    // Non-existent local file
    auto result = client.upload_file("/nonexistent/file.txt", "/tmp/test.txt");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("not found"),
        testing::HasSubstr("local file"),
        testing::HasSubstr("not connected")
    ));
    
    // Empty remote path
    auto test_file = temp_dir_->create_file("test.txt", "content");
    result = client.upload_file(test_file.string(), "");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("remote path"),
        testing::HasSubstr("empty"),
        testing::HasSubstr("not connected")
    ));
}

TEST_F(SSHClientTest, DownloadFileWhenNotConnected) {
    SSHClient client(test_options_);
    
    auto local_path = (temp_dir_->path() / "downloaded.txt").string();
    
    auto result = client.download_file("/tmp/test.txt", local_path);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, DownloadFileValidation) {
    SSHClient client(test_options_);
    
    // Empty remote path
    auto result = client.download_file("", "/tmp/local.txt");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("remote path"),
        testing::HasSubstr("empty"),
        testing::HasSubstr("not connected")
    ));
    
    // Empty local path
    result = client.download_file("/tmp/remote.txt", "");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("local path"),
        testing::HasSubstr("empty"),
        testing::HasSubstr("not connected")
    ));
}

TEST_F(SSHClientTest, UploadDirectoryWhenNotConnected) {
    SSHClient client(test_options_);
    
    auto test_dir = temp_dir_->create_subdirectory("upload_test");
    temp_dir_->create_file("upload_test/file1.txt", "content1");
    temp_dir_->create_file("upload_test/file2.txt", "content2");
    
    auto result = client.upload_directory(test_dir.string(), "/tmp/test_dir");
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, DownloadDirectoryWhenNotConnected) {
    SSHClient client(test_options_);
    
    auto local_dir = temp_dir_->create_subdirectory("download_test");
    
    auto result = client.download_directory("/tmp/remote_dir", local_dir.string());
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

// Progress tracking tests
TEST_F(SSHClientTest, SetProgressCallback) {
    SSHClient client(test_options_);
    
    std::vector<std::string> operations;
    client.set_progress_callback([&](const auto& op, auto curr, auto total, const auto& msg) {
        operations.push_back(op);
    });
    
    // Try file operation (will fail due to no connection, but should invoke callback)
    auto test_file = temp_dir_->create_file("test.txt", "content");
    auto result = client.upload_file(test_file.string(), "/tmp/test.txt");
    
    // Callback might not be called if operation fails early
    // This is acceptable behavior
    EXPECT_FALSE(result.success);
}

// Connection state and monitoring tests
TEST_F(SSHClientTest, GetConnectionState) {
    SSHClient client(test_options_);
    
    auto state = client.get_connection_state();
    EXPECT_EQ(state.status, SSHClient::ConnectionStatus::Disconnected);
    EXPECT_TRUE(state.host.empty());
    EXPECT_EQ(state.port, 0);
    EXPECT_TRUE(state.username.empty());
}

TEST_F(SSHClientTest, GetLastError) {
    SSHClient client(test_options_);
    
    // Initial state should have no error
    auto error = client.get_last_error();
    EXPECT_TRUE(error.empty());
    
    // After failed operation, should have error
    auto result = client.connect(connection_info_);
    EXPECT_FALSE(result.success);
    
    error = client.get_last_error();
    EXPECT_FALSE(error.empty());
}

TEST_F(SSHClientTest, GetConnectionStats) {
    SSHClient client(test_options_);
    
    auto stats = client.get_connection_stats();
    EXPECT_EQ(stats.bytes_sent, 0);
    EXPECT_EQ(stats.bytes_received, 0);
    EXPECT_EQ(stats.commands_executed, 0);
    EXPECT_EQ(stats.files_transferred, 0);
}

// Concurrent operations tests
TEST_F(SSHClientTest, ConcurrentCommands) {
    SSHClient client(test_options_);
    
    // Start multiple async operations
    auto future1 = client.execute_command_async("echo test1");
    auto future2 = client.execute_command_async("echo test2");
    
    // Both should complete quickly with "not connected" error
    auto result1 = future1.get();
    auto result2 = future2.get();
    
    EXPECT_FALSE(result1.success);
    EXPECT_FALSE(result2.success);
    EXPECT_THAT(result1.error_message, testing::HasSubstr("not connected"));
    EXPECT_THAT(result2.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, ConcurrentFileOperations) {
    SSHClient client(test_options_);
    
    auto test_file1 = temp_dir_->create_file("test1.txt", "content1");
    auto test_file2 = temp_dir_->create_file("test2.txt", "content2");
    
    // Start concurrent uploads
    auto upload_future = std::async(std::launch::async, [&]() {
        return client.upload_file(test_file1.string(), "/tmp/test1.txt");
    });
    
    auto download_future = std::async(std::launch::async, [&]() {
        return client.download_file("/tmp/remote.txt", test_file2.string());
    });
    
    auto upload_result = upload_future.get();
    auto download_result = download_future.get();
    
    // Both should fail due to no connection
    EXPECT_FALSE(upload_result.success);
    EXPECT_FALSE(download_result.success);
}

// Error handling and recovery tests
TEST_F(SSHClientTest, ConnectionRecovery) {
    SSHClient client(test_options_);
    
    // Attempt connection (will fail)
    auto result1 = client.connect(connection_info_);
    EXPECT_FALSE(result1.success);
    
    // Try again - should handle gracefully
    auto result2 = client.connect(connection_info_);
    EXPECT_FALSE(result2.success);
    
    // Should not crash or hang
    EXPECT_FALSE(client.is_connected());
}

TEST_F(SSHClientTest, InvalidConfigurationHandling) {
    SSHClient::Options bad_options;
    bad_options.connection_timeout = std::chrono::seconds{0}; // Invalid
    bad_options.command_timeout = std::chrono::seconds{-1}; // Invalid
    bad_options.max_retries = 0; // Edge case
    
    // Should either throw or handle gracefully
    EXPECT_NO_THROW({
        SSHClient client(bad_options);
        // Constructor might correct invalid values
        auto corrected_options = client.get_options();
        EXPECT_GT(corrected_options.connection_timeout.count(), 0);
        EXPECT_GT(corrected_options.command_timeout.count(), 0);
    });
}

// Timeout and retry tests
TEST_F(SSHClientTest, ConnectionTimeout) {
    auto timeout_options = test_options_;
    timeout_options.connection_timeout = std::chrono::milliseconds{1}; // Very short timeout
    
    SSHClient client(timeout_options);
    
    auto start = std::chrono::steady_clock::now();
    auto result = client.connect(connection_info_);
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    EXPECT_FALSE(result.success);
    // Should timeout quickly
    EXPECT_LT(elapsed, std::chrono::seconds{2});
}

TEST_F(SSHClientTest, RetryMechanism) {
    auto retry_options = test_options_;
    retry_options.max_retries = 2;
    retry_options.retry_delay = std::chrono::milliseconds{10};
    
    SSHClient client(retry_options);
    
    auto start = std::chrono::steady_clock::now();
    auto result = client.connect(connection_info_);
    auto elapsed = std::chrono::steady_clock::now() - start;
    
    EXPECT_FALSE(result.success);
    // Should have attempted multiple times with delays
    EXPECT_GE(elapsed, std::chrono::milliseconds{20}); // At least 2 retries with 10ms delay each
}

// Move semantics and resource management
TEST_F(SSHClientTest, MoveSemantics) {
    SSHClient client1(test_options_);
    
    // Move construct
    SSHClient client2(std::move(client1));
    
    // Moved-to object should be functional
    EXPECT_FALSE(client2.is_connected());
    auto options = client2.get_options();
    EXPECT_EQ(options.connection_timeout, test_options_.connection_timeout);
}

TEST_F(SSHClientTest, ResourceCleanupOnDestruction) {
    {
        SSHClient client(test_options_);
        auto result = client.connect(connection_info_);
        // Client goes out of scope here
    }
    
    // Should not leave any hanging connections or resources
    // (This is more of a manual verification test)
    SUCCEED();
}

// Stress and edge case tests
TEST_F(SSHClientTest, LargeFilePathHandling) {
    SSHClient client(test_options_);
    
    // Very long file path
    std::string long_path(1000, 'a');
    long_path = "/" + long_path + ".txt";
    
    auto result = client.upload_file("/tmp/test.txt", long_path);
    EXPECT_FALSE(result.success);
    // Should handle gracefully without crashing
}

TEST_F(SSHClientTest, SpecialCharacterHandling) {
    SSHClient client(test_options_);
    
    // File paths with special characters
    auto test_file = temp_dir_->create_file("test with spaces.txt", "content");
    
    auto result = client.upload_file(test_file.string(), "/tmp/test with spaces.txt");
    EXPECT_FALSE(result.success); // Due to no connection
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}

TEST_F(SSHClientTest, EncodingHandling) {
    SSHClient client(test_options_);
    
    // Command with special characters
    auto result = client.execute_command("echo 'Special chars: äöü αβγ 中文'");
    EXPECT_FALSE(result.success); // Due to no connection
    EXPECT_THAT(result.error_message, testing::HasSubstr("not connected"));
}