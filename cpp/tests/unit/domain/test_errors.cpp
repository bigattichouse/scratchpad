#include <gtest/gtest.h>
#include "scratchpad/errors.hpp"
#include "utils/test_helpers.hpp"

using namespace scratchpad;
using namespace scratchpad::test;

class ErrorTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_vm_id_ = VMId("test-vm-123");
        test_process_id_ = 12345;
    }
    
    VMId test_vm_id_;
    ProcessId test_process_id_;
};

// ScratchpadError tests
TEST_F(ErrorTest, ScratchpadErrorBasic) {
    ErrorCode code = ErrorCode::InvalidConfiguration;
    std::string message = "Test error message";
    
    ScratchpadError error(code, message);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "scratchpad");
}

TEST_F(ErrorTest, ScratchpadErrorInheritance) {
    ScratchpadError error(ErrorCode::InternalError, "Base error");
    
    // Should be catchable as std::exception
    try {
        throw error;
    } catch (const std::exception& e) {
        EXPECT_STREQ(e.what(), "Base error");
    } catch (...) {
        FAIL() << "Error should be catchable as std::exception";
    }
}

// VMError tests
TEST_F(ErrorTest, VMErrorBasic) {
    ErrorCode code = ErrorCode::VMNotFound;
    std::string message = "VM not found";
    
    VMError error(code, message, test_vm_id_);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "vm");
    EXPECT_EQ(error.vm_id(), test_vm_id_);
}

TEST_F(ErrorTest, VMErrorWithoutVMId) {
    VMError error(ErrorCode::InvalidVMConfiguration, "Invalid config");
    
    EXPECT_EQ(error.code(), ErrorCode::InvalidVMConfiguration);
    EXPECT_TRUE(error.vm_id().empty());
}

TEST_F(ErrorTest, VMErrorInheritance) {
    VMError vm_error(ErrorCode::VMStartupFailed, "Startup failed", test_vm_id_);
    
    // Should be catchable as ScratchpadError
    try {
        throw vm_error;
    } catch (const ScratchpadError& e) {
        EXPECT_EQ(e.code(), ErrorCode::VMStartupFailed);
        EXPECT_EQ(e.category(), "vm");
    } catch (...) {
        FAIL() << "VMError should be catchable as ScratchpadError";
    }
}

// ProcessError tests
TEST_F(ErrorTest, ProcessErrorBasic) {
    ErrorCode code = ErrorCode::ProcessNotFound;
    std::string message = "Process not found";
    
    ProcessError error(code, message, test_process_id_);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "process");
    EXPECT_EQ(error.process_id(), test_process_id_);
}

TEST_F(ErrorTest, ProcessErrorWithoutPid) {
    ProcessError error(ErrorCode::ProcessStartupFailed, "Startup failed");
    
    EXPECT_EQ(error.code(), ErrorCode::ProcessStartupFailed);
    EXPECT_EQ(error.process_id(), 0);
}

// SSHError tests
TEST_F(ErrorTest, SSHErrorBasic) {
    ErrorCode code = ErrorCode::SSHConnectionFailed;
    std::string message = "Connection failed";
    std::string host = "192.168.1.100";
    int port = 2222;
    
    SSHError error(code, message, host, port);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "ssh");
    EXPECT_EQ(error.host(), host);
    EXPECT_EQ(error.port(), port);
}

TEST_F(ErrorTest, SSHErrorWithoutHostPort) {
    SSHError error(ErrorCode::SSHAuthenticationFailed, "Auth failed");
    
    EXPECT_EQ(error.code(), ErrorCode::SSHAuthenticationFailed);
    EXPECT_TRUE(error.host().empty());
    EXPECT_EQ(error.port(), 0);
}

// ImageError tests
TEST_F(ErrorTest, ImageErrorBasic) {
    ErrorCode code = ErrorCode::ImageNotFound;
    std::string message = "Image not found";
    std::string image_name = "ubuntu-22.04";
    
    ImageError error(code, message, image_name);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "image");
    EXPECT_EQ(error.image_name(), image_name);
}

// ConfigurationError tests
TEST_F(ErrorTest, ConfigurationErrorBasic) {
    ErrorCode code = ErrorCode::InvalidConfiguration;
    std::string message = "Invalid config value";
    std::string config_key = "ssh_port_range_start";
    
    ConfigurationError error(code, message, config_key);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "configuration");
    EXPECT_EQ(error.config_key(), config_key);
}

// ResourceError tests
TEST_F(ErrorTest, ResourceErrorBasic) {
    ErrorCode code = ErrorCode::InsufficientResources;
    std::string message = "Not enough memory";
    ResourceType resource_type = ResourceType::Memory;
    
    ResourceError error(code, message, resource_type);
    
    EXPECT_EQ(error.code(), code);
    EXPECT_EQ(error.what(), message);
    EXPECT_EQ(error.category(), "resource");
    EXPECT_EQ(error.resource_type(), resource_type);
}

