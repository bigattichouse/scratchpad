#include "scratchpad/config/configuration.hpp"
#include "scratchpad/errors.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <filesystem>
#include <cstdlib>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace scratchpad {

Configuration::Configuration() {
    load_defaults();
}

void Configuration::load_defaults() {
    // Set default values
    values_["vm_directory"] = get_default_vm_directory();
    values_["images_directory"] = get_default_images_directory();
    values_["ssh_keys_directory"] = get_default_ssh_keys_directory();
    values_["cloud_init_directory"] = get_default_cloud_init_directory();
    
    values_["default_memory"] = "512M";
    values_["default_disk_size"] = "10G";
    values_["ssh_timeout"] = 60000;  // milliseconds
    values_["health_check_interval"] = 30000;  // milliseconds
    
    values_["ssh_port_range_start"] = 2222;
    values_["ssh_port_range_end"] = 9999;
    values_["vnc_port_range_start"] = 5900;
    values_["vnc_port_range_end"] = 5999;
    
    values_["enable_health_monitoring"] = true;
    values_["enable_resource_monitoring"] = true;
    values_["memory_reserve_percentage"] = 0.1;
    values_["disk_reserve_percentage"] = 0.1;
    
    values_["max_concurrent_downloads"] = 2;
    values_["download_timeout"] = 300000;  // milliseconds
    values_["verify_checksums"] = true;
}

void Configuration::load_from_file(const std::string& config_path) {
    if (!fs::exists(config_path)) {
        return; // File doesn't exist, use defaults
    }
    
    try {
        std::ifstream file(config_path);
        if (!file.is_open()) {
            THROW_CONFIG_ERROR(ErrorCode::FileSystemError, 
                              "Cannot open configuration file", config_path);
        }
        
        json config_json;
        file >> config_json;
        
        // Merge with defaults
        for (const auto& [key, value] : config_json.items()) {
            values_[key] = value;
        }
        
    } catch (const json::exception& e) {
        THROW_CONFIG_ERROR(ErrorCode::ConfigurationError,
                          "Invalid JSON in configuration file: " + std::string(e.what()),
                          config_path);
    } catch (const std::exception& e) {
        THROW_CONFIG_ERROR(ErrorCode::FileSystemError,
                          "Error reading configuration file: " + std::string(e.what()),
                          config_path);
    }
}

void Configuration::save_to_file(const std::string& config_path) const {
    try {
        // Ensure directory exists
        fs::create_directories(fs::path(config_path).parent_path());
        
        json config_json(values_);
        
        std::ofstream file(config_path);
        if (!file.is_open()) {
            THROW_CONFIG_ERROR(ErrorCode::FileSystemError,
                              "Cannot create configuration file", config_path);
        }
        
        file << config_json.dump(2);  // Pretty print with 2-space indent
        
    } catch (const json::exception& e) {
        THROW_CONFIG_ERROR(ErrorCode::ConfigurationError,
                          "Error serializing configuration: " + std::string(e.what()),
                          config_path);
    } catch (const std::exception& e) {
        THROW_CONFIG_ERROR(ErrorCode::FileSystemError,
                          "Error writing configuration file: " + std::string(e.what()),
                          config_path);
    }
}

void Configuration::load_environment_overrides() {
    // Check for environment variable overrides
    load_env_override("SCRATCHPAD_VM_DIR", "vm_directory");
    load_env_override("SCRATCHPAD_IMAGES_DIR", "images_directory");
    load_env_override("SCRATCHPAD_SSH_KEYS_DIR", "ssh_keys_directory");
    load_env_override("SCRATCHPAD_CLOUD_INIT_DIR", "cloud_init_directory");
    
    load_env_override("SCRATCHPAD_DEFAULT_MEMORY", "default_memory");
    load_env_override("SCRATCHPAD_DEFAULT_DISK_SIZE", "default_disk_size");
    load_env_override("SCRATCHPAD_SSH_TIMEOUT", "ssh_timeout");
    
    load_env_override("SCRATCHPAD_SSH_PORT_START", "ssh_port_range_start");
    load_env_override("SCRATCHPAD_SSH_PORT_END", "ssh_port_range_end");
    load_env_override("SCRATCHPAD_VNC_PORT_START", "vnc_port_range_start");
    load_env_override("SCRATCHPAD_VNC_PORT_END", "vnc_port_range_end");
}

