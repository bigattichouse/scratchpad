#include "image_manager_impl.hpp"
#include "scratchpad/errors.hpp"

#include <fstream>
#include <regex>
#include <iomanip>
#include <sstream>
#include <curl/curl.h>
#include <openssl/sha.h>

namespace scratchpad {

ImageManagerImpl::ImageManagerImpl() 
    : logger_(Logger::instance()) {
    
    logger_.info("Initializing Image Manager");
    
    // Set up base directory
    const char* home = std::getenv("HOME");
    std::string home_dir = home ? home : "/tmp";
    base_directory_ = std::filesystem::path(home_dir) / ".scratchpad" / "images";
    
    // Create directories if they don't exist
    std::filesystem::create_directories(base_directory_);
    std::filesystem::create_directories(get_images_directory());
    
    // Load existing image registry
    load_image_registry();
    
    // Initialize default registry if empty
    if (image_registry_.empty()) {
        initialize_default_registry();
        save_image_registry();
    }
    
    logger_.info("Image Manager initialized with {} images", image_registry_.size());
}

std::vector<ImageInfo> ImageManagerImpl::list_available_images() {
    std::shared_lock lock(images_mutex_);
    
    std::vector<ImageInfo> images;
    images.reserve(image_registry_.size());
    
    for (const auto& [name, metadata] : image_registry_) {
        ImageInfo info;
        info.name = metadata.name;
        info.version = metadata.version;
        info.architecture = metadata.architecture;
        info.os_family = metadata.os_family;
        info.size = metadata.size;
        info.is_downloaded = metadata.is_downloaded;
        info.is_prepared = metadata.is_prepared;
        info.local_path = metadata.local_path;
        
        images.push_back(info);
    }
    
    return images;
}

std::vector<ImageInfo> ImageManagerImpl::list_local_images() {
    std::shared_lock lock(images_mutex_);
    
    std::vector<ImageInfo> images;
    
    for (const auto& [name, metadata] : image_registry_) {
        if (metadata.is_downloaded) {
            ImageInfo info;
            info.name = metadata.name;
            info.version = metadata.version;
            info.architecture = metadata.architecture;
            info.os_family = metadata.os_family;
            info.size = metadata.size;
            info.is_downloaded = metadata.is_downloaded;
            info.is_prepared = metadata.is_prepared;
            info.local_path = metadata.local_path;
            
            images.push_back(info);
        }
    }
    
    return images;
}

ImageInfo ImageManagerImpl::get_image_info(const std::string& image_name) {
    validate_image_name(image_name);
    
    std::shared_lock lock(images_mutex_);
    
    const ImageMetadata* metadata = find_image_metadata(image_name);
    if (!metadata) {
        THROW_IMAGE_ERROR(ErrorCode::ImageNotFound, 
                         "Image not found: " + image_name, image_name);
    }
    
    ImageInfo info;
    info.name = metadata->name;
    info.version = metadata->version;
    info.architecture = metadata->architecture;
    info.os_family = metadata->os_family;
    info.size = metadata->size;
    info.is_downloaded = metadata->is_downloaded;
    info.is_prepared = metadata->is_prepared;
    info.local_path = metadata->local_path;
    
    return info;
}

bool ImageManagerImpl::is_image_available(const std::string& image_name) {
    validate_image_name(image_name);
    
    std::shared_lock lock(images_mutex_);
    return find_image_metadata(image_name) != nullptr;
}

void ImageManagerImpl::download_image(const std::string& image_name) {
    logger_.info("Downloading image: {}", image_name);
    download_image_impl(image_name);
}

std::future<void> ImageManagerImpl::download_image_async(const std::string& image_name) {
    return std::async(std::launch::async, [this, image_name]() {
        download_image_impl(image_name);
    });
}

void ImageManagerImpl::remove_image(const std::string& image_name) {
    logger_.info("Removing image: {}", image_name);
    
    validate_image_name(image_name);
    
    std::unique_lock lock(images_mutex_);
    
    ImageMetadata* metadata = find_image_metadata(image_name);
    if (!metadata) {
        THROW_IMAGE_ERROR(ErrorCode::ImageNotFound,
                         "Image not found: " + image_name, image_name);
    }
    
    // Remove local files
    if (metadata->is_downloaded && std::filesystem::exists(metadata->local_path)) {
        std::filesystem::remove(metadata->local_path);
    }
    
    // Remove prepared image
    auto prepared_path = get_prepared_image_path(image_name);
    if (std::filesystem::exists(prepared_path)) {
        std::filesystem::remove(prepared_path);
    }
    
    // Update metadata
    metadata->is_downloaded = false;
    metadata->is_prepared = false;
    metadata->local_path.clear();
    
    save_image_registry();
    
    logger_.info("Image removed: {}", image_name);
}

void ImageManagerImpl::prepare_image(const std::string& image_name, const PrepareParams& params) {
    logger_.info("Preparing image: {}", image_name);
    prepare_image_impl(image_name, params);
}

std::future<void> ImageManagerImpl::prepare_image_async(const std::string& image_name, const PrepareParams& params) {
    return std::async(std::launch::async, [this, image_name, params]() {
        prepare_image_impl(image_name, params);
    });
}

void ImageManagerImpl::set_progress_callback(ProgressCallback callback) {
    std::lock_guard lock(callback_mutex_);
    progress_callback_ = std::move(callback);
}

void ImageManagerImpl::remove_progress_callback() {
    std::lock_guard lock(callback_mutex_);
    progress_callback_ = nullptr;
}

DiskSize ImageManagerImpl::get_cache_size() {
    return calculate_cache_size();
}

void ImageManagerImpl::clear_cache() {
    logger_.info("Clearing image cache");
    
    std::unique_lock lock(images_mutex_);
    
    for (auto& [name, metadata] : image_registry_) {
        if (metadata.is_downloaded && std::filesystem::exists(metadata.local_path)) {
            std::filesystem::remove(metadata.local_path);
        }
        
        auto prepared_path = get_prepared_image_path(name);
        if (std::filesystem::exists(prepared_path)) {
            std::filesystem::remove(prepared_path);
        }
        
        metadata.is_downloaded = false;
        metadata.is_prepared = false;
        metadata.local_path.clear();
    }
    
    save_image_registry();
    
    logger_.info("Image cache cleared");
}

void ImageManagerImpl::set_cache_limit(DiskSize limit) {
    cache_limit_ = limit;
    enforce_cache_limit();
}

// Private implementation methods

ImageManagerImpl::ImageMetadata::ImageMetadata(const std::string& name, const std::string& url)
    : name(name), url(url), is_downloaded(false), is_prepared(false) {
    last_accessed = std::chrono::system_clock::now();
}

void ImageManagerImpl::load_image_registry() {
    auto metadata_file = get_metadata_file_path();
    if (!std::filesystem::exists(metadata_file)) {
        return;
    }
    
    std::ifstream file(metadata_file);
    if (!file.is_open()) {
        logger_.warning("Failed to open image registry file: {}", metadata_file.string());
        return;
    }
    
    // Simple JSON-like parsing (in a real implementation, use a proper JSON library)
    // For now, implement a basic format
    std::string line;
    while (std::getline(file, line)) {
        // Parse line-by-line metadata
        // This is a simplified implementation
    }
}

void ImageManagerImpl::save_image_registry() {
    auto metadata_file = get_metadata_file_path();
    
    std::ofstream file(metadata_file);
    if (!file.is_open()) {
        logger_.error("Failed to save image registry to: {}", metadata_file.string());
        return;
    }
    
    // Save metadata in a simple format
    for (const auto& [name, metadata] : image_registry_) {
        file << name << "|" << metadata.version << "|" << metadata.architecture << "|"
             << metadata.os_family << "|" << metadata.size.bytes << "|" << metadata.url << "|"
             << metadata.checksum << "|" << metadata.local_path.string() << "|"
             << (metadata.is_downloaded ? "1" : "0") << "|" 
             << (metadata.is_prepared ? "1" : "0") << "\n";
    }
}

ImageManagerImpl::ImageMetadata* ImageManagerImpl::find_image_metadata(const std::string& image_name) {
    auto it = image_registry_.find(image_name);
    return (it != image_registry_.end()) ? &it->second : nullptr;
}

const ImageManagerImpl::ImageMetadata* ImageManagerImpl::find_image_metadata(const std::string& image_name) const {
    auto it = image_registry_.find(image_name);
    return (it != image_registry_.end()) ? &it->second : nullptr;
}

void ImageManagerImpl::download_image_impl(const std::string& image_name) {
    validate_image_name(image_name);
    
    // Check download limit
    if (active_downloads_ >= max_concurrent_downloads_) {
        THROW_IMAGE_ERROR(ErrorCode::InternalError,
                         "Maximum concurrent downloads reached", image_name);
    }
    
    std::unique_lock lock(images_mutex_);
    
    ImageMetadata* metadata = find_image_metadata(image_name);
    if (!metadata) {
        THROW_IMAGE_ERROR(ErrorCode::ImageNotFound,
                         "Image not found: " + image_name, image_name);
    }
    
    if (metadata->is_downloaded) {
        logger_.info("Image already downloaded: {}", image_name);
        return;
    }
    
    lock.unlock();
    
    ++active_downloads_;
    
    try {
        auto destination = get_image_path(image_name);
        
        report_progress(image_name, ProgressInfo{0, "Starting download..."});
        
        download_from_url(metadata->url, destination);
        
        report_progress(image_name, ProgressInfo{50, "Verifying checksum..."});
        
        if (!metadata->checksum.empty() && !verify_image_checksum(destination, metadata->checksum)) {
            std::filesystem::remove(destination);
            THROW_IMAGE_ERROR(ErrorCode::ImageCorrupted,
                             "Checksum verification failed for " + image_name, image_name);
        }
        
        report_progress(image_name, ProgressInfo{100, "Download complete"});
        
        // Update metadata
        lock.lock();
        metadata->is_downloaded = true;
        metadata->local_path = destination;
        metadata->last_accessed = std::chrono::system_clock::now();
        save_image_registry();
        
        logger_.info("Image downloaded successfully: {}", image_name);
        
        // Enforce cache limit after successful download
        lock.unlock();
        enforce_cache_limit();
        
    } catch (const std::exception& e) {
        logger_.error("Failed to download image {}: {}", image_name, e.what());
        report_progress(image_name, ProgressInfo{-1, "Download failed: " + std::string(e.what())});
        throw;
    }
    
    --active_downloads_;
}

void ImageManagerImpl::download_from_url(const std::string& url, const std::filesystem::path& destination) {
    // Simple implementation using libcurl
    CURL* curl = curl_easy_init();
    if (!curl) {
        throw std::runtime_error("Failed to initialize curl");
    }
    
    FILE* file = fopen(destination.c_str(), "wb");
    if (!file) {
        curl_easy_cleanup(curl);
        throw std::runtime_error("Failed to create destination file");
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 3600L); // 1 hour timeout
    
    CURLcode res = curl_easy_perform(curl);
    
    fclose(file);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::filesystem::remove(destination);
        throw std::runtime_error("Download failed: " + std::string(curl_easy_strerror(res)));
    }
}

