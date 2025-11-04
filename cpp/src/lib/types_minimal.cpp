#include "scratchpad/types.hpp"
#include "scratchpad/errors.hpp"
#include <regex>
#include <cctype>

namespace scratchpad {

MemoryAmount MemoryAmount::from_string(const std::string& str) {
    std::regex memory_regex(R"((\d+)([KMGT]?)B?)");
    std::smatch match;
    
    if (!std::regex_match(str, match, memory_regex)) {
        throw ValidationError("Invalid memory format: " + str);
    }
    
    uint64_t value = std::stoull(match[1].str());
    std::string unit = match[2].str();
    
    if (unit.empty()) {
        return {value};
    } else if (unit == "K") {
        return {value * 1024};
    } else if (unit == "M") {
        return {value * 1024 * 1024};
    } else if (unit == "G") {
        return {value * 1024 * 1024 * 1024};
    } else if (unit == "T") {
        return {value * 1024ULL * 1024 * 1024 * 1024};
    }
    
    throw ValidationError("Unknown memory unit: " + unit);
}

std::string MemoryAmount::to_string() const {
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        uint64_t tb = bytes / (1024ULL * 1024 * 1024 * 1024);
        return std::to_string(tb) + "T";
    } else if (bytes >= 1024 * 1024 * 1024) {
        uint64_t gb = bytes / (1024 * 1024 * 1024);
        return std::to_string(gb) + "G";
    } else if (bytes >= 1024 * 1024) {
        uint64_t mb = bytes / (1024 * 1024);
        return std::to_string(mb) + "M";
    } else if (bytes >= 1024) {
        uint64_t kb = bytes / 1024;
        return std::to_string(kb) + "K";
    } else {
        return std::to_string(bytes) + "B";
    }
}

DiskSize DiskSize::from_string(const std::string& str) {
    std::regex disk_regex(R"((\d+)([KMGT]?)B?)");
    std::smatch match;
    
    if (!std::regex_match(str, match, disk_regex)) {
        throw ValidationError("Invalid disk size format: " + str);
    }
    
    uint64_t value = std::stoull(match[1].str());
    std::string unit = match[2].str();
    
    if (unit.empty()) {
        return {value};
    } else if (unit == "K") {
        return {value * 1024};
    } else if (unit == "M") {
        return {value * 1024 * 1024};
    } else if (unit == "G") {
        return {value * 1024 * 1024 * 1024};
    } else if (unit == "T") {
        return {value * 1024ULL * 1024 * 1024 * 1024};
    }
    
    throw ValidationError("Unknown disk size unit: " + unit);
}

std::string DiskSize::to_string() const {
    if (bytes >= 1024ULL * 1024 * 1024 * 1024) {
        uint64_t tb = bytes / (1024ULL * 1024 * 1024 * 1024);
        return std::to_string(tb) + "T";
    } else if (bytes >= 1024 * 1024 * 1024) {
        uint64_t gb = bytes / (1024 * 1024 * 1024);
        return std::to_string(gb) + "G";
    } else if (bytes >= 1024 * 1024) {
        uint64_t mb = bytes / (1024 * 1024);
        return std::to_string(mb) + "M";
    } else if (bytes >= 1024) {
        uint64_t kb = bytes / 1024;
        return std::to_string(kb) + "K";
    } else {
        return std::to_string(bytes) + "B";
    }
}

} // namespace scratchpad