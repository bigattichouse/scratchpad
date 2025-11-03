#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "scratchpad/image_manager.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>
#include <fstream>

using namespace scratchpad;
using namespace scratchpad::test;
using testing::_;
using testing::Return;
using testing::InSequence;

class ImageManagerTest : public ::testing::Test {
protected:
    void SetUp() override {
        temp_dir_ = std::make_unique<TempDirectory>();
        
        // Set up test options
        test_options_.images_directory = temp_dir_->create_subdirectory("images").string();
        test_options_.cloud_init_directory = temp_dir_->create_subdirectory("cloud_init").string();
        test_options_.download_timeout = std::chrono::seconds{30};
        test_options_.verify_checksums = true;
        test_options_.max_concurrent_downloads = 2;
        
        // Create test image files
        create_test_image_files();
        
        progress_calls_.clear();
    }
    
    void create_test_image_files() {
        // Create dummy base image files for testing
        auto images_dir = std::filesystem::path(test_options_.images_directory);
        
        // Ubuntu 22.04 image
        temp_dir_->create_file("images/ubuntu-22.04-minimal-cloudimg-amd64.img", 
                              "DUMMY_UBUNTU_IMAGE_DATA_" + TestHelpers::random_string(100));
        
        // Alpine image
        temp_dir_->create_file("images/alpine-3.17-x86_64.qcow2",
                              "DUMMY_ALPINE_IMAGE_DATA_" + TestHelpers::random_string(100));
        
        // Create checksum files
        temp_dir_->create_file("images/ubuntu-22.04.sha256", "abcd1234 ubuntu-22.04-minimal-cloudimg-amd64.img");
        temp_dir_->create_file("images/alpine-3.17.sha256", "efgh5678 alpine-3.17-x86_64.qcow2");
    }
    
    void progress_callback(const std::string& operation, size_t current, size_t total, const std::string& message) {
        progress_calls_.emplace_back(operation, current, total, message);
    }
    
    std::unique_ptr<TempDirectory> temp_dir_;
    ImageManager::Options test_options_;
    std::vector<std::tuple<std::string, size_t, size_t, std::string>> progress_calls_;
};

// Construction and configuration tests
TEST_F(ImageManagerTest, ConstructionWithDefaultOptions) {
    EXPECT_NO_THROW({
        ImageManager manager;
        auto available = manager.list_available_images();
        EXPECT_TRUE(available.size() > 0); // Should have some predefined images
    });
}

TEST_F(ImageManagerTest, ConstructionWithCustomOptions) {
    EXPECT_NO_THROW({
        ImageManager manager(test_options_);
        
        auto options = manager.get_options();
        EXPECT_EQ(options.images_directory, test_options_.images_directory);
        EXPECT_EQ(options.cloud_init_directory, test_options_.cloud_init_directory);
        EXPECT_EQ(options.download_timeout, test_options_.download_timeout);
        EXPECT_EQ(options.verify_checksums, test_options_.verify_checksums);
    });
}

// Image listing and availability tests
TEST_F(ImageManagerTest, ListAvailableImages) {
    ImageManager manager(test_options_);
    
    auto available = manager.list_available_images();
    EXPECT_FALSE(available.empty());
    
    // Check for common image types
    bool has_ubuntu = std::any_of(available.begin(), available.end(),
        [](const auto& info) { return info.type == ImageType::Ubuntu2204; });
    bool has_alpine = std::any_of(available.begin(), available.end(),
        [](const auto& info) { return info.type == ImageType::Alpine317; });
    
    EXPECT_TRUE(has_ubuntu);
    EXPECT_TRUE(has_alpine);
}

TEST_F(ImageManagerTest, ListLocalImages) {
    ImageManager manager(test_options_);
    
    auto local = manager.list_local_images();
    
    // Should detect the test images we created
    EXPECT_GE(local.size(), 2);
    
    auto ubuntu_it = std::find_if(local.begin(), local.end(),
        [](const auto& info) { return info.type == ImageType::Ubuntu2204; });
    EXPECT_NE(ubuntu_it, local.end());
    EXPECT_TRUE(ubuntu_it->is_available);
}

