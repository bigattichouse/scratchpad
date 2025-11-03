#include <gtest/gtest.h>
#include "domain/communication/ssh_connection.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>

using namespace scratchpad;
using namespace scratchpad::test;

class SSHConnectionTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::make_unique<TempDirectory>();
        
        // Create test SSH keys
        auto [private_key, public_key] = TestHelpers::create_temp_ssh_keys(temp_dir_->path());
        
        // Set up test credentials
        test_credentials_.host = "127.0.0.1";
        test_credentials_.port = TestHelpers::find_available_port(2222, 3222);
        test_credentials_.username = "testuser";
        test_credentials_.private_key_path = private_key;
        test_credentials_.connection_timeout = std::chrono::seconds{30};
        test_credentials_.command_timeout = std::chrono::seconds{60};
    }
    
    std::unique_ptr<TempDirectory> temp_dir_;
    SSHCredentials test_credentials_;
};

// Construction tests
TEST_F(SSHConnectionTest, ConstructionWithValidCredentials) {
    EXPECT_NO_THROW({
        SSHConnection connection(test_credentials_);
        EXPECT_EQ(connection.credentials().host, test_credentials_.host);
        EXPECT_EQ(connection.credentials().port, test_credentials_.port);
        EXPECT_EQ(connection.credentials().username, test_credentials_.username);
        EXPECT_EQ(connection.status(), ConnectionStatus::Disconnected);
    });
}

TEST_F(SSHConnectionTest, ConstructionWithInvalidCredentials) {
    // Empty host
    SSHCredentials invalid_creds = test_credentials_;
    invalid_creds.host = "";
    EXPECT_THROW({
        SSHConnection connection(invalid_creds);
    }, ScratchpadError);
    
    // Invalid port
    invalid_creds = test_credentials_;
    invalid_creds.port = 0;
    EXPECT_THROW({
        SSHConnection connection(invalid_creds);
    }, ScratchpadError);
    
    // Empty username
    invalid_creds = test_credentials_;
    invalid_creds.username = "";
    EXPECT_THROW({
        SSHConnection connection(invalid_creds);
    }, ScratchpadError);
    
    // Non-existent private key
    invalid_creds = test_credentials_;
    invalid_creds.private_key_path = "/nonexistent/key";
    EXPECT_THROW({
        SSHConnection connection(invalid_creds);
    }, ScratchpadError);
}

// Basic property tests
TEST_F(SSHConnectionTest, BasicProperties) {
    SSHConnection connection(test_credentials_);
    
    EXPECT_EQ(connection.credentials().host, test_credentials_.host);
    EXPECT_EQ(connection.credentials().port, test_credentials_.port);
    EXPECT_EQ(connection.credentials().username, test_credentials_.username);
    EXPECT_GT(connection.creation_time(), std::chrono::system_clock::time_point{});
    EXPECT_GT(connection.last_status_change(), std::chrono::system_clock::time_point{});
}

// Status management tests
TEST_F(SSHConnectionTest, InitialStatus) {
    SSHConnection connection(test_credentials_);
    
    EXPECT_EQ(connection.status(), ConnectionStatus::Disconnected);
    EXPECT_FALSE(connection.is_connected());
    EXPECT_FALSE(connection.is_connecting());
    EXPECT_FALSE(connection.has_failed());
}

TEST_F(SSHConnectionTest, StatusTransitions) {
    SSHConnection connection(test_credentials_);
    
    auto before_change = std::chrono::system_clock::now();
    
    // Disconnected -> Connecting
    connection.set_status(ConnectionStatus::Connecting);
    EXPECT_EQ(connection.status(), ConnectionStatus::Connecting);
    EXPECT_FALSE(connection.is_connected());
    EXPECT_TRUE(connection.is_connecting());
    EXPECT_FALSE(connection.has_failed());
    EXPECT_GE(connection.last_status_change(), before_change);
    
    // Connecting -> Connected
    connection.set_status(ConnectionStatus::Connected);
    EXPECT_EQ(connection.status(), ConnectionStatus::Connected);
    EXPECT_TRUE(connection.is_connected());
    EXPECT_FALSE(connection.is_connecting());
    EXPECT_FALSE(connection.has_failed());
    
    // Connected -> Disconnected
    connection.set_status(ConnectionStatus::Disconnected);
    EXPECT_EQ(connection.status(), ConnectionStatus::Disconnected);
    EXPECT_FALSE(connection.is_connected());
    
    // Test failure state
    connection.set_status(ConnectionStatus::Connecting);
    connection.set_status(ConnectionStatus::Failed);
    EXPECT_EQ(connection.status(), ConnectionStatus::Failed);
    EXPECT_FALSE(connection.is_connected());
    EXPECT_TRUE(connection.has_failed());
}

