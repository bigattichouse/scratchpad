#include <gtest/gtest.h>
#include "domain/vm/vm_id.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"

using namespace scratchpad;
using namespace scratchpad::test;

class VMIdTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test data for various scenarios
        valid_ids = {
            "test-vm",
            "my-development-vm",
            "vm123",
            "Test_VM_1",
            "a",
            std::string(VMId::MAX_LENGTH, 'a')  // Maximum length
        };
        
        invalid_ids = {
            "",                                    // Empty
            "vm with spaces",                      // Spaces
            "vm!@#$%",                            // Special characters
            "vm/with/slashes",                    // Slashes
            "-starting-with-dash",                // Starting with dash
            "ending-with-dash-",                  // Ending with dash
            std::string(VMId::MAX_LENGTH + 1, 'a') // Too long
        };
    }
    
    std::vector<std::string> valid_ids;
    std::vector<std::string> invalid_ids;
};

// Basic construction tests
TEST_F(VMIdTest, ConstructFromValidString) {
    for (const auto& id : valid_ids) {
        EXPECT_NO_THROW({
            VMId vm_id(id);
            EXPECT_EQ(vm_id.value(), id);
            EXPECT_FALSE(vm_id.empty());
            EXPECT_EQ(vm_id.length(), id.length());
        }) << "Failed for ID: " << id;
    }
}

TEST_F(VMIdTest, ConstructFromInvalidString) {
    for (const auto& id : invalid_ids) {
        EXPECT_THROW({
            VMId vm_id(id);
        }, ScratchpadError) << "Should have thrown for invalid ID: " << id;
    }
}

TEST_F(VMIdTest, MoveConstruction) {
    std::string id = "test-vm";
    std::string original_id = id;
    
    VMId vm_id(std::move(id));
    EXPECT_EQ(vm_id.value(), original_id);
    EXPECT_TRUE(id.empty() || id != original_id); // Move should have occurred
}

TEST_F(VMIdTest, DefaultConstruction) {
    VMId vm_id;
    EXPECT_TRUE(vm_id.empty());
    EXPECT_EQ(vm_id.length(), 0);
    EXPECT_EQ(vm_id.value(), "");
}

// Copy and assignment tests
TEST_F(VMIdTest, CopyConstruction) {
    VMId original("test-vm");
    VMId copy(original);
    
    EXPECT_EQ(copy.value(), original.value());
    EXPECT_EQ(copy.value(), "test-vm");
}

TEST_F(VMIdTest, CopyAssignment) {
    VMId original("test-vm");
    VMId assigned;
    
    assigned = original;
    EXPECT_EQ(assigned.value(), original.value());
    EXPECT_EQ(assigned.value(), "test-vm");
}

TEST_F(VMIdTest, MoveAssignment) {
    VMId original("test-vm");
    std::string original_value = original.value();
    VMId assigned;
    
    assigned = std::move(original);
    EXPECT_EQ(assigned.value(), original_value);
}

// Validation tests
TEST_F(VMIdTest, IsValidFormat) {
    for (const auto& id : valid_ids) {
        EXPECT_TRUE(VMId::is_valid_format(id)) << "Should be valid: " << id;
    }
    
    for (const auto& id : invalid_ids) {
        EXPECT_FALSE(VMId::is_valid_format(id)) << "Should be invalid: " << id;
    }
}

TEST_F(VMIdTest, Normalize) {
    // Test normalization of various inputs
    EXPECT_EQ(VMId::normalize("Test VM 123"), "test-vm-123");
    EXPECT_EQ(VMId::normalize("My Development VM"), "my-development-vm");
    EXPECT_EQ(VMId::normalize("vm_with_underscores"), "vm-with-underscores");
    EXPECT_EQ(VMId::normalize("VM@#$%123"), "vm-123");
    EXPECT_EQ(VMId::normalize("--multiple--dashes--"), "multiple-dashes");
    
    // Test length truncation
    std::string long_name(100, 'a');
    std::string normalized = VMId::normalize(long_name);
    EXPECT_LE(normalized.length(), VMId::MAX_LENGTH);
}

