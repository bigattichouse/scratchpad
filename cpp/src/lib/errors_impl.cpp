#include "scratchpad/errors.hpp"
#include <unordered_map>

namespace scratchpad {

std::string_view error_code_to_string(ErrorCode code) noexcept {
    static const std::unordered_map<ErrorCode, std::string_view> error_strings = {
        // Success
        {ErrorCode::Success, "success"},
        
        // VM errors (100-199)
        {ErrorCode::VMNotFound, "vm_not_found"},
        {ErrorCode::VMAlreadyExists, "vm_already_exists"},
        {ErrorCode::VMNotRunning, "vm_not_running"},
        {ErrorCode::VMStartupFailed, "vm_startup_failed"},
        {ErrorCode::VMShutdownFailed, "vm_shutdown_failed"},
        
        // Process errors (200-299)
        {ErrorCode::ProcessNotFound, "process_not_found"},
        {ErrorCode::ProcessStartFailed, "process_start_failed"},
        {ErrorCode::ProcessKillFailed, "process_kill_failed"},
        {ErrorCode::QemuNotFound, "qemu_not_found"},
        
        // SSH errors (300-399)
        {ErrorCode::SSHConnectionFailed, "ssh_connection_failed"},
        {ErrorCode::SSHAuthenticationFailed, "ssh_authentication_failed"},
        {ErrorCode::SSHCommandFailed, "ssh_command_failed"},
        {ErrorCode::SSHKeyNotFound, "ssh_key_not_found"},
        
        // Image errors (400-499)
        {ErrorCode::ImageNotFound, "image_not_found"},
        {ErrorCode::ImageDownloadFailed, "image_download_failed"},
        {ErrorCode::ImageCorrupted, "image_corrupted"},
        {ErrorCode::ProvisioningFailed, "provisioning_failed"},
        
        // Resource errors (500-599)
        {ErrorCode::OutOfMemory, "out_of_memory"},
        {ErrorCode::OutOfDiskSpace, "out_of_disk_space"},
        {ErrorCode::PortUnavailable, "port_unavailable"},
        {ErrorCode::InsufficientResources, "insufficient_resources"},
        
        // System errors (600-699)
        {ErrorCode::FileSystemError, "filesystem_error"},
        {ErrorCode::NetworkError, "network_error"},
        {ErrorCode::PermissionDenied, "permission_denied"},
        {ErrorCode::ConfigurationError, "configuration_error"},
        
        // Generic errors (900-999)
        {ErrorCode::InvalidArgument, "invalid_argument"},
        {ErrorCode::NotImplemented, "not_implemented"},
        {ErrorCode::InternalError, "internal_error"},
        {ErrorCode::UnknownError, "unknown_error"}
    };
    
    auto it = error_strings.find(code);
    if (it != error_strings.end()) {
        return it->second;
    }
    
    return "unknown_error";
}

std::string_view get_error_category(ErrorCode code) noexcept {
    int code_value = static_cast<int>(code);
    
    if (code_value >= 100 && code_value < 200) {
        return "vm";
    } else if (code_value >= 200 && code_value < 300) {
        return "process";
    } else if (code_value >= 300 && code_value < 400) {
        return "ssh";
    } else if (code_value >= 400 && code_value < 500) {
        return "image";
    } else if (code_value >= 500 && code_value < 600) {
        return "resource";
    } else if (code_value >= 600 && code_value < 700) {
        return "system";
    } else if (code_value >= 900 && code_value < 1000) {
        return "generic";
    }
    
    return "unknown";
}

bool is_recoverable_error(ErrorCode code) noexcept {
    switch (code) {
        // Recoverable errors - can be retried or fixed
        case ErrorCode::SSHConnectionFailed:
        case ErrorCode::SSHCommandFailed:
        case ErrorCode::ImageDownloadFailed:
        case ErrorCode::PortUnavailable:
        case ErrorCode::NetworkError:
        case ErrorCode::ProcessStartFailed:
        case ErrorCode::VMStartupFailed:
            return true;
            
        // Non-recoverable errors - indicate permanent issues
        case ErrorCode::VMNotFound:
        case ErrorCode::VMAlreadyExists:
        case ErrorCode::ProcessNotFound:
        case ErrorCode::QemuNotFound:
        case ErrorCode::SSHAuthenticationFailed:
        case ErrorCode::SSHKeyNotFound:
        case ErrorCode::ImageNotFound:
        case ErrorCode::ImageCorrupted:
        case ErrorCode::OutOfMemory:
        case ErrorCode::OutOfDiskSpace:
        case ErrorCode::InsufficientResources:
        case ErrorCode::FileSystemError:
        case ErrorCode::PermissionDenied:
        case ErrorCode::ConfigurationError:
        case ErrorCode::InvalidArgument:
        case ErrorCode::NotImplemented:
        case ErrorCode::InternalError:
            return false;
            
        // Special cases
        case ErrorCode::Success:
            return false; // Not an error
            
        case ErrorCode::ProvisioningFailed:
        case ErrorCode::VMShutdownFailed:
        case ErrorCode::ProcessKillFailed:
            return true; // Can sometimes be retried
            
        default:
            return false; // Conservative default
    }
}

std::unique_ptr<ScratchpadError> make_error(ErrorCode code, std::string_view message) {
    std::string_view category = get_error_category(code);
    
    if (category == "vm") {
        return std::make_unique<VMError>(code, message);
    } else if (category == "process") {
        return std::make_unique<ProcessError>(code, message);
    } else if (category == "ssh") {
        return std::make_unique<SSHError>(code, message);
    } else if (category == "image") {
        return std::make_unique<ImageError>(code, message);
    } else if (category == "resource") {
        return std::make_unique<ResourceError>(code, message);
    } else if (category == "system") {
        return std::make_unique<SystemError>(code, message);
    } else {
        return std::make_unique<ScratchpadError>(code, message);
    }
}

} // namespace scratchpad