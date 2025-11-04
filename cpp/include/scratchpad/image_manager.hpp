#pragma once

#include "types.hpp"
#include "errors.hpp"
#include <memory>
#include <vector>
#include <functional>
#include <future>

namespace scratchpad {

// Forward declarations
class ImageDownloader;
class ImageProvisioner;
class OverlayManager;

/**
 * Interface for managing VM images - downloading, preparing, and provisioning
 * 
 * This class handles all aspects of VM image management including downloading
 * base images, preparing customized images with packages, and managing
 * overlay disks for ephemeral VMs.
 */
class ImageManager {
public:
    /**
     * Configuration options for ImageManager
     */
    struct Options {
        std::string images_directory;
        std::string cloud_init_directory;
        std::chrono::milliseconds download_timeout{300000}; // 5 minutes
        bool verify_checksums = true;
        size_t max_concurrent_downloads = 2;
    };

    /**
     * Progress callback for long-running operations
     */
    using ProgressCallback = std::function<void(const std::string& operation, 
                                                size_t current, 
                                                size_t total,
                                                const std::string& message)>;

    /**
     * Image preparation parameters
     */
    struct PrepareParams {
        std::string name;
        ImageType base_image;
        ProvisioningConfig config;
        MemoryAmount build_memory = MemoryAmount::gigabytes(1);
        std::chrono::milliseconds timeout{1800000}; // 30 minutes
        bool cleanup_on_failure = true;
        std::optional<DiskSize> disk_size;
        std::string user_data;
        std::string network_config;
    };

public:
    /**
     * Construct ImageManager with specified options
     */
    explicit ImageManager(const Options& options);
    
    /**
     * Destructor
     */
    virtual ~ImageManager();

    // Non-copyable, movable
    ImageManager(const ImageManager&) = delete;
    ImageManager& operator=(const ImageManager&) = delete;
    ImageManager(ImageManager&&) noexcept;
    ImageManager& operator=(ImageManager&&) noexcept;

    // ========== Image Information ==========
    
    /**
     * List all available images
     * @return Vector of available image information
     */
    virtual std::vector<ImageInfo> list_available_images() = 0;
    
    /**
     * List locally cached images
     * @return Vector of local image information
     */
    virtual std::vector<ImageInfo> list_local_images() = 0;
    
    /**
     * Get detailed information about a specific image
     * @param image_name Name of the image
     * @return Image information
     * @throws ImageError if image not found
     */
    virtual ImageInfo get_image_info(const std::string& image_name) = 0;
    
    /**
     * Check if an image is available for download
     * @param image_name Name of the image
     * @return true if available
     */
    virtual bool is_image_available(const std::string& image_name) = 0;

    // ========== Base Image Management ==========

    /**
     * Download a base image if not already present
     * @param type Image type to download
     * @param force_redownload Force redownload even if present
     * @throws ImageError if download fails
     */
    virtual void download_base_image(ImageType type, bool force_redownload = false);

    /**
     * Download base image asynchronously
     * @param type Image type to download
     * @param force_redownload Force redownload even if present
     * @param progress_callback Optional progress callback
     * @return Future that completes when download is finished
     */
    std::future<void> download_base_image_async(
        ImageType type, 
        bool force_redownload = false,
        ProgressCallback progress_callback = {}
    );

    /**
     * Check if base image is available locally
     * @param type Image type to check
     * @return true if image is available
     */
    bool is_base_image_available(ImageType type) const;

    /**
     * List all available base images
     * @return Vector of base image information
     */
    std::vector<BaseImage> list_base_images() const;

    /**
     * Get information about a specific base image
     * @param type Image type
     * @return Base image information
     * @throws ImageError if image type is not supported
     */
    BaseImage get_base_image_info(ImageType type) const;

    /**
     * Remove base image from local storage
     * @param type Image type to remove
     * @throws ImageError if image is in use
     */
    void remove_base_image(ImageType type);

    // ========== Prepared Image Management ==========

    /**
     * Prepare a customized VM image
     * @param params Preparation parameters
     * @return Prepared image identifier
     * @throws ImageError if preparation fails
     */
    PreparedImageId prepare_image(const PrepareParams& params);

    /**
     * Prepare image asynchronously
     * @param params Preparation parameters
     * @param progress_callback Optional progress callback
     * @return Future containing prepared image ID
     */
    std::future<PreparedImageId> prepare_image_async(
        const PrepareParams& params,
        ProgressCallback progress_callback = {}
    );

