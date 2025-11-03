#include <gtest/gtest.h>
#include <string>

// Simple test to verify GoogleTest framework is working
TEST(SimpleTest, BasicAssertions) {
    EXPECT_EQ(7 * 6, 42);
    EXPECT_TRUE(true);
    EXPECT_FALSE(false);
    EXPECT_STREQ("hello", "hello");
}

TEST(SimpleTest, StringOperations) {
    std::string test_str = "Hello World";
    EXPECT_EQ(test_str.length(), 11);
    EXPECT_TRUE(test_str.find("World") != std::string::npos);
    EXPECT_EQ(test_str.substr(0, 5), "Hello");
}

TEST(SimpleTest, ContainerOperations) {
    std::vector<int> numbers = {1, 2, 3, 4, 5};
    EXPECT_EQ(numbers.size(), 5);
    EXPECT_EQ(numbers[0], 1);
    EXPECT_EQ(numbers.back(), 5);
    
    auto it = std::find(numbers.begin(), numbers.end(), 3);
    EXPECT_NE(it, numbers.end());
    EXPECT_EQ(*it, 3);
}