// Error code category tests
TEST_F(ErrorTest, ErrorCodeCategories) {
    EXPECT_EQ(get_error_category(ErrorCode::Success), "success");
    EXPECT_EQ(get_error_category(ErrorCode::InvalidConfiguration), "configuration");
    EXPECT_EQ(get_error_category(ErrorCode::VMNotFound), "vm");
    EXPECT_EQ(get_error_category(ErrorCode::ProcessNotFound), "process");
    EXPECT_EQ(get_error_category(ErrorCode::SSHConnectionFailed), "ssh");
    EXPECT_EQ(get_error_category(ErrorCode::ImageNotFound), "image");
    EXPECT_EQ(get_error_category(ErrorCode::InsufficientResources), "resource");
    EXPECT_EQ(get_error_category(ErrorCode::InternalError), "internal");
}

// Error code descriptions
TEST_F(ErrorTest, ErrorCodeDescriptions) {
    EXPECT_FALSE(get_error_description(ErrorCode::Success).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::InvalidConfiguration).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::VMNotFound).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::ProcessNotFound).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::SSHConnectionFailed).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::ImageNotFound).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::InsufficientResources).empty());
    EXPECT_FALSE(get_error_description(ErrorCode::InternalError).empty());
    
    // Check specific descriptions
    EXPECT_EQ(get_error_description(ErrorCode::Success), "Operation completed successfully");
    EXPECT_THAT(get_error_description(ErrorCode::VMNotFound), 
                testing::HasSubstr("Virtual machine not found"));
    EXPECT_THAT(get_error_description(ErrorCode::SSHConnectionFailed), 
                testing::HasSubstr("SSH connection failed"));
}

// Error severity tests
TEST_F(ErrorTest, ErrorSeverities) {
    EXPECT_EQ(get_error_severity(ErrorCode::Success), ErrorSeverity::Info);
    EXPECT_EQ(get_error_severity(ErrorCode::InvalidConfiguration), ErrorSeverity::Error);
    EXPECT_EQ(get_error_severity(ErrorCode::VMNotFound), ErrorSeverity::Error);
    EXPECT_EQ(get_error_severity(ErrorCode::ProcessNotFound), ErrorSeverity::Error);
    EXPECT_EQ(get_error_severity(ErrorCode::SSHConnectionFailed), ErrorSeverity::Error);
    EXPECT_EQ(get_error_severity(ErrorCode::InsufficientResources), ErrorSeverity::Warning);
    EXPECT_EQ(get_error_severity(ErrorCode::InternalError), ErrorSeverity::Critical);
}

// Exception chaining tests
TEST_F(ErrorTest, ExceptionChaining) {
    try {
        try {
            throw ProcessError(ErrorCode::ProcessStartupFailed, "QEMU failed to start", test_process_id_);
        } catch (const ProcessError& process_error) {
            // Chain the exception
            throw VMError(ErrorCode::VMStartupFailed, 
                         "VM startup failed due to process error: " + std::string(process_error.what()),
                         test_vm_id_);
        }
    } catch (const VMError& vm_error) {
        EXPECT_EQ(vm_error.vm_id(), test_vm_id_);
        EXPECT_THAT(vm_error.what(), testing::HasSubstr("process error"));
        EXPECT_THAT(vm_error.what(), testing::HasSubstr("QEMU failed to start"));
    } catch (...) {
        FAIL() << "Should have caught VMError";
    }
}

// Error factory tests
TEST_F(ErrorTest, ErrorFactory) {
    // Test VM error factory
    auto vm_error = make_vm_error(ErrorCode::VMNotFound, "VM {} not found", test_vm_id_);
    EXPECT_EQ(vm_error.vm_id(), test_vm_id_);
    EXPECT_THAT(vm_error.what(), testing::HasSubstr(test_vm_id_.value()));
    
    // Test process error factory
    auto process_error = make_process_error(ErrorCode::ProcessNotFound, 
                                          "Process {} not found", test_process_id_);
    EXPECT_EQ(process_error.process_id(), test_process_id_);
    EXPECT_THAT(process_error.what(), testing::HasSubstr(std::to_string(test_process_id_)));
    
    // Test SSH error factory
    auto ssh_error = make_ssh_error(ErrorCode::SSHConnectionFailed, 
                                   "Failed to connect to {}:{}", "localhost", 2222);
    EXPECT_EQ(ssh_error.host(), "localhost");
    EXPECT_EQ(ssh_error.port(), 2222);
    EXPECT_THAT(ssh_error.what(), testing::HasSubstr("localhost:2222"));
}

// Error context tests
TEST_F(ErrorTest, ErrorContext) {
    ErrorContext context;
    context.add_context("operation", "vm_start");
    context.add_context("vm_id", test_vm_id_.value());
    context.add_context("attempt", "2");
    
    VMError error(ErrorCode::VMStartupFailed, "Startup failed", test_vm_id_);
    error.set_context(context);
    
    auto retrieved_context = error.context();
    EXPECT_EQ(retrieved_context.get("operation"), "vm_start");
    EXPECT_EQ(retrieved_context.get("vm_id"), test_vm_id_.value());
    EXPECT_EQ(retrieved_context.get("attempt"), "2");
}