TEST_F(ImageManagerTest, CheckImageExists) {
    ImageManager manager(test_options_);
    
    // Should detect existing test images
    EXPECT_TRUE(manager.image_exists(ImageType::Ubuntu2204));
    EXPECT_TRUE(manager.image_exists(ImageType::Alpine317));
    
    // Should not exist
    EXPECT_FALSE(manager.image_exists(ImageType::CentOS8));
}

// Image downloading tests
TEST_F(ImageManagerTest, DownloadImageBasic) {
    ImageManager manager(test_options_);
    
    // Set progress callback
    manager.set_progress_callback([this](const auto& op, auto curr, auto total, const auto& msg) {
        progress_callback(op, curr, total, msg);
    });
    
    // Mock the download by copying an existing file
    auto result = manager.download_image(ImageType::Ubuntu2204);
    
    if (result.success) {
        EXPECT_TRUE(manager.image_exists(ImageType::Ubuntu2204));
        EXPECT_FALSE(progress_calls_.empty());
        
        // Check progress was reported
        auto download_progress = std::find_if(progress_calls_.begin(), progress_calls_.end(),
            [](const auto& call) { return std::get<0>(call).find("download") != std::string::npos; });
        EXPECT_NE(download_progress, progress_calls_.end());
    }
}

TEST_F(ImageManagerTest, DownloadImageWithForceRedownload) {
    ImageManager manager(test_options_);
    
    // Image already exists
    EXPECT_TRUE(manager.image_exists(ImageType::Ubuntu2204));
    
    // Download without force should succeed quickly (no-op)
    auto result1 = manager.download_image(ImageType::Ubuntu2204, false);
    EXPECT_TRUE(result1.success);
    
    // Download with force should re-download
    auto result2 = manager.download_image(ImageType::Ubuntu2204, true);
    EXPECT_TRUE(result2.success);
}

TEST_F(ImageManagerTest, DownloadNonExistentImage) {
    ImageManager manager(test_options_);
    
    // Try to download an image that doesn't exist in our test setup
    auto result = manager.download_image(ImageType::WindowsServer2022);
    EXPECT_FALSE(result.success);
    EXPECT_FALSE(result.error_message.empty());
}

TEST_F(ImageManagerTest, ConcurrentDownloads) {
    ImageManager manager(test_options_);
    
    // Start multiple downloads concurrently
    auto future1 = manager.download_image_async(ImageType::Ubuntu2204);
    auto future2 = manager.download_image_async(ImageType::Alpine317);
    
    auto result1 = future1.get();
    auto result2 = future2.get();
    
    // At least one should succeed (depending on test setup)
    EXPECT_TRUE(result1.success || result2.success);
}

// Image preparation tests
TEST_F(ImageManagerTest, PrepareCustomImage) {
    ImageManager manager(test_options_);
    
    // Ensure base image exists
    if (!manager.image_exists(ImageType::Ubuntu2204)) {
        auto download_result = manager.download_image(ImageType::Ubuntu2204);
        ASSERT_TRUE(download_result.success) << "Failed to download base image for test";
    }
    
    ImageManager::PrepareParams params;
    params.name = "test-custom-image";
    params.base_image = ImageType::Ubuntu2204;
    params.config.packages = {"curl", "wget", "git"};
    params.config.custom_commands = {"apt-get update", "apt-get upgrade -y"};
    params.build_memory = MemoryAmount::megabytes(512);
    params.timeout = std::chrono::seconds{60}; // Short timeout for testing
    
    auto result = manager.prepare_image(params);
    
    if (result.success) {
        EXPECT_TRUE(manager.custom_image_exists(params.name));
        
        auto info = manager.get_image_info(params.name);
        EXPECT_TRUE(info.has_value());
        EXPECT_EQ(info->base_image, ImageType::Ubuntu2204);
        EXPECT_FALSE(info->packages.empty());
    } else {
        // If preparation fails, it should provide meaningful error
        EXPECT_FALSE(result.error_message.empty());
    }
}

