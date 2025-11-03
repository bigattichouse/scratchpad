#pragma once

#include "scratchpad/infrastructure/ssh/ssh_client.hpp"
#include <gmock/gmock.h>

namespace scratchpad::test {

class MockSSHClient : public SSHClient {
public:
    MockSSHClient() : SSHClient() {}
    
    MOCK_METHOD(ConnectionResult, connect, 
                (const ConnectionInfo& info), (override));
    
    MOCK_METHOD(void, disconnect, (), (override));
    
    MOCK_METHOD(bool, is_connected, (), (const, override));
    
    MOCK_METHOD(CommandResult, execute_command, 
                (const std::string& command), (override));
                
    MOCK_METHOD(CommandResult, execute_command, 
                (const std::string& command, std::chrono::milliseconds timeout), (override));
    
    MOCK_METHOD(std::future<CommandResult>, execute_command_async, 
                (const std::string& command), (override));
    
    MOCK_METHOD(FileTransferResult, upload_file, 
                (const std::string& local_path, const std::string& remote_path), (override));
    
    MOCK_METHOD(FileTransferResult, download_file, 
                (const std::string& remote_path, const std::string& local_path), (override));
    
    MOCK_METHOD(std::string, get_last_error, (), (const, override));
    
    MOCK_METHOD(ConnectionState, get_connection_state, (), (const, override));
};

// Factory for creating mock SSH clients
class MockSSHClientFactory {
public:
    static std::unique_ptr<MockSSHClient> create_mock() {
        auto mock = std::make_unique<MockSSHClient>();
        
        // Set up default expectations
        ON_CALL(*mock, is_connected())
            .WillByDefault(testing::Return(false));
        
        ON_CALL(*mock, get_last_error())
            .WillByDefault(testing::Return(""));
        
        // Default connection state
        SSHClient::ConnectionState default_state;
        default_state.status = SSHClient::ConnectionStatus::Disconnected;
        default_state.host = "";
        default_state.port = 0;
        default_state.username = "";
        
        ON_CALL(*mock, get_connection_state())
            .WillByDefault(testing::Return(default_state));
        
        return mock;
    }
    
    static std::unique_ptr<MockSSHClient> create_connected_mock() {
        auto mock = create_mock();
        
        ON_CALL(*mock, is_connected())
            .WillByDefault(testing::Return(true));
        
        SSHClient::ConnectionResult success_result;
        success_result.success = true;
        success_result.status = SSHClient::ConnectionStatus::Connected;
        
        ON_CALL(*mock, connect(testing::_))
            .WillByDefault(testing::Return(success_result));
        
        return mock;
    }
};

} // namespace scratchpad::test