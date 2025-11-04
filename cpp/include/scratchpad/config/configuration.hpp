#pragma once

#include <string>
#include <unordered_map>
#include <vector>
#include <any>
#include <nlohmann/json.hpp>

namespace scratchpad {

/**
 * Configuration management class for Scratchpad
 * 
 * Handles loading configuration from files, environment variables,
 * and provides type-safe access to configuration values with validation.
 */
class Configuration {
public:
    /**
     * Default constructor - loads default configuration
     */
    Configuration();

    /**
     * Load configuration from JSON file
     * @param config_path Path to configuration file
     */
    void load_from_file(const std::string& config_path);

    /**
     * Save current configuration to JSON file
     * @param config_path Path to save configuration
     */
    void save_to_file(const std::string& config_path) const;

    /**
     * Load environment variable overrides
     */
    void load_environment_overrides();

    /**
     * Validate current configuration
     * @return true if configuration is valid
     * @throws ConfigurationError if validation fails
     */
    bool validate() const;

    // ========== Type-safe getters ==========

    /**
     * Get string configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value
     */
    std::string get_string(const std::string& key, const std::string& default_value = {}) const {
        auto it = values_.find(key);
        if (it != values_.end() && it->second.is_string()) {
            return it->second.get<std::string>();
        }
        return default_value;
    }

    /**
     * Get integer configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value
     */
    int get_int(const std::string& key, int default_value = 0) const {
        auto it = values_.find(key);
        if (it != values_.end() && it->second.is_number_integer()) {
            return it->second.get<int>();
        }
        return default_value;
    }

    /**
     * Get double configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value
     */
    double get_double(const std::string& key, double default_value = 0.0) const {
        auto it = values_.find(key);
        if (it != values_.end() && it->second.is_number()) {
            return it->second.get<double>();
        }
        return default_value;
    }

    /**
     * Get boolean configuration value
     * @param key Configuration key
     * @param default_value Default value if key not found
     * @return Configuration value
     */
    bool get_bool(const std::string& key, bool default_value = false) const {
        auto it = values_.find(key);
        if (it != values_.end() && it->second.is_boolean()) {
            return it->second.get<bool>();
        }
        return default_value;
    }

    /**
     * Get array configuration value
     * @param key Configuration key
     * @return Configuration value as vector of strings
     */
    std::vector<std::string> get_string_array(const std::string& key) const {
        auto it = values_.find(key);
        if (it != values_.end() && it->second.is_array()) {
            std::vector<std::string> result;
            for (const auto& item : it->second) {
                if (item.is_string()) {
                    result.push_back(item.get<std::string>());
                }
            }
            return result;
        }
        return {};
    }

    // ========== Setters ==========

    /**
     * Set string configuration value
     * @param key Configuration key
     * @param value Configuration value
     */
    void set_string(const std::string& key, const std::string& value) {
        values_[key] = value;
    }

    /**
     * Set integer configuration value
     * @param key Configuration key
     * @param value Configuration value
     */
    void set_int(const std::string& key, int value) {
        values_[key] = value;
    }

    /**
     * Set double configuration value
     * @param key Configuration key
     * @param value Configuration value
     */
    void set_double(const std::string& key, double value) {
        values_[key] = value;
    }

    /**
     * Set boolean configuration value
     * @param key Configuration key
     * @param value Configuration value
     */
    void set_bool(const std::string& key, bool value) {
        values_[key] = value;
    }

    /**
     * Set array configuration value
     * @param key Configuration key
     * @param value Configuration value
     */
    void set_string_array(const std::string& key, const std::vector<std::string>& value) {
        values_[key] = value;
    }

    // ========== Utilities ==========

    /**
     * Check if configuration key exists
     * @param key Configuration key
     * @return true if key exists
     */
    bool has_value(const std::string& key) const {
        return values_.find(key) != values_.end();
    }

    /**
     * Remove configuration key
     * @param key Configuration key
     */
    void remove(const std::string& key) {
        values_.erase(key);
    }

    /**
     * Get all configuration keys
     * @return Vector of all keys
     */
    std::vector<std::string> get_all_keys() const {
        std::vector<std::string> keys;
        for (const auto& [key, value] : values_) {
            keys.push_back(key);
        }
        return keys;
    }

    /**
     * Clear all configuration values and reload defaults
     */
    void reset_to_defaults() {
        values_.clear();
        load_defaults();
    }

private:
    nlohmann::json values_;

    void load_defaults();
    void load_env_override(const std::string& env_var, const std::string& config_key);
    
    // Validation helpers
    void validate_directory(const std::string& key, std::vector<std::string>& errors) const;
    void validate_positive_integer(const std::string& key, std::vector<std::string>& errors) const;
    void validate_port_range(const std::string& start_key, const std::string& end_key, 
                            std::vector<std::string>& errors) const;
    void validate_percentage(const std::string& key, std::vector<std::string>& errors) const;
    
    // Default value generators
    static std::string get_default_vm_directory();
    static std::string get_default_images_directory();
    static std::string get_default_ssh_keys_directory();
    static std::string get_default_cloud_init_directory();
};

} // namespace scratchpad