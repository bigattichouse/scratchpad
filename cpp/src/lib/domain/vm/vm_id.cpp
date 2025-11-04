#include "scratchpad/domain/vm/vm_id.hpp"
#include "scratchpad/errors.hpp"
#include <regex>
#include <algorithm>
#include <cctype>

namespace scratchpad {

VMId::VMId(const std::string& id) : value_(id) {
    validate();
}

VMId::VMId(std::string&& id) : value_(std::move(id)) {
    validate();
}

void VMId::validate() const {
    if (value_.empty()) {
        throw ValidationError("VM ID cannot be empty");
    }
    
    if (value_.length() > MAX_LENGTH) {
        throw ValidationError("VM ID cannot be longer than " + std::to_string(MAX_LENGTH) + " characters");
    }
    
    // Check for valid characters (alphanumeric, hyphens, underscores)
    static const std::regex valid_pattern("^[a-zA-Z0-9_-]+$");
    if (!std::regex_match(value_, valid_pattern)) {
        throw ValidationError("VM ID can only contain alphanumeric characters, hyphens, and underscores");
    }
    
    // Cannot start or end with hyphen or underscore
    if (value_.front() == '-' || value_.front() == '_' ||
        value_.back() == '-' || value_.back() == '_') {
        throw ValidationError("VM ID cannot start or end with hyphen or underscore");
    }
    
    // Check for reserved names
    static const std::vector<std::string> reserved_names = {
        "default", "all", "none", "auto", "system", "root", "admin",
        "con", "prn", "aux", "nul", // Windows reserved names
        "com1", "com2", "com3", "com4", "com5", "com6", "com7", "com8", "com9",
        "lpt1", "lpt2", "lpt3", "lpt4", "lpt5", "lpt6", "lpt7", "lpt8", "lpt9"
    };
    
    std::string lower_id = value_;
    std::transform(lower_id.begin(), lower_id.end(), lower_id.begin(), ::tolower);
    
    for (const auto& reserved : reserved_names) {
        if (lower_id == reserved) {
            throw ValidationError("VM ID '" + value_ + "' is reserved and cannot be used");
        }
    }
}

VMId VMId::generate_unique(const std::string& prefix) {
    auto now = std::chrono::system_clock::now();
    auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()).count();
    
    std::string unique_id;
    if (!prefix.empty()) {
        unique_id = prefix + "-" + std::to_string(timestamp);
    } else {
        unique_id = "vm-" + std::to_string(timestamp);
    }
    
    return VMId(unique_id);
}

bool VMId::is_valid_format(const std::string& id) {
    try {
        VMId temp(id);
        return true;
    } catch (const ValidationError&) {
        return false;
    }
}

std::string VMId::normalize(const std::string& id) {
    std::string normalized = id;
    
    // Convert to lowercase
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), ::tolower);
    
    // Replace invalid characters with hyphens
    std::replace_if(normalized.begin(), normalized.end(), [](char c) {
        return !std::isalnum(c) && c != '-' && c != '_';
    }, '-');
    
    // Remove leading/trailing hyphens and underscores
    while (!normalized.empty() && (normalized.front() == '-' || normalized.front() == '_')) {
        normalized.erase(0, 1);
    }
    while (!normalized.empty() && (normalized.back() == '-' || normalized.back() == '_')) {
        normalized.pop_back();
    }
    
    // Collapse multiple consecutive hyphens/underscores
    normalized = std::regex_replace(normalized, std::regex("[-_]+"), "-");
    
    // Ensure it's not empty after normalization
    if (normalized.empty()) {
        normalized = "vm";
    }
    
    // Truncate if too long
    if (normalized.length() > MAX_LENGTH) {
        normalized = normalized.substr(0, MAX_LENGTH);
        // Remove trailing hyphen/underscore after truncation
        while (!normalized.empty() && (normalized.back() == '-' || normalized.back() == '_')) {
            normalized.pop_back();
        }
    }
    
    return normalized;
}

bool VMId::operator<(const VMId& other) const {
    return value_ < other.value_;
}

bool VMId::operator==(const VMId& other) const {
    return value_ == other.value_;
}

bool VMId::operator!=(const VMId& other) const {
    return value_ != other.value_;
}

} // namespace scratchpad