void Configuration::load_env_override(const std::string& env_var, const std::string& config_key) {
    const char* env_value = std::getenv(env_var.c_str());
    if (env_value != nullptr) {
        std::string value_str(env_value);
        
        // Try to parse as appropriate type based on current value
        if (values_[config_key].is_number_integer()) {
            try {
                values_[config_key] = std::stoi(value_str);
            } catch (const std::exception&) {
                // Keep original value if parsing fails
            }
        } else if (values_[config_key].is_number_float()) {
            try {
                values_[config_key] = std::stod(value_str);
            } catch (const std::exception&) {
                // Keep original value if parsing fails
            }
        } else if (values_[config_key].is_boolean()) {
            std::string lower_value = value_str;
            std::transform(lower_value.begin(), lower_value.end(), lower_value.begin(), ::tolower);
            if (lower_value == "true" || lower_value == "1" || lower_value == "yes") {
                values_[config_key] = true;
            } else if (lower_value == "false" || lower_value == "0" || lower_value == "no") {
                values_[config_key] = false;
            }
        } else {
            // String value
            values_[config_key] = value_str;
        }
    }
}

bool Configuration::validate() const {
    std::vector<std::string> errors;
    
    // Validate required directories
    validate_directory("vm_directory", errors);
    validate_directory("images_directory", errors);
    validate_directory("ssh_keys_directory", errors);
    validate_directory("cloud_init_directory", errors);
    
    // Validate numeric values
    validate_positive_integer("ssh_timeout", errors);
    validate_positive_integer("health_check_interval", errors);
    validate_positive_integer("download_timeout", errors);
    validate_positive_integer("max_concurrent_downloads", errors);
    
    // Validate port ranges
    validate_port_range("ssh_port_range_start", "ssh_port_range_end", errors);
    validate_port_range("vnc_port_range_start", "vnc_port_range_end", errors);
    
    // Validate percentages
    validate_percentage("memory_reserve_percentage", errors);
    validate_percentage("disk_reserve_percentage", errors);
    
    if (!errors.empty()) {
        std::string combined_errors;
        for (const auto& error : errors) {
            if (!combined_errors.empty()) combined_errors += "; ";
            combined_errors += error;
        }
        THROW_CONFIG_ERROR(ErrorCode::ConfigurationError, 
                          "Configuration validation failed: " + combined_errors);
    }
    
    return true;
}

void Configuration::validate_directory(const std::string& key, std::vector<std::string>& errors) const {
    if (!has_value(key)) {
        errors.push_back(key + " is required");
        return;
    }
    
    std::string dir_path = get_string(key);
    if (dir_path.empty()) {
        errors.push_back(key + " cannot be empty");
    }
}

void Configuration::validate_positive_integer(const std::string& key, std::vector<std::string>& errors) const {
    if (!has_value(key)) {
        errors.push_back(key + " is required");
        return;
    }
    
    if (!values_.at(key).is_number_integer() || get_int(key) <= 0) {
        errors.push_back(key + " must be a positive integer");
    }
}

void Configuration::validate_port_range(const std::string& start_key, const std::string& end_key, 
                                       std::vector<std::string>& errors) const {
    if (!has_value(start_key) || !has_value(end_key)) {
        errors.push_back("Port range keys " + start_key + " and " + end_key + " are required");
        return;
    }
    
    int start_port = get_int(start_key);
    int end_port = get_int(end_key);
    
    if (start_port < 1024 || start_port > 65535) {
        errors.push_back(start_key + " must be between 1024 and 65535");
    }
    
    if (end_port < 1024 || end_port > 65535) {
        errors.push_back(end_key + " must be between 1024 and 65535");
    }
    
    if (start_port >= end_port) {
        errors.push_back(start_key + " must be less than " + end_key);
    }
}

void Configuration::validate_percentage(const std::string& key, std::vector<std::string>& errors) const {
    if (!has_value(key)) {
        errors.push_back(key + " is required");
        return;
    }
    
    double value = get_double(key);
    if (value < 0.0 || value > 1.0) {
        errors.push_back(key + " must be between 0.0 and 1.0");
    }
}

std::string Configuration::get_default_vm_directory() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.scratchpad/vms";
    }
    return "/tmp/scratchpad/vms";
}

std::string Configuration::get_default_images_directory() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.scratchpad/images";
    }
    return "/tmp/scratchpad/images";
}

std::string Configuration::get_default_ssh_keys_directory() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.scratchpad/keys";
    }
    return "/tmp/scratchpad/keys";
}

std::string Configuration::get_default_cloud_init_directory() {
    const char* home = std::getenv("HOME");
    if (home) {
        return std::string(home) + "/.scratchpad/cloud-init";
    }
    return "/tmp/scratchpad/cloud-init";
}

} // namespace scratchpad