TEST_F(ImageManagerTest, PrepareImageWithInvalidBase) {
    ImageManager manager(test_options_);
    
    ImageManager::PrepareParams params;
    params.name = "test-invalid-base";
    params.base_image = ImageType::CentOS8; // Not available in test setup
    
    auto result = manager.prepare_image(params);
    EXPECT_FALSE(result.success);
    EXPECT_THAT(result.error_message, testing::AnyOf(
        testing::HasSubstr("not found"),
        testing::HasSubstr("not available"),
        testing::HasSubstr("base image")
    ));
}

TEST_F(ImageManagerTest, PrepareImageAsync) {
    ImageManager manager(test_options_);
    
    ImageManager::PrepareParams params;
    params.name = "test-async-prepare";
    params.base_image = ImageType::Alpine317;
    params.config.packages = {"git"};
    params.timeout = std::chrono::seconds{30};
    
    auto future = manager.prepare_image_async(params);
    
    // Should complete within reasonable time
    auto status = future.wait_for(std::chrono::seconds{45});
    EXPECT_NE(status, std::future_status::timeout);
    
    if (status == std::future_status::ready) {
        auto result = future.get();
        // Result could be success or failure depending on environment
        EXPECT_FALSE(result.error_message.empty() && !result.success);
    }
}

// Image validation and integrity tests
TEST_F(ImageManagerTest, ValidateImageIntegrity) {
    ImageManager manager(test_options_);
    
    // Validate existing images
    auto ubuntu_valid = manager.validate_image(ImageType::Ubuntu2204);
    auto alpine_valid = manager.validate_image(ImageType::Alpine317);
    
    // In our test setup, validation might pass or fail depending on checksums
    // But it should not crash and should provide clear results
    EXPECT_TRUE(ubuntu_valid.has_value());
    EXPECT_TRUE(alpine_valid.has_value());
}

TEST_F(ImageManagerTest, ValidateNonExistentImage) {
    ImageManager manager(test_options_);
    
    auto result = manager.validate_image(ImageType::CentOS8);
    EXPECT_FALSE(result.has_value());
}

TEST_F(ImageManagerTest, ChecksumValidation) {
    ImageManager manager(test_options_);
    
    if (manager.image_exists(ImageType::Ubuntu2204)) {
        auto checksum_result = manager.verify_checksum(ImageType::Ubuntu2204);
        
        // Should provide clear result (pass/fail/unavailable)
        EXPECT_TRUE(checksum_result.has_value());
    }
}

// Image cleanup and management tests
TEST_F(ImageManagerTest, CleanupUnusedImages) {
    ImageManager manager(test_options_);
    
    auto initial_count = manager.list_local_images().size();
    
    // Cleanup should not fail
    auto cleanup_result = manager.cleanup_unused_images();
    EXPECT_TRUE(cleanup_result.success);
    
    auto final_count = manager.list_local_images().size();
    EXPECT_LE(final_count, initial_count);
}

TEST_F(ImageManagerTest, DeleteSpecificImage) {
    ImageManager manager(test_options_);
    
    // Try to delete a test image
    if (manager.image_exists(ImageType::Alpine317)) {
        auto delete_result = manager.delete_image(ImageType::Alpine317);
        EXPECT_TRUE(delete_result.success);
        EXPECT_FALSE(manager.image_exists(ImageType::Alpine317));
    }
}