bool ImageManagerImpl::verify_image_checksum(const std::filesystem::path& image_path, const std::string& expected_checksum) {
    // Simple SHA256 implementation
    std::ifstream file(image_path, std::ios::binary);
    if (!file.is_open()) {
        return false;
    }
    
    SHA256_CTX sha256;
    SHA256_Init(&sha256);
    
    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        SHA256_Update(&sha256, buffer, file.gcount());
    }
    
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_Final(hash, &sha256);
    
    std::stringstream ss;
    for (int i = 0; i < SHA256_DIGEST_LENGTH; ++i) {
        ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(hash[i]);
    }
    
    return ss.str() == expected_checksum;
}

void ImageManagerImpl::prepare_image_impl(const std::string& image_name, const PrepareParams& params) {
    validate_image_name(image_name);
    validate_prepare_params(params);
    
    std::unique_lock lock(images_mutex_);
    
    ImageMetadata* metadata = find_image_metadata(image_name);
    if (!metadata) {
        THROW_IMAGE_ERROR(ErrorCode::ImageNotFound,
                         "Image not found: " + image_name, image_name);
    }
    
    if (!metadata->is_downloaded) {
        THROW_IMAGE_ERROR(ErrorCode::ImageNotFound,
                         "Image not downloaded: " + image_name, image_name);
    }
    
    lock.unlock();
    
    try {
        report_progress(image_name, ProgressInfo{0, "Starting image preparation..."});
        
        auto prepared_path = get_prepared_image_path(image_name);
        
        // Copy base image to prepared location
        std::filesystem::copy_file(metadata->local_path, prepared_path, 
                                 std::filesystem::copy_options::overwrite_existing);
        
        report_progress(image_name, ProgressInfo{25, "Resizing image..."});
        
        // Resize if requested
        if (params.disk_size.has_value()) {
            resize_image(prepared_path, params.disk_size.value());
        }
        
        report_progress(image_name, ProgressInfo{50, "Injecting SSH keys..."});
        
        // Inject SSH keys
        if (!params.ssh_public_keys.empty()) {
            inject_ssh_keys(prepared_path, params.ssh_public_keys);
        }
        
        report_progress(image_name, ProgressInfo{75, "Creating cloud-init configuration..."});
        
        // Create cloud-init ISO if needed
        if (!params.user_data.empty() || !params.network_config.empty()) {
            auto iso_path = prepared_path.parent_path() / (image_name + "-cloudinit.iso");
            create_cloud_init_iso(iso_path, params);
        }
        
        report_progress(image_name, ProgressInfo{100, "Image preparation complete"});
        
        // Update metadata
        lock.lock();
        metadata->is_prepared = true;
        metadata->last_accessed = std::chrono::system_clock::now();
        save_image_registry();
        
        logger_.info("Image prepared successfully: {}", image_name);
        
    } catch (const std::exception& e) {
        logger_.error("Failed to prepare image {}: {}", image_name, e.what());
        report_progress(image_name, ProgressInfo{-1, "Preparation failed: " + std::string(e.what())});
        throw;
    }
}

