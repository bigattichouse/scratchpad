#pragma once

#include "scratchpad/vm_manager.hpp"
#include "scratchpad/image_manager.hpp"
#include "scratchpad/resource_manager.hpp"
#include "logging/logger.hpp"

#include <memory>
#include <string>
#include <vector>
#include <map>

namespace scratchpad::cli {

/**
 * Command-line interface for Scratchpad VM management
 * 
 * This class provides the main CLI interface for managing VMs,
 * implementing the same functionality as the original Node.js version.
 */
class ScratchpadCLI {
public:
    ScratchpadCLI();
    ~ScratchpadCLI() = default;

    /**
     * Main entry point for CLI execution
     * @param argc Argument count
     * @param argv Argument vector
     * @return Exit code
     */
    int run(int argc, char* argv[]);

private:
    struct Command {
        std::string name;
        std::string description;
        std::function<int(const std::vector<std::string>&)> handler;
        std::vector<std::string> usage;
    };

    // Command handlers
    int handle_create(const std::vector<std::string>& args);
    int handle_start(const std::vector<std::string>& args);
    int handle_stop(const std::vector<std::string>& args);
    int handle_restart(const std::vector<std::string>& args);
    int handle_destroy(const std::vector<std::string>& args);
    int handle_list(const std::vector<std::string>& args);
    int handle_status(const std::vector<std::string>& args);
    int handle_execute(const std::vector<std::string>& args);
    int handle_copy_to(const std::vector<std::string>& args);
    int handle_copy_from(const std::vector<std::string>& args);
    int handle_images(const std::vector<std::string>& args);
    int handle_download(const std::vector<std::string>& args);
    int handle_prepare(const std::vector<std::string>& args);
    int handle_resources(const std::vector<std::string>& args);
    int handle_help(const std::vector<std::string>& args);
    int handle_version(const std::vector<std::string>& args);

    // Utility methods
    void register_commands();
    void register_signal_handlers();
    void print_usage();
    void print_command_help(const std::string& command_name);
    void print_vm_info(const VMInfo& info);
    void print_vm_list(const std::vector<VMInfo>& vms);
    void print_image_info(const ImageInfo& info);
    void print_image_list(const std::vector<ImageInfo>& images);
    void print_resource_usage(const ResourceUsage& usage);
    
    VMId resolve_vm_id(const std::string& input);
    bool validate_vm_id_format(const std::string& vm_id);
    std::string format_duration(std::chrono::milliseconds duration);
    std::string format_timestamp(std::chrono::system_clock::time_point timestamp);

    // Option parsing
    struct ParsedOptions {
        std::map<std::string, std::string> options;
        std::vector<std::string> positional;
    };
    
    ParsedOptions parse_options(const std::vector<std::string>& args);
    bool has_option(const ParsedOptions& opts, const std::string& name);
    std::string get_option(const ParsedOptions& opts, const std::string& name, const std::string& default_value = "");

    // Progress callbacks
    void setup_progress_callbacks();
    void on_vm_status_change(const VMId& vm_id, VMStatus old_status, VMStatus new_status);
    void on_image_progress(const std::string& image_name, const ProgressInfo& info);
    void on_resource_event(const ResourceEvent& event);

    // Member variables
    std::unique_ptr<VMManager> vm_manager_;
    std::unique_ptr<ImageManager> image_manager_;
    std::unique_ptr<ResourceManager> resource_manager_;
    
    std::map<std::string, Command> commands_;
    bool verbose_mode_{false};
    bool quiet_mode_{false};
    
    Logger& logger_;
};

} // namespace scratchpad::cli