    /**
     * Check if prepared image exists
     * @param image_id Prepared image identifier
     * @return true if image exists
     */
    bool is_prepared_image_available(const PreparedImageId& image_id) const;

    /**
     * List all prepared images
     * @return Vector of prepared image information
     */
    std::vector<PreparedImage> list_prepared_images() const;

    /**
     * Get information about a prepared image
     * @param image_id Prepared image identifier
     * @return Prepared image information
     * @throws ImageError if image doesn't exist
     */
    PreparedImage get_prepared_image_info(const PreparedImageId& image_id) const;

    /**
     * Remove prepared image
     * @param image_id Prepared image identifier
     * @throws ImageError if image is in use
     */
    void remove_prepared_image(const PreparedImageId& image_id);

    /**
     * Update prepared image (re-run provisioning)
     * @param image_id Existing prepared image identifier
     * @param config New provisioning configuration
     * @param progress_callback Optional progress callback
     * @return Updated prepared image ID (may be different)
     */
    PreparedImageId update_prepared_image(
        const PreparedImageId& image_id,
        const ProvisioningConfig& config,
        ProgressCallback progress_callback = {}
    );

    // ========== Overlay Disk Management ==========

    /**
     * Create overlay disk for ephemeral VM
     * @param base_image_id Base image to create overlay from
     * @param overlay_name Name for the overlay
     * @return Path to created overlay disk
     * @throws ImageError if base image doesn't exist
     */
    std::string create_overlay_disk(const PreparedImageId& base_image_id, const std::string& overlay_name);

    /**
     * Remove overlay disk
     * @param overlay_path Path to overlay disk
     */
    void remove_overlay_disk(const std::string& overlay_path);

    /**
     * List all overlay disks
     * @return Vector of overlay disk information
     */
    std::vector<OverlayDisk> list_overlay_disks() const;

    /**
     * Clean up orphaned overlay disks
     * @return Number of overlay disks removed
     */
    size_t cleanup_orphaned_overlays();

    // ========== Image Information and Utilities ==========

    /**
     * Get disk usage information for all images
     * @return Total disk space used by images
     */
    DiskSize get_total_image_size() const;

    /**
     * Get available image types
     * @return Vector of supported image types
     */
    std::vector<ImageType> get_supported_image_types() const;

    /**
     * Validate image integrity
     * @param image_id Image identifier (base or prepared)
     * @return true if image is valid
     */
    bool validate_image_integrity(const std::string& image_id) const;

    /**
     * Get recommended packages for image type
     * @param type Image type
     * @return Vector of recommended packages
     */
    PackageList get_recommended_packages(ImageType type) const;

    /**
     * Test image by starting temporary VM
     * @param image_id Image identifier to test
     * @param test_commands Commands to run for testing
     * @return true if all tests pass
     */
    bool test_image(const PreparedImageId& image_id, const std::vector<std::string>& test_commands = {});

    // ========== Cloud-init Management ==========

    /**
     * Generate cloud-init configuration
     * @param config Provisioning configuration
     * @param ssh_public_key SSH public key to inject
     * @return Path to generated cloud-init ISO
     */
    std::string generate_cloud_init(const ProvisioningConfig& config, const std::string& ssh_public_key);

    /**
     * Validate cloud-init configuration
     * @param config Provisioning configuration
     * @return true if configuration is valid
     */
    bool validate_cloud_init_config(const ProvisioningConfig& config) const;

    // ========== Cleanup and Maintenance ==========

    /**
     * Clean up temporary files and caches
     * @param aggressive Perform aggressive cleanup (remove cached downloads)
     * @return Amount of space freed
     */
    DiskSize cleanup_temporary_files(bool aggressive = false);

    /**
     * Compact image files to save space
     * @param image_id Optional specific image to compact
     * @return Amount of space saved
     */
    DiskSize compact_images(const std::optional<PreparedImageId>& image_id = {});

    /**
     * Export prepared image to file
     * @param image_id Prepared image identifier
     * @param export_path Path to export file
     * @param compress Compress exported image
     */
    void export_image(const PreparedImageId& image_id, const std::string& export_path, bool compress = true);

    /**
     * Import prepared image from file
     * @param import_path Path to import file
     * @param image_name Name for imported image
     * @return Imported image identifier
     */
    PreparedImageId import_image(const std::string& import_path, const std::string& image_name);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace scratchpad