void ImageManagerImpl::resize_image(const std::filesystem::path& image_path, DiskSize new_size) {
    // Use qemu-img to resize the image
    std::string command = "qemu-img resize \"" + image_path.string() + "\" " + new_size.to_string();
    
    int result = std::system(command.c_str());
    if (result != 0) {
        throw std::runtime_error("Failed to resize image");
    }
}

void ImageManagerImpl::inject_ssh_keys(const std::filesystem::path& image_path, const std::vector<std::string>& ssh_keys) {
    // This would use tools like virt-customize or guestfs to inject SSH keys
    // For now, implement a simplified version
    
    std::string keys_content;
    for (const auto& key : ssh_keys) {
        keys_content += key + "\n";
    }
    
    // Write authorized_keys file and inject it into the image
    auto temp_keys_file = std::filesystem::temp_directory_path() / "authorized_keys";
    std::ofstream keys_file(temp_keys_file);
    keys_file << keys_content;
    keys_file.close();
    
    std::string command = "virt-copy-in -a \"" + image_path.string() + 
                         "\" \"" + temp_keys_file.string() + "\" /home/scratchpad/.ssh/";
    
    int result = std::system(command.c_str());
    std::filesystem::remove(temp_keys_file);
    
    if (result != 0) {
        throw std::runtime_error("Failed to inject SSH keys");
    }
}

