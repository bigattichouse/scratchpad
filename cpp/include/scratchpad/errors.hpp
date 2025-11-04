#pragma once

#include "types.hpp"
#include <stdexcept>
#include <string>
#include <string_view>

namespace scratchpad {

/**
 * Base exception class for all Scratchpad errors
 */
class ScratchpadError : public std::runtime_error {
public:
    explicit ScratchpadError(ErrorCode code, std::string_view message)
        : std::runtime_error(std::string(message))
        , error_code_(code) {}
    
    ErrorCode code() const noexcept { return error_code_; }
    
    virtual std::string_view category() const noexcept { return "scratchpad"; }

private:
    ErrorCode error_code_;
};

/**
 * VM-related errors
 */
class VMError : public ScratchpadError {
public:
    explicit VMError(ErrorCode code, std::string_view message, std::string vm_id = "")
        : ScratchpadError(code, message)
        , vm_id_(std::move(vm_id)) {}
    
    const std::string& vm_id() const noexcept { return vm_id_; }
    std::string_view category() const noexcept override { return "vm"; }

private:
    std::string vm_id_;
};

/**
 * Process management errors
 */
class ProcessError : public ScratchpadError {
public:
    explicit ProcessError(ErrorCode code, std::string_view message, ProcessId pid = 0)
        : ScratchpadError(code, message)
        , process_id_(pid) {}
    
    ProcessId process_id() const noexcept { return process_id_; }
    std::string_view category() const noexcept override { return "process"; }

private:
    ProcessId process_id_;
};

/**
 * SSH communication errors
 */
class SSHError : public ScratchpadError {
public:
    explicit SSHError(ErrorCode code, std::string_view message, std::string host = {}, PortNumber port = 0)
        : ScratchpadError(code, message)
        , host_(std::move(host))
        , port_(port) {}
    
    const std::string& host() const noexcept { return host_; }
    PortNumber port() const noexcept { return port_; }
    std::string_view category() const noexcept override { return "ssh"; }

private:
    std::string host_;
    PortNumber port_;
};

/**
 * Image management errors
 */
class ImageError : public ScratchpadError {
public:
    explicit ImageError(ErrorCode code, std::string_view message, std::string image_name = {})
        : ScratchpadError(code, message)
        , image_name_(std::move(image_name)) {}
    
    const std::string& image_name() const noexcept { return image_name_; }
    std::string_view category() const noexcept override { return "image"; }

private:
    std::string image_name_;
};

/**
 * Resource allocation errors
 */
class ResourceError : public ScratchpadError {
public:
    explicit ResourceError(ErrorCode code, std::string_view message, std::string resource_type = {})
        : ScratchpadError(code, message)
        , resource_type_(std::move(resource_type)) {}
    
    const std::string& resource_type() const noexcept { return resource_type_; }
    std::string_view category() const noexcept override { return "resource"; }

private:
    std::string resource_type_;
};

/**
 * System-level errors
 */
class SystemError : public ScratchpadError {
public:
    explicit SystemError(ErrorCode code, std::string_view message, int system_error_code = 0)
        : ScratchpadError(code, message)
        , system_error_code_(system_error_code) {}
    
    int system_error_code() const noexcept { return system_error_code_; }
    std::string_view category() const noexcept override { return "system"; }

private:
    int system_error_code_;
};

/**
 * Configuration errors
 */
class ConfigurationError : public ScratchpadError {
public:
    explicit ConfigurationError(ErrorCode code, std::string_view message, std::string config_key = {})
        : ScratchpadError(code, message)
        , config_key_(std::move(config_key)) {}
    
    const std::string& config_key() const noexcept { return config_key_; }
    std::string_view category() const noexcept override { return "config"; }

private:
    std::string config_key_;
};

/**
 * Validation errors for input validation
 */
class ValidationError : public ScratchpadError {
public:
    explicit ValidationError(std::string_view message)
        : ScratchpadError(ErrorCode::InvalidArgument, message) {}
    
    std::string_view category() const noexcept override { return "validation"; }
};

// Utility functions for error handling

/**
 * Convert error code to human-readable string
 */
std::string_view error_code_to_string(ErrorCode code) noexcept;

/**
 * Get error category from error code
 */
std::string_view get_error_category(ErrorCode code) noexcept;

/**
 * Check if error code indicates a recoverable error
 */
bool is_recoverable_error(ErrorCode code) noexcept;

/**
 * Create appropriate exception type based on error code
 */
std::unique_ptr<ScratchpadError> make_error(ErrorCode code, std::string_view message);

// Convenience macros for throwing errors
#define THROW_VM_ERROR(code, message, vm_id) \
    throw VMError(code, message, vm_id)

#define THROW_PROCESS_ERROR(code, message, pid) \
    throw ProcessError(code, message, pid)

#define THROW_SSH_ERROR(code, message, host, port) \
    throw SSHError(code, message, host, port)

#define THROW_IMAGE_ERROR(code, message, image_name) \
    throw ImageError(code, message, image_name)

#define THROW_RESOURCE_ERROR(code, message, resource_type) \
    throw ResourceError(code, message, resource_type)

#define THROW_SYSTEM_ERROR(code, message, system_code) \
    throw SystemError(code, message, system_code)

#define THROW_CONFIG_ERROR(code, message, config_key) \
    throw ConfigurationError(code, message, config_key)

} // namespace scratchpad