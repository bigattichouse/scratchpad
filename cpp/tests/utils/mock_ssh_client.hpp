#pragma once

#include "infrastructure/ssh/ssh_client.hpp"
#include <gmock/gmock.h>

namespace scratchpad::test {

class MockSSHClient : public SSHClient {
public:
    MOCK_METHOD(std::future<bool>, connect_async, 
                (const std::string& host, int port, const std::string& username, 
                 const std::string& private_key_path), (override));
    
    MOCK_METHOD(void, disconnect, (), (override));
    
    MOCK_METHOD(bool, is_connected, (), (const, override));
    
    MOCK_METHOD(std::future<CommandResult>, execute_command_async, 
                (const std::string& command), (override));
    
    MOCK_METHOD(std::future<bool>, upload_file_async, 
                (const std::filesystem::path& local_path, 
                 const std::filesystem::path& remote_path), (override));
    
    MOCK_METHOD(std::future<bool>, download_file_async, 
                (const std::filesystem::path& remote_path, 
                 const std::filesystem::path& local_path), (override));
    
    MOCK_METHOD(bool, test_connection, (), (override));
    
    MOCK_METHOD(std::string, get_last_error, (), (const, override));
};

// Factory for creating mock SSH clients
class MockSSHClientFactory {
public:
    static std::unique_ptr<MockSSHClient> create_mock() {
        auto mock = std::make_unique<MockSSHClient>();
        
        // Set up default expectations
        ON_CALL(*mock, is_connected())
            .WillByDefault(testing::Return(false));
        
        ON_CALL(*mock, test_connection())
            .WillByDefault(testing::Return(true));
        
        ON_CALL(*mock, get_last_error())
            .WillByDefault(testing::Return(""));
        
        // Default successful command execution
        CommandResult default_result;
        default_result.exit_code = 0;
        default_result.stdout = "";
        default_result.stderr = "";
        default_result.success = true;
        
        auto future = std::async(std::launch::deferred, [default_result]() {
            return default_result;
        });
        
        ON_CALL(*mock, execute_command_async(testing::_))
            .WillByDefault(testing::Return(testing::ByMove(std::move(future))));
        
        return mock;
    }
    
    static std::unique_ptr<MockSSHClient> create_connected_mock() {
        auto mock = create_mock();
        
        ON_CALL(*mock, is_connected())
            .WillByDefault(testing::Return(true));
        
        // Return successful connection future
        auto connect_future = std::async(std::launch::deferred, []() {
            return true;
        });
        
        ON_CALL(*mock, connect_async(testing::_, testing::_, testing::_, testing::_))
            .WillByDefault(testing::Return(testing::ByMove(std::move(connect_future))));
        
        return mock;
    }
};

} // namespace scratchpad::test