void ImageManagerImpl::create_cloud_init_iso(const std::filesystem::path& iso_path, const PrepareParams& params) {
    // Create cloud-init ISO with user-data and meta-data
    auto temp_dir = std::filesystem::temp_directory_path() / "cloudinit";
    std::filesystem::create_directories(temp_dir);
    
    // Write user-data
    if (!params.user_data.empty()) {
        std::ofstream user_data_file(temp_dir / "user-data");
        user_data_file << params.user_data;
    }
    
    // Write network-config
    if (!params.network_config.empty()) {
        std::ofstream network_file(temp_dir / "network-config");
        network_file << params.network_config;
    }
    
    // Write meta-data (minimal)
    std::ofstream meta_data_file(temp_dir / "meta-data");
    meta_data_file << "instance-id: scratchpad-vm\n";
    meta_data_file << "local-hostname: scratchpad\n";
    
    // Create ISO
    std::string command = "genisoimage -output \"" + iso_path.string() + 
                         "\" -volid cidata -joliet -rock \"" + temp_dir.string() + "\"";
    
    int result = std::system(command.c_str());
    
    // Cleanup
    std::filesystem::remove_all(temp_dir);
    
    if (result != 0) {
        throw std::runtime_error("Failed to create cloud-init ISO");
    }
}