TEST_F(SSHConnectionTest, StatusHistory) {
    SSHConnection connection(test_credentials_);
    
    // Perform several status changes
    connection.set_status(ConnectionStatus::Connecting);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    connection.set_status(ConnectionStatus::Connected);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    connection.set_status(ConnectionStatus::Disconnected);
    
    auto history = connection.status_history();
    EXPECT_GE(history.size(), 3);
    
    // Check that history is ordered chronologically
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
    }
    
    // Check specific transitions
    auto it = std::find_if(history.begin(), history.end(), [](const auto& change) {
        return change.from_status == ConnectionStatus::Disconnected && 
               change.to_status == ConnectionStatus::Connecting;
    });
    EXPECT_NE(it, history.end());
}

// Connection metrics tests
TEST_F(SSHConnectionTest, ConnectionMetrics) {
    SSHConnection connection(test_credentials_);
    
    // Initially no connection time
    EXPECT_EQ(connection.connection_time(), std::chrono::seconds{0});
    EXPECT_EQ(connection.total_connected_time(), std::chrono::seconds{0});
    
    // Simulate connection
    auto start_time = std::chrono::system_clock::now();
    connection.set_status(ConnectionStatus::Connecting);
    connection.set_status(ConnectionStatus::Connected);
    
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    
    auto connection_time = connection.connection_time();
    EXPECT_GT(connection_time, std::chrono::seconds{0});
    EXPECT_LT(connection_time, std::chrono::seconds{10});
    
    // Disconnect and check total time
    connection.set_status(ConnectionStatus::Disconnected);
    auto total_time = connection.total_connected_time();
    EXPECT_GE(total_time, connection_time);
    
    // Reconnect and check cumulative time
    connection.set_status(ConnectionStatus::Connecting);
    connection.set_status(ConnectionStatus::Connected);
    std::this_thread::sleep_for(std::chrono::milliseconds{50});
    connection.set_status(ConnectionStatus::Disconnected);
    
    auto final_total = connection.total_connected_time();
    EXPECT_GT(final_total, total_time);
}

// Error tracking tests
TEST_F(SSHConnectionTest, ErrorTracking) {
    SSHConnection connection(test_credentials_);
    
    // Initially no errors
    EXPECT_TRUE(connection.errors().empty());
    EXPECT_TRUE(connection.last_error().empty());
    EXPECT_EQ(connection.error_count(), 0);
    
    // Add errors
    connection.add_error("Connection timeout");
    connection.add_error("Authentication failed");
    connection.add_error("Host unreachable");
    
    EXPECT_EQ(connection.error_count(), 3);
    EXPECT_EQ(connection.last_error(), "Host unreachable");
    
    auto errors = connection.errors();
    EXPECT_EQ(errors.size(), 3);
    EXPECT_EQ(errors[0].message, "Connection timeout");
    EXPECT_EQ(errors[2].message, "Host unreachable");
    
    // Check timestamps are ordered
    for (size_t i = 1; i < errors.size(); ++i) {
        EXPECT_GE(errors[i].timestamp, errors[i-1].timestamp);
    }
    
    // Clear errors
    connection.clear_errors();
    EXPECT_TRUE(connection.errors().empty());
    EXPECT_EQ(connection.error_count(), 0);
}

// Authentication tests
TEST_F(SSHConnectionTest, AuthenticationMethods) {
    SSHConnection connection(test_credentials_);
    
    // Test key-based authentication (default)
    EXPECT_EQ(connection.authentication_method(), AuthenticationMethod::PublicKey);
    EXPECT_FALSE(connection.credentials().private_key_path.empty());
    
    // Test password authentication
    SSHCredentials password_creds = test_credentials_;
    password_creds.password = "testpassword";
    password_creds.private_key_path = ""; // Clear key path
    
    SSHConnection password_connection(password_creds);
    EXPECT_EQ(password_connection.authentication_method(), AuthenticationMethod::Password);
    EXPECT_FALSE(password_connection.credentials().password.empty());
}

TEST_F(SSHConnectionTest, KeyValidation) {
    SSHConnection connection(test_credentials_);
    
    // Test key validation
    EXPECT_TRUE(connection.validate_private_key());
    
    // Test with invalid key path
    SSHCredentials invalid_key_creds = test_credentials_;
    invalid_key_creds.private_key_path = "/nonexistent/key";
    
    EXPECT_THROW({
        SSHConnection invalid_connection(invalid_key_creds);
    }, ScratchpadError);
}

// Connection attempt tracking
TEST_F(SSHConnectionTest, ConnectionAttempts) {
    SSHConnection connection(test_credentials_);
    
    // Initially no attempts
    EXPECT_EQ(connection.connection_attempts(), 0);
    EXPECT_EQ(connection.successful_connections(), 0);
    EXPECT_EQ(connection.failed_connections(), 0);
    
    // Simulate connection attempts
    connection.set_status(ConnectionStatus::Connecting);
    connection.set_status(ConnectionStatus::Failed);
    
    EXPECT_EQ(connection.connection_attempts(), 1);
    EXPECT_EQ(connection.failed_connections(), 1);
    EXPECT_EQ(connection.successful_connections(), 0);
    
    // Successful connection
    connection.set_status(ConnectionStatus::Connecting);
    connection.set_status(ConnectionStatus::Connected);
    
    EXPECT_EQ(connection.connection_attempts(), 2);
    EXPECT_EQ(connection.successful_connections(), 1);
    EXPECT_EQ(connection.failed_connections(), 1);
}