// Error logging integration tests
TEST_F(ErrorTest, ErrorLogging) {
    // Test that errors can be logged properly
    VMError error(ErrorCode::VMStartupFailed, "VM failed to start", test_vm_id_);
    
    // Test structured logging format
    auto log_entry = error.to_log_entry();
    EXPECT_EQ(log_entry.level, LogLevel::Error);
    EXPECT_EQ(log_entry.category, "vm");
    EXPECT_EQ(log_entry.error_code, ErrorCode::VMStartupFailed);
    EXPECT_EQ(log_entry.vm_id, test_vm_id_);
    EXPECT_FALSE(log_entry.message.empty());
}

// Error recovery suggestions tests
TEST_F(ErrorTest, ErrorRecoverySuggestions) {
    // Test that errors provide helpful recovery suggestions
    VMError vm_not_found(ErrorCode::VMNotFound, "VM not found", test_vm_id_);
    auto suggestions = vm_not_found.recovery_suggestions();
    EXPECT_FALSE(suggestions.empty());
    EXPECT_THAT(suggestions[0], testing::HasSubstr("create"));
    
    SSHError ssh_conn_failed(ErrorCode::SSHConnectionFailed, "Connection failed", "localhost", 2222);
    auto ssh_suggestions = ssh_conn_failed.recovery_suggestions();
    EXPECT_FALSE(ssh_suggestions.empty());
    EXPECT_THAT(ssh_suggestions[0], testing::AnyOf(
        testing::HasSubstr("check"),
        testing::HasSubstr("verify"),
        testing::HasSubstr("ensure")
    ));
    
    ProcessError process_not_found(ErrorCode::ProcessNotFound, "Process not found", test_process_id_);
    auto process_suggestions = process_not_found.recovery_suggestions();
    EXPECT_FALSE(process_suggestions.empty());
}

// Error formatting tests
TEST_F(ErrorTest, ErrorFormatting) {
    VMError error(ErrorCode::VMStartupFailed, "Failed to start VM", test_vm_id_);
    
    // Test different formatting options
    auto brief_format = error.format(ErrorFormat::Brief);
    auto detailed_format = error.format(ErrorFormat::Detailed);
    auto json_format = error.format(ErrorFormat::JSON);
    
    EXPECT_FALSE(brief_format.empty());
    EXPECT_FALSE(detailed_format.empty());
    EXPECT_FALSE(json_format.empty());
    
    // Brief should be shorter than detailed
    EXPECT_LT(brief_format.length(), detailed_format.length());
    
    // JSON should contain structured data
    EXPECT_THAT(json_format, testing::HasSubstr("{"));
    EXPECT_THAT(json_format, testing::HasSubstr("error_code"));
    EXPECT_THAT(json_format, testing::HasSubstr("vm_id"));
}

// Error statistics and tracking
TEST_F(ErrorTest, ErrorStatistics) {
    ErrorTracker tracker;
    
    // Record various errors
    tracker.record_error(make_vm_error(ErrorCode::VMNotFound, "VM not found", test_vm_id_));
    tracker.record_error(make_vm_error(ErrorCode::VMStartupFailed, "Startup failed", test_vm_id_));
    tracker.record_error(make_ssh_error(ErrorCode::SSHConnectionFailed, "Connection failed", "host", 22));
    tracker.record_error(make_vm_error(ErrorCode::VMNotFound, "Another VM not found", VMId("other-vm")));
    
    // Check statistics
    auto stats = tracker.get_statistics();
    EXPECT_EQ(stats.total_errors, 4);
    EXPECT_EQ(stats.errors_by_category["vm"], 3);
    EXPECT_EQ(stats.errors_by_category["ssh"], 1);
    EXPECT_EQ(stats.errors_by_code[ErrorCode::VMNotFound], 2);
    EXPECT_EQ(stats.errors_by_code[ErrorCode::VMStartupFailed], 1);
    EXPECT_EQ(stats.errors_by_code[ErrorCode::SSHConnectionFailed], 1);
    
    // Check most common errors
    auto most_common = tracker.get_most_common_errors(2);
    EXPECT_EQ(most_common.size(), 2);
    EXPECT_EQ(most_common[0].first, ErrorCode::VMNotFound);
    EXPECT_EQ(most_common[0].second, 2);
}

// Custom error types
TEST_F(ErrorTest, CustomErrorTypes) {
    // Test that custom error types can be created
    class CustomError : public ScratchpadError {
    public:
        CustomError(ErrorCode code, std::string_view message, std::string custom_field)
            : ScratchpadError(code, message), custom_field_(std::move(custom_field)) {}
        
        const std::string& custom_field() const { return custom_field_; }
        std::string_view category() const noexcept override { return "custom"; }
        
    private:
        std::string custom_field_;
    };
    
    CustomError custom_error(ErrorCode::InternalError, "Custom error", "custom_value");
    
    EXPECT_EQ(custom_error.custom_field(), "custom_value");
    EXPECT_EQ(custom_error.category(), "custom");
    
    // Should still be catchable as base class
    try {
        throw custom_error;
    } catch (const ScratchpadError& e) {
        EXPECT_EQ(e.category(), "custom");
    }
}