void ImageManagerImpl::enforce_cache_limit() {
    DiskSize current_size = calculate_cache_size();
    
    if (current_size.bytes <= cache_limit_.bytes) {
        return;
    }
    
    logger_.info("Cache size ({}) exceeds limit ({}), cleaning up old images", 
                current_size.to_string(), cache_limit_.to_string());
    
    auto images_by_access = get_cached_images_by_access_time();
    
    std::unique_lock lock(images_mutex_);
    
    for (const auto& image_name : images_by_access) {
        ImageMetadata* metadata = find_image_metadata(image_name);
        if (metadata && metadata->is_downloaded) {
            if (std::filesystem::exists(metadata->local_path)) {
                std::filesystem::remove(metadata->local_path);
            }
            
            metadata->is_downloaded = false;
            metadata->is_prepared = false;
            metadata->local_path.clear();
            
            current_size = calculate_cache_size();
            if (current_size.bytes <= cache_limit_.bytes) {
                break;
            }
        }
    }
    
    save_image_registry();
}

DiskSize ImageManagerImpl::calculate_cache_size() {
    std::shared_lock lock(images_mutex_);
    
    uint64_t total_size = 0;
    
    for (const auto& [name, metadata] : image_registry_) {
        if (metadata.is_downloaded && std::filesystem::exists(metadata.local_path)) {
            total_size += std::filesystem::file_size(metadata.local_path);
        }
        
        auto prepared_path = get_prepared_image_path(name);
        if (std::filesystem::exists(prepared_path)) {
            total_size += std::filesystem::file_size(prepared_path);
        }
    }
    
    return DiskSize{total_size};
}

std::vector<std::string> ImageManagerImpl::get_cached_images_by_access_time() {
    std::shared_lock lock(images_mutex_);
    
    std::vector<std::pair<std::string, std::chrono::system_clock::time_point>> images;
    
    for (const auto& [name, metadata] : image_registry_) {
        if (metadata.is_downloaded) {
            images.emplace_back(name, metadata.last_accessed);
        }
    }
    
    // Sort by access time (oldest first)
    std::sort(images.begin(), images.end(), 
              [](const auto& a, const auto& b) {
                  return a.second < b.second;
              });
    
    std::vector<std::string> result;
    result.reserve(images.size());
    
    for (const auto& [name, time] : images) {
        result.push_back(name);
    }
    
    return result;
}

std::filesystem::path ImageManagerImpl::get_images_directory() const {
    return base_directory_ / "cache";
}

std::filesystem::path ImageManagerImpl::get_image_path(const std::string& image_name) const {
    return get_images_directory() / (image_name + ".qcow2");
}