// Timeout handling tests
TEST_F(SSHConnectionTest, TimeoutHandling) {
    SSHConnection connection(test_credentials_);
    
    // Test connection timeout
    EXPECT_EQ(connection.credentials().connection_timeout, std::chrono::seconds{30});
    EXPECT_EQ(connection.credentials().command_timeout, std::chrono::seconds{60});
    
    // Test timeout detection
    connection.set_status(ConnectionStatus::Connecting);
    
    // Simulate timeout by waiting beyond connection timeout
    // (In real implementation, this would be handled by the SSH client)
    auto start_time = connection.last_status_change();
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    
    // Check if connection has been connecting too long
    auto elapsed = std::chrono::system_clock::now() - start_time;
    EXPECT_LT(elapsed, test_credentials_.connection_timeout); // Should not have timed out yet
}

// Keep-alive and heartbeat tests
TEST_F(SSHConnectionTest, KeepAliveSettings) {
    SSHConnection connection(test_credentials_);
    
    // Test keep-alive settings
    EXPECT_TRUE(connection.credentials().enable_keepalive);
    EXPECT_GT(connection.credentials().keepalive_interval, std::chrono::seconds{0});
    
    // Test heartbeat functionality
    connection.set_status(ConnectionStatus::Connected);
    connection.send_heartbeat();
    
    auto last_heartbeat = connection.last_heartbeat();
    EXPECT_GT(last_heartbeat, std::chrono::system_clock::time_point{});
    
    // Check if heartbeat is recent
    auto heartbeat_age = std::chrono::system_clock::now() - last_heartbeat;
    EXPECT_LT(heartbeat_age, std::chrono::seconds{5});
}

// Connection quality metrics
TEST_F(SSHConnectionTest, ConnectionQuality) {
    SSHConnection connection(test_credentials_);
    
    // Initially no quality metrics
    auto quality = connection.connection_quality();
    EXPECT_DOUBLE_EQ(quality.latency_ms, 0.0);
    EXPECT_DOUBLE_EQ(quality.packet_loss_percent, 0.0);
    EXPECT_EQ(quality.bytes_sent, 0);
    EXPECT_EQ(quality.bytes_received, 0);
    
    // Set connection quality metrics
    ConnectionQuality new_quality;
    new_quality.latency_ms = 25.5;
    new_quality.packet_loss_percent = 0.1;
    new_quality.bytes_sent = 1024;
    new_quality.bytes_received = 2048;
    
    connection.set_connection_quality(new_quality);
    
    auto retrieved_quality = connection.connection_quality();
    EXPECT_DOUBLE_EQ(retrieved_quality.latency_ms, 25.5);
    EXPECT_DOUBLE_EQ(retrieved_quality.packet_loss_percent, 0.1);
    EXPECT_EQ(retrieved_quality.bytes_sent, 1024);
    EXPECT_EQ(retrieved_quality.bytes_received, 2048);
}

// Copy and move semantics tests
TEST_F(SSHConnectionTest, CopySemantics) {
    SSHConnection original(test_credentials_);
    original.set_status(ConnectionStatus::Connected);
    original.add_error("Test error");
    
    SSHConnection copy(original);
    
    EXPECT_EQ(copy.credentials().host, original.credentials().host);
    EXPECT_EQ(copy.status(), original.status());
    EXPECT_EQ(copy.error_count(), original.error_count());
    
    // Changes to copy shouldn't affect original
    copy.set_status(ConnectionStatus::Disconnected);
    EXPECT_EQ(original.status(), ConnectionStatus::Connected);
}

TEST_F(SSHConnectionTest, MoveSemantics) {
    SSHConnection original(test_credentials_);
    original.set_status(ConnectionStatus::Connected);
    std::string original_host = original.credentials().host;
    
    SSHConnection moved(std::move(original));
    
    EXPECT_EQ(moved.credentials().host, original_host);
    EXPECT_EQ(moved.status(), ConnectionStatus::Connected);
}

// Thread safety tests (basic checks)
TEST_F(SSHConnectionTest, ThreadSafetyBasics) {
    SSHConnection connection(test_credentials_);
    
    // Test concurrent status changes
    std::vector<std::thread> threads;
    std::atomic<int> status_changes{0};
    
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&connection, &status_changes]() {
            for (int j = 0; j < 10; ++j) {
                connection.set_status(ConnectionStatus::Connecting);
                connection.set_status(ConnectionStatus::Connected);
                connection.set_status(ConnectionStatus::Disconnected);
                status_changes++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(status_changes.load(), 50);
    // Connection should end up in a valid state
    EXPECT_NE(connection.status(), static_cast<ConnectionStatus>(-1));
}