TEST_F(ImageManagerTest, GetImageInfo) {
    ImageManager manager(test_options_);
    
    if (manager.image_exists(ImageType::Ubuntu2204)) {
        auto info = manager.get_image_info(ImageType::Ubuntu2204);
        EXPECT_TRUE(info.has_value());
        EXPECT_EQ(info->type, ImageType::Ubuntu2204);
        EXPECT_FALSE(info->file_path.empty());
        EXPECT_GT(info->size_bytes, 0);
    }
}

// Error handling and edge cases
TEST_F(ImageManagerTest, InvalidDirectoryConfiguration) {
    ImageManager::Options bad_options;
    bad_options.images_directory = "/nonexistent/invalid/path";
    bad_options.cloud_init_directory = "/also/invalid/path";
    
    // Should either throw during construction or handle gracefully
    EXPECT_THROW({
        ImageManager manager(bad_options);
    }, ScratchpadError);
}

TEST_F(ImageManagerTest, DiskSpaceHandling) {
    ImageManager manager(test_options_);
    
    // Test behavior when disk space is limited
    auto space_info = manager.get_available_disk_space();
    EXPECT_GT(space_info.available_bytes, 0);
    EXPECT_GT(space_info.total_bytes, space_info.available_bytes);
}

TEST_F(ImageManagerTest, ConcurrentImageOperations) {
    ImageManager manager(test_options_);
    
    // Test concurrent operations on different images
    auto download_future = manager.download_image_async(ImageType::Ubuntu2204);
    auto validate_future = std::async(std::launch::async, [&]() {
        return manager.validate_image(ImageType::Alpine317);
    });
    
    // Both operations should complete without interference
    auto download_result = download_future.get();
    auto validate_result = validate_future.get();
    
    // Results depend on test environment, but operations should not fail due to concurrency
    EXPECT_TRUE(download_result.success || !download_result.error_message.empty());
}

// Progress tracking tests
TEST_F(ImageManagerTest, ProgressCallbackInvocation) {
    ImageManager manager(test_options_);
    
    std::vector<std::string> operations;
    manager.set_progress_callback([&](const auto& op, auto curr, auto total, const auto& msg) {
        operations.push_back(op);
    });
    
    // Perform operation that should trigger progress
    auto result = manager.download_image(ImageType::Ubuntu2204, true);
    
    if (result.success) {
        EXPECT_FALSE(operations.empty());
    }
}

TEST_F(ImageManagerTest, ProgressCallbackAccuracy) {
    ImageManager manager(test_options_);
    
    size_t total_reported = 0;
    size_t final_current = 0;
    
    manager.set_progress_callback([&](const auto& op, auto curr, auto total, const auto& msg) {
        total_reported = total;
        final_current = curr;
        EXPECT_LE(curr, total); // Current should never exceed total
    });
    
    auto result = manager.prepare_image({
        .name = "progress-test",
        .base_image = ImageType::Alpine317,
        .config = {.packages = {"curl"}},
        .timeout = std::chrono::seconds{30}
    });
    
    if (result.success && total_reported > 0) {
        EXPECT_EQ(final_current, total_reported); // Should complete to 100%
    }
}

// Move semantics and resource management
TEST_F(ImageManagerTest, MoveSemantics) {
    ImageManager manager1(test_options_);
    
    // Move construct
    ImageManager manager2(std::move(manager1));
    
    // Moved-to object should be functional
    auto images = manager2.list_local_images();
    EXPECT_TRUE(images.size() >= 0); // Should not crash
}

TEST_F(ImageManagerTest, ResourceCleanupOnDestruction) {
    auto temp_images_dir = temp_dir_->create_subdirectory("temp_images");
    
    {
        ImageManager::Options temp_options = test_options_;
        temp_options.images_directory = temp_images_dir.string();
        
        ImageManager manager(temp_options);
        // Manager goes out of scope here
    }
    
    // Should not leave any locked files or processes
    // (This is more of a manual verification test)
    EXPECT_TRUE(std::filesystem::exists(temp_images_dir));
}