std::filesystem::path ImageManagerImpl::get_prepared_image_path(const std::string& image_name) const {
    return get_images_directory() / (image_name + "-prepared.qcow2");
}

std::filesystem::path ImageManagerImpl::get_metadata_file_path() const {
    return base_directory_ / "registry.txt";
}

void ImageManagerImpl::validate_image_name(const std::string& image_name) {
    if (image_name.empty()) {
        THROW_IMAGE_ERROR(ErrorCode::InvalidArgument, "Image name cannot be empty");
    }
    
    // Check for valid characters
    std::regex valid_name(R"(^[a-zA-Z0-9][a-zA-Z0-9._-]*$)");
    if (!std::regex_match(image_name, valid_name)) {
        THROW_IMAGE_ERROR(ErrorCode::InvalidArgument, 
                         "Invalid image name format: " + image_name);
    }
}

void ImageManagerImpl::validate_prepare_params(const PrepareParams& params) {
    if (params.disk_size.has_value() && params.disk_size.value().bytes < DiskSize::from_string("1G").bytes) {
        THROW_IMAGE_ERROR(ErrorCode::InvalidArgument, 
                         "Minimum disk size for prepared image is 1GB");
    }
}

void ImageManagerImpl::report_progress(const std::string& image_name, ProgressInfo info) {
    std::lock_guard lock(callback_mutex_);
    if (progress_callback_) {
        try {
            progress_callback_(image_name, info);
        } catch (const std::exception& e) {
            logger_.error("Error in progress callback: {}", e.what());
        }
    }
}

void ImageManagerImpl::initialize_default_registry() {
    logger_.info("Initializing default image registry");
    
    add_ubuntu_images();
    add_debian_images();
    add_centos_images();
}

void ImageManagerImpl::add_ubuntu_images() {
    // Add Ubuntu 22.04 LTS
    ImageMetadata ubuntu2204("ubuntu-22.04", "https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img");
    ubuntu2204.version = "22.04";
    ubuntu2204.architecture = "amd64";
    ubuntu2204.os_family = "ubuntu";
    ubuntu2204.size = DiskSize::from_string("2.5G");
    ubuntu2204.checksum = ""; // Would be populated with actual checksum
    
    image_registry_["ubuntu-22.04"] = ubuntu2204;
    
    // Add Ubuntu 20.04 LTS
    ImageMetadata ubuntu2004("ubuntu-20.04", "https://cloud-images.ubuntu.com/focal/current/focal-server-cloudimg-amd64.img");
    ubuntu2004.version = "20.04";
    ubuntu2004.architecture = "amd64";
    ubuntu2004.os_family = "ubuntu";
    ubuntu2004.size = DiskSize::from_string("2.3G");
    ubuntu2004.checksum = "";
    
    image_registry_["ubuntu-20.04"] = ubuntu2004;
}

void ImageManagerImpl::add_debian_images() {
    // Add Debian 12 (Bookworm)
    ImageMetadata debian12("debian-12", "https://cloud.debian.org/images/cloud/bookworm/latest/debian-12-generic-amd64.qcow2");
    debian12.version = "12";
    debian12.architecture = "amd64";
    debian12.os_family = "debian";
    debian12.size = DiskSize::from_string("2G");
    debian12.checksum = "";
    
    image_registry_["debian-12"] = debian12;
}

void ImageManagerImpl::add_centos_images() {
    // Add CentOS Stream 9
    ImageMetadata centos9("centos-stream-9", "https://cloud.centos.org/centos/9-stream/x86_64/images/CentOS-Stream-GenericCloud-9-latest.x86_64.qcow2");
    centos9.version = "9-stream";
    centos9.architecture = "amd64";
    centos9.os_family = "centos";
    centos9.size = DiskSize::from_string("1.5G");
    centos9.checksum = "";
    
    image_registry_["centos-stream-9"] = centos9;
}

} // namespace scratchpad