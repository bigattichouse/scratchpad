#pragma once

#include "scratchpad/types.hpp"
#include <string>
#include <chrono>
#include <functional>

namespace scratchpad {

/**
 * Value object representing a virtual machine identifier
 * 
 * VMId enforces naming conventions and validation rules for VM identifiers.
 * It ensures IDs are unique, valid, and follow consistent formatting.
 */
class VMId {
public:
    static constexpr size_t MAX_LENGTH = 64;

    /**
     * Construct VMId from string (validates format)
     * @param id VM identifier string
     * @throws VMError if ID format is invalid
     */
    explicit VMId(const std::string& id);
    
    /**
     * Construct VMId from string (move semantics)
     * @param id VM identifier string
     * @throws VMError if ID format is invalid
     */
    explicit VMId(std::string&& id);

    /**
     * Default constructor creates empty VMId (invalid)
     */
    VMId() = default;

    /**
     * Copy constructor
     */
    VMId(const VMId&) = default;

    /**
     * Move constructor
     */
    VMId(VMId&&) = default;

    /**
     * Copy assignment operator
     */
    VMId& operator=(const VMId&) = default;

    /**
     * Move assignment operator
     */
    VMId& operator=(VMId&&) = default;

    /**
     * Get the string value of the ID
     * @return ID string
     */
    const std::string& value() const { return value_; }

    /**
     * Check if this VMId is empty/invalid
     * @return true if empty
     */
    bool empty() const { return value_.empty(); }

    /**
     * Get length of the ID
     * @return Length in characters
     */
    size_t length() const { return value_.length(); }

    /**
     * Convert to string (implicit conversion)
     */
    operator const std::string&() const { return value_; }

    /**
     * Convert to string (explicit conversion)
     */
    std::string to_string() const { return value_; }

    // ========== Static utility methods ==========

    /**
     * Generate a unique VM ID with optional prefix
     * @param prefix Optional prefix for the ID
     * @return Unique VMId
     */
    static VMId generate_unique(const std::string& prefix = "");

    /**
     * Check if a string is a valid VM ID format
     * @param id String to check
     * @return true if valid format
     */
    static bool is_valid_format(const std::string& id);

    /**
     * Normalize a string to valid VM ID format
     * @param id String to normalize
     * @return Normalized VM ID string
     */
    static std::string normalize(const std::string& id);

    // ========== Comparison operators ==========

    bool operator<(const VMId& other) const;
    bool operator==(const VMId& other) const;
    bool operator!=(const VMId& other) const;

private:
    std::string value_;

    void validate() const;
};

} // namespace scratchpad

// Hash function for VMId to use in std::unordered_map
namespace std {
    template<>
    struct hash<scratchpad::VMId> {
        size_t operator()(const scratchpad::VMId& vm_id) const {
            return hash<string>()(vm_id.value());
        }
    };
}