// Unique generation tests
TEST_F(VMIdTest, GenerateUnique) {
    // Generate multiple IDs and ensure they're unique
    std::set<std::string> generated_ids;
    
    for (int i = 0; i < 100; ++i) {
        VMId vm_id = VMId::generate_unique();
        EXPECT_FALSE(vm_id.empty());
        EXPECT_TRUE(VMId::is_valid_format(vm_id.value()));
        
        // Should be unique
        EXPECT_TRUE(generated_ids.insert(vm_id.value()).second) 
            << "Generated duplicate ID: " << vm_id.value();
    }
}

TEST_F(VMIdTest, GenerateUniqueWithPrefix) {
    std::string prefix = "test";
    
    for (int i = 0; i < 10; ++i) {
        VMId vm_id = VMId::generate_unique(prefix);
        EXPECT_FALSE(vm_id.empty());
        EXPECT_TRUE(vm_id.value().starts_with(prefix));
        EXPECT_TRUE(VMId::is_valid_format(vm_id.value()));
    }
}

// Conversion tests
TEST_F(VMIdTest, ImplicitStringConversion) {
    VMId vm_id("test-vm");
    const std::string& str_ref = vm_id;
    EXPECT_EQ(str_ref, "test-vm");
}

TEST_F(VMIdTest, ExplicitStringConversion) {
    VMId vm_id("test-vm");
    std::string str = vm_id.to_string();
    EXPECT_EQ(str, "test-vm");
}

// Comparison tests
TEST_F(VMIdTest, EqualityComparison) {
    VMId vm_id1("test-vm");
    VMId vm_id2("test-vm");
    VMId vm_id3("different-vm");
    
    EXPECT_TRUE(vm_id1 == vm_id2);
    EXPECT_FALSE(vm_id1 == vm_id3);
    EXPECT_FALSE(vm_id1 != vm_id2);
    EXPECT_TRUE(vm_id1 != vm_id3);
}

TEST_F(VMIdTest, LessThanComparison) {
    VMId vm_id1("abc");
    VMId vm_id2("def");
    VMId vm_id3("abc");
    
    EXPECT_TRUE(vm_id1 < vm_id2);
    EXPECT_FALSE(vm_id2 < vm_id1);
    EXPECT_FALSE(vm_id1 < vm_id3);
}

// Hash function tests
TEST_F(VMIdTest, HashFunction) {
    VMId vm_id1("test-vm");
    VMId vm_id2("test-vm");
    VMId vm_id3("different-vm");
    
    std::hash<VMId> hasher;
    
    // Same IDs should have same hash
    EXPECT_EQ(hasher(vm_id1), hasher(vm_id2));
    
    // Different IDs should typically have different hashes
    EXPECT_NE(hasher(vm_id1), hasher(vm_id3));
}

TEST_F(VMIdTest, UseInUnorderedMap) {
    std::unordered_map<VMId, std::string> vm_map;
    
    VMId vm_id1("vm1");
    VMId vm_id2("vm2");
    
    vm_map[vm_id1] = "value1";
    vm_map[vm_id2] = "value2";
    
    EXPECT_EQ(vm_map.size(), 2);
    EXPECT_EQ(vm_map[vm_id1], "value1");
    EXPECT_EQ(vm_map[vm_id2], "value2");
}

// Edge case tests
TEST_F(VMIdTest, MaxLengthHandling) {
    std::string max_length_id(VMId::MAX_LENGTH, 'a');
    
    EXPECT_NO_THROW({
        VMId vm_id(max_length_id);
        EXPECT_EQ(vm_id.length(), VMId::MAX_LENGTH);
    });
    
    std::string too_long_id(VMId::MAX_LENGTH + 1, 'a');
    EXPECT_THROW({
        VMId vm_id(too_long_id);
    }, ScratchpadError);
}

TEST_F(VMIdTest, SpecialCharacterHandling) {
    // Test various special characters that should be rejected
    std::vector<std::string> special_chars = {
        "vm.with.dots",
        "vm:with:colons",
        "vm;with;semicolons",
        "vm|with|pipes",
        "vm&with&ampersands"
    };
    
    for (const auto& id : special_chars) {
        EXPECT_FALSE(VMId::is_valid_format(id)) << "Should reject: " << id;
        EXPECT_THROW({
            VMId vm_id(id);
        }, ScratchpadError) << "Should throw for: " << id;
    }
}