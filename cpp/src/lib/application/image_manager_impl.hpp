#pragma once

#include "scratchpad/image_manager.hpp"
#include "logging/logger.hpp"

#include <memory>
#include <unordered_map>
#include <shared_mutex>
#include <filesystem>
#include <future>

namespace scratchpad {

class ImageManagerImpl : public ImageManager {
public:
    ImageManagerImpl();
    ~ImageManagerImpl() override = default;

    // Image Discovery and Management
    std::vector<ImageInfo> list_available_images() override;
    std::vector<ImageInfo> list_local_images() override;
    ImageInfo get_image_info(const std::string& image_name) override;
    bool is_image_available(const std::string& image_name) override;

    // Image Download and Caching
    void download_image(const std::string& image_name) override;
    std::future<void> download_image_async(const std::string& image_name) override;
    void remove_image(const std::string& image_name) override;

    // Image Creation and Preparation
    void prepare_image(const std::string& image_name, const PrepareParams& params) override;
    std::future<void> prepare_image_async(const std::string& image_name, const PrepareParams& params) override;

    // Progress Monitoring
    void set_progress_callback(ProgressCallback callback) override;
    void remove_progress_callback() override;

    // Cache Management
    DiskSize get_cache_size() override;
    void clear_cache() override;
    void set_cache_limit(DiskSize limit) override;

private:
    struct ImageMetadata {
        std::string name;
        std::string version;
        std::string architecture;
        std::string os_family;
        DiskSize size;
        std::string url;
        std::string checksum;
        std::filesystem::path local_path;
        bool is_downloaded;
        bool is_prepared;
        std::chrono::system_clock::time_point last_accessed;
        
        ImageMetadata() = default;
        ImageMetadata(const std::string& name, const std::string& url);
    };

    // Image metadata management
    void load_image_registry();
    void save_image_registry();
    void update_image_metadata(const std::string& image_name);
    ImageMetadata* find_image_metadata(const std::string& image_name);
    const ImageMetadata* find_image_metadata(const std::string& image_name) const;

    // Download operations
    void download_image_impl(const std::string& image_name);
    void download_from_url(const std::string& url, const std::filesystem::path& destination);
    bool verify_image_checksum(const std::filesystem::path& image_path, const std::string& expected_checksum);

    // Image preparation
    void prepare_image_impl(const std::string& image_name, const PrepareParams& params);
    void resize_image(const std::filesystem::path& image_path, DiskSize new_size);
    void inject_ssh_keys(const std::filesystem::path& image_path, const std::vector<std::string>& ssh_keys);
    void create_cloud_init_iso(const std::filesystem::path& iso_path, const PrepareParams& params);

    // Cache management
    void enforce_cache_limit();
    void cleanup_old_images();
    DiskSize calculate_cache_size();
    std::vector<std::string> get_cached_images_by_access_time();

    // Path management
    std::filesystem::path get_images_directory() const;
    std::filesystem::path get_image_path(const std::string& image_name) const;
    std::filesystem::path get_prepared_image_path(const std::string& image_name) const;
    std::filesystem::path get_metadata_file_path() const;

    // Validation
    void validate_image_name(const std::string& image_name);
    void validate_prepare_params(const PrepareParams& params);

    // Progress reporting
    void report_progress(const std::string& image_name, ProgressInfo info);

    // Default image registry
    void initialize_default_registry();
    void add_ubuntu_images();
    void add_debian_images();
    void add_centos_images();

    // Member variables
    mutable std::shared_mutex images_mutex_;
    std::unordered_map<std::string, ImageMetadata> image_registry_;
    
    ProgressCallback progress_callback_;
    std::mutex callback_mutex_;
    
    std::filesystem::path base_directory_;
    DiskSize cache_limit_{DiskSize::from_string("50G")};
    
    // Configuration
    std::chrono::hours image_expiry_time_{24 * 7}; // 1 week
    size_t max_concurrent_downloads_{3};
    std::atomic<size_t> active_downloads_{0};
    
    Logger& logger_;
};

} // namespace scratchpad