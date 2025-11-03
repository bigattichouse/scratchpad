#include "scratchpad_cli.hpp"
#include "service_factory.hpp"
#include "scratchpad/errors.hpp"

#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <signal.h>
#include <regex>

namespace scratchpad::cli {

namespace {
    // Global CLI instance for signal handling
    ScratchpadCLI* g_cli_instance = nullptr;
    
    void signal_handler(int signal) {
        std::cout << "\nReceived signal " << signal << ", shutting down...\n";
        exit(signal);
    }
}

ScratchpadCLI::ScratchpadCLI() 
    : logger_(Logger::instance()) {
    
    // Create services
    auto service_bundle = ServiceFactory::create_service_bundle();
    vm_manager_ = std::move(service_bundle.vm_manager);
    image_manager_ = std::move(service_bundle.image_manager);
    resource_manager_ = std::move(service_bundle.resource_manager);
    
    // Register commands
    register_commands();
    
    // Set up callbacks
    setup_progress_callbacks();
    
    // Set global instance for signal handling
    g_cli_instance = this;
    register_signal_handlers();
}

int ScratchpadCLI::run(int argc, char* argv[]) {
    try {
        if (argc < 2) {
            print_usage();
            return 1;
        }
        
        // Parse global options
        std::vector<std::string> args;
        for (int i = 1; i < argc; ++i) {
            std::string arg = argv[i];
            if (arg == "-v" || arg == "--verbose") {
                verbose_mode_ = true;
                logger_.set_level(LogLevel::Debug);
            } else if (arg == "-q" || arg == "--quiet") {
                quiet_mode_ = true;
                logger_.set_level(LogLevel::Error);
            } else {
                args.push_back(arg);
            }
        }
        
        if (args.empty()) {
            print_usage();
            return 1;
        }
        
        std::string command = args[0];
        std::vector<std::string> command_args(args.begin() + 1, args.end());
        
        auto cmd_it = commands_.find(command);
        if (cmd_it == commands_.end()) {
            std::cerr << "Unknown command: " << command << std::endl;
            print_usage();
            return 1;
        }
        
        return cmd_it->second.handler(command_args);
        
    } catch (const ScratchpadError& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        if (verbose_mode_) {
            std::cerr << "Error code: " << static_cast<int>(e.code()) << std::endl;
            std::cerr << "Category: " << get_error_category(e.code()) << std::endl;
        }
        return static_cast<int>(e.code());
    } catch (const std::exception& e) {
        std::cerr << "Unexpected error: " << e.what() << std::endl;
        return 1;
    }
}

int ScratchpadCLI::handle_create(const std::vector<std::string>& args) {
    auto opts = parse_options(args);
    
    if (opts.positional.empty()) {
        std::cerr << "Usage: scratchpad create <image-name> [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  --memory <size>     Memory allocation (default: 2G)" << std::endl;
        std::cerr << "  --disk <size>       Disk size (default: 10G)" << std::endl;
        std::cerr << "  --cpus <count>      CPU cores (default: 2)" << std::endl;
        return 1;
    }
    
    std::string image_name = opts.positional[0];
    
    // Check if image is available
    if (!image_manager_->is_image_available(image_name)) {
        std::cerr << "Image not available: " << image_name << std::endl;
        std::cerr << "Use 'scratchpad images' to list available images" << std::endl;
        return 1;
    }
    
    // Parse options
    CreateParams params;
    params.image_name = image_name;
    params.memory = MemoryAmount::from_string(get_option(opts, "memory", "2G"));
    params.disk_size = DiskSize::from_string(get_option(opts, "disk", "10G"));
    params.cpu_cores = static_cast<uint32_t>(std::stoi(get_option(opts, "cpus", "2")));
    
    if (!quiet_mode_) {
        std::cout << "Creating VM with image: " << image_name << std::endl;
        std::cout << "Memory: " << params.memory.to_string() << std::endl;
        std::cout << "Disk: " << params.disk_size.to_string() << std::endl;
        std::cout << "CPUs: " << params.cpu_cores << std::endl;
    }
    
    VMId vm_id = vm_manager_->create_vm(params);
    
    std::cout << vm_id.value() << std::endl;
    
    if (!quiet_mode_) {
        std::cout << "VM created successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_start(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad start <vm-id>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    if (!quiet_mode_) {
        std::cout << "Starting VM: " << vm_id.value() << std::endl;
    }
    
    vm_manager_->start_vm(vm_id);
    
    if (!quiet_mode_) {
        std::cout << "VM started successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_stop(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad stop <vm-id>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    if (!quiet_mode_) {
        std::cout << "Stopping VM: " << vm_id.value() << std::endl;
    }
    
    vm_manager_->stop_vm(vm_id);
    
    if (!quiet_mode_) {
        std::cout << "VM stopped successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_restart(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad restart <vm-id>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    if (!quiet_mode_) {
        std::cout << "Restarting VM: " << vm_id.value() << std::endl;
    }
    
    vm_manager_->restart_vm(vm_id);
    
    if (!quiet_mode_) {
        std::cout << "VM restarted successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_destroy(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad destroy <vm-id>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    if (!quiet_mode_) {
        std::cout << "Destroying VM: " << vm_id.value() << std::endl;
    }
    
    vm_manager_->destroy_vm(vm_id);
    
    if (!quiet_mode_) {
        std::cout << "VM destroyed successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_list(const std::vector<std::string>& args) {
    auto vms = vm_manager_->list_vm_info();
    
    if (vms.empty()) {
        if (!quiet_mode_) {
            std::cout << "No VMs found" << std::endl;
        }
        return 0;
    }
    
    print_vm_list(vms);
    return 0;
}

int ScratchpadCLI::handle_status(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad status <vm-id>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    VMInfo info = vm_manager_->get_vm_info(vm_id);
    
    print_vm_info(info);
    return 0;
}

int ScratchpadCLI::handle_execute(const std::vector<std::string>& args) {
    if (args.size() < 2) {
        std::cerr << "Usage: scratchpad execute <vm-id> <command>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    // Join remaining args as command
    std::string command;
    for (size_t i = 1; i < args.size(); ++i) {
        if (i > 1) command += " ";
        command += args[i];
    }
    
    ExecuteParams params;
    params.command = command;
    params.capture_output = true;
    params.timeout = std::chrono::milliseconds(30000); // 30 seconds
    
    CommandResult result = vm_manager_->execute_command(vm_id, params);
    
    if (!result.stdout_output.empty()) {
        std::cout << result.stdout_output;
    }
    
    if (!result.stderr_output.empty()) {
        std::cerr << result.stderr_output;
    }
    
    return result.exit_code;
}

int ScratchpadCLI::handle_copy_to(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Usage: scratchpad copy-to <vm-id> <local-path> <remote-path>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    CopyParams params;
    params.source = args[1];
    params.destination = args[2];
    
    if (!quiet_mode_) {
        std::cout << "Copying " << params.source << " to VM:" << params.destination << std::endl;
    }
    
    vm_manager_->copy_file_to_vm(vm_id, params);
    
    if (!quiet_mode_) {
        std::cout << "File copied successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_copy_from(const std::vector<std::string>& args) {
    if (args.size() < 3) {
        std::cerr << "Usage: scratchpad copy-from <vm-id> <remote-path> <local-path>" << std::endl;
        return 1;
    }
    
    VMId vm_id = resolve_vm_id(args[0]);
    
    CopyParams params;
    params.source = args[1];
    params.destination = args[2];
    
    if (!quiet_mode_) {
        std::cout << "Copying VM:" << params.source << " to " << params.destination << std::endl;
    }
    
    vm_manager_->copy_file_from_vm(vm_id, params);
    
    if (!quiet_mode_) {
        std::cout << "File copied successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_images(const std::vector<std::string>& args) {
    auto opts = parse_options(args);
    
    std::vector<ImageInfo> images;
    if (has_option(opts, "local")) {
        images = image_manager_->list_local_images();
    } else {
        images = image_manager_->list_available_images();
    }
    
    if (images.empty()) {
        if (!quiet_mode_) {
            std::cout << "No images found" << std::endl;
        }
        return 0;
    }
    
    print_image_list(images);
    return 0;
}

int ScratchpadCLI::handle_download(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad download <image-name>" << std::endl;
        return 1;
    }
    
    std::string image_name = args[0];
    
    if (!image_manager_->is_image_available(image_name)) {
        std::cerr << "Image not available: " << image_name << std::endl;
        return 1;
    }
    
    if (!quiet_mode_) {
        std::cout << "Downloading image: " << image_name << std::endl;
    }
    
    image_manager_->download_image(image_name);
    
    if (!quiet_mode_) {
        std::cout << "Image downloaded successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_prepare(const std::vector<std::string>& args) {
    if (args.empty()) {
        std::cerr << "Usage: scratchpad prepare <image-name> [options]" << std::endl;
        std::cerr << "Options:" << std::endl;
        std::cerr << "  --disk-size <size>  Resize disk to specified size" << std::endl;
        return 1;
    }
    
    std::string image_name = args[0];
    auto opts = parse_options(std::vector<std::string>(args.begin() + 1, args.end()));
    
    PrepareParams params;
    if (has_option(opts, "disk-size")) {
        params.disk_size = DiskSize::from_string(get_option(opts, "disk-size"));
    }
    
    if (!quiet_mode_) {
        std::cout << "Preparing image: " << image_name << std::endl;
    }
    
    image_manager_->prepare_image(image_name, params);
    
    if (!quiet_mode_) {
        std::cout << "Image prepared successfully" << std::endl;
    }
    
    return 0;
}

int ScratchpadCLI::handle_resources(const std::vector<std::string>& args) {
    auto usage = resource_manager_->get_current_usage();
    auto limits = resource_manager_->get_resource_limits();
    auto system = resource_manager_->get_system_resources();
    
    print_resource_usage(usage);
    
    std::cout << "\nResource Limits:" << std::endl;
    std::cout << "  Memory: " << limits.max_memory.to_string() << std::endl;
    std::cout << "  Disk: " << limits.max_disk.to_string() << std::endl;
    std::cout << "  CPUs: " << limits.max_cpu_cores << std::endl;
    std::cout << "  Max VMs: " << limits.max_vms << std::endl;
    
    std::cout << "\nSystem Resources:" << std::endl;
    std::cout << "  Total Memory: " << system.total_memory.to_string() << std::endl;
    std::cout << "  Total Disk: " << system.total_disk.to_string() << std::endl;
    std::cout << "  Total CPUs: " << system.total_cpu_cores << std::endl;
    
    return 0;
}

int ScratchpadCLI::handle_help(const std::vector<std::string>& args) {
    if (!args.empty()) {
        print_command_help(args[0]);
    } else {
        print_usage();
    }
    return 0;
}

int ScratchpadCLI::handle_version(const std::vector<std::string>& args) {
    std::cout << "Scratchpad C++ v1.0.0" << std::endl;
    std::cout << "Built with DDD architecture" << std::endl;
    return 0;
}

void ScratchpadCLI::register_commands() {
    commands_["create"] = {
        "create",
        "Create a new VM from an image",
        [this](const auto& args) { return handle_create(args); },
        {"scratchpad create <image-name> [--memory <size>] [--disk <size>] [--cpus <count>]"}
    };
    
    commands_["start"] = {
        "start",
        "Start a VM",
        [this](const auto& args) { return handle_start(args); },
        {"scratchpad start <vm-id>"}
    };
    
    commands_["stop"] = {
        "stop",
        "Stop a VM",
        [this](const auto& args) { return handle_stop(args); },
        {"scratchpad stop <vm-id>"}
    };
    
    commands_["restart"] = {
        "restart",
        "Restart a VM",
        [this](const auto& args) { return handle_restart(args); },
        {"scratchpad restart <vm-id>"}
    };
    
    commands_["destroy"] = {
        "destroy",
        "Destroy a VM",
        [this](const auto& args) { return handle_destroy(args); },
        {"scratchpad destroy <vm-id>"}
    };
    
    commands_["list"] = {
        "list",
        "List all VMs",
        [this](const auto& args) { return handle_list(args); },
        {"scratchpad list"}
    };
    
    commands_["status"] = {
        "status",
        "Show VM status and information",
        [this](const auto& args) { return handle_status(args); },
        {"scratchpad status <vm-id>"}
    };
    
    commands_["execute"] = {
        "execute",
        "Execute a command in a VM",
        [this](const auto& args) { return handle_execute(args); },
        {"scratchpad execute <vm-id> <command>"}
    };
    
    commands_["copy-to"] = {
        "copy-to",
        "Copy a file to a VM",
        [this](const auto& args) { return handle_copy_to(args); },
        {"scratchpad copy-to <vm-id> <local-path> <remote-path>"}
    };
    
    commands_["copy-from"] = {
        "copy-from",
        "Copy a file from a VM",
        [this](const auto& args) { return handle_copy_from(args); },
        {"scratchpad copy-from <vm-id> <remote-path> <local-path>"}
    };
    
    commands_["images"] = {
        "images",
        "List available images",
        [this](const auto& args) { return handle_images(args); },
        {"scratchpad images [--local]"}
    };
    
    commands_["download"] = {
        "download",
        "Download an image",
        [this](const auto& args) { return handle_download(args); },
        {"scratchpad download <image-name>"}
    };
    
    commands_["prepare"] = {
        "prepare",
        "Prepare an image for use",
        [this](const auto& args) { return handle_prepare(args); },
        {"scratchpad prepare <image-name> [--disk-size <size>]"}
    };
    
    commands_["resources"] = {
        "resources",
        "Show resource usage",
        [this](const auto& args) { return handle_resources(args); },
        {"scratchpad resources"}
    };
    
    commands_["help"] = {
        "help",
        "Show help information",
        [this](const auto& args) { return handle_help(args); },
        {"scratchpad help [command]"}
    };
    
    commands_["version"] = {
        "version",
        "Show version information",
        [this](const auto& args) { return handle_version(args); },
        {"scratchpad version"}
    };
}

void ScratchpadCLI::register_signal_handlers() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
}

void ScratchpadCLI::print_usage() {
    std::cout << "Scratchpad VM Management Tool\n" << std::endl;
    std::cout << "Usage: scratchpad [global-options] <command> [command-options]\n" << std::endl;
    
    std::cout << "Global Options:" << std::endl;
    std::cout << "  -v, --verbose   Enable verbose output" << std::endl;
    std::cout << "  -q, --quiet     Suppress non-essential output" << std::endl;
    std::cout << std::endl;
    
    std::cout << "Commands:" << std::endl;
    for (const auto& [name, cmd] : commands_) {
        std::cout << "  " << std::left << std::setw(12) << name << cmd.description << std::endl;
    }
    
    std::cout << "\nUse 'scratchpad help <command>' for detailed command help." << std::endl;
}

void ScratchpadCLI::print_command_help(const std::string& command_name) {
    auto it = commands_.find(command_name);
    if (it == commands_.end()) {
        std::cerr << "Unknown command: " << command_name << std::endl;
        return;
    }
    
    const Command& cmd = it->second;
    std::cout << cmd.description << "\n" << std::endl;
    
    std::cout << "Usage:" << std::endl;
    for (const auto& usage : cmd.usage) {
        std::cout << "  " << usage << std::endl;
    }
}

void ScratchpadCLI::print_vm_info(const VMInfo& info) {
    std::cout << "VM ID: " << info.vm_id.value() << std::endl;
    std::cout << "Status: " << static_cast<int>(info.status) << std::endl;
    std::cout << "Image: " << info.configuration.image_name << std::endl;
    std::cout << "Memory: " << info.configuration.memory.to_string() << std::endl;
    std::cout << "Disk: " << info.configuration.disk_size.to_string() << std::endl;
    std::cout << "CPUs: " << info.configuration.cpu_cores << std::endl;
    std::cout << "SSH Port: " << info.configuration.ssh_port << std::endl;
    std::cout << "Created: " << format_timestamp(info.created_at) << std::endl;
    
    if (info.started_at.has_value()) {
        std::cout << "Started: " << format_timestamp(info.started_at.value()) << std::endl;
    }
    
    std::cout << "Statistics:" << std::endl;
    std::cout << "  Commands executed: " << info.statistics.commands_executed << std::endl;
    std::cout << "  Files transferred: " << info.statistics.files_transferred << std::endl;
    std::cout << "  Uptime: " << format_duration(info.statistics.uptime) << std::endl;
}

void ScratchpadCLI::print_vm_list(const std::vector<VMInfo>& vms) {
    std::cout << std::left << std::setw(36) << "VM ID" 
              << std::setw(12) << "STATUS"
              << std::setw(20) << "IMAGE"
              << std::setw(10) << "MEMORY"
              << std::setw(8) << "CPUS"
              << "SSH PORT" << std::endl;
    
    std::cout << std::string(90, '-') << std::endl;
    
    for (const auto& info : vms) {
        std::cout << std::left << std::setw(36) << info.vm_id.value()
                  << std::setw(12) << static_cast<int>(info.status)
                  << std::setw(20) << info.configuration.image_name
                  << std::setw(10) << info.configuration.memory.to_string()
                  << std::setw(8) << info.configuration.cpu_cores
                  << info.configuration.ssh_port << std::endl;
    }
}

void ScratchpadCLI::print_image_info(const ImageInfo& info) {
    std::cout << "Name: " << info.name << std::endl;
    std::cout << "Version: " << info.version << std::endl;
    std::cout << "Architecture: " << info.architecture << std::endl;
    std::cout << "OS Family: " << info.os_family << std::endl;
    std::cout << "Size: " << info.size.to_string() << std::endl;
    std::cout << "Downloaded: " << (info.is_downloaded ? "Yes" : "No") << std::endl;
    std::cout << "Prepared: " << (info.is_prepared ? "Yes" : "No") << std::endl;
    
    if (!info.local_path.empty()) {
        std::cout << "Local Path: " << info.local_path << std::endl;
    }
}

void ScratchpadCLI::print_image_list(const std::vector<ImageInfo>& images) {
    std::cout << std::left << std::setw(20) << "NAME"
              << std::setw(12) << "VERSION"
              << std::setw(12) << "ARCH"
              << std::setw(10) << "SIZE"
              << std::setw(12) << "DOWNLOADED"
              << "PREPARED" << std::endl;
    
    std::cout << std::string(78, '-') << std::endl;
    
    for (const auto& info : images) {
        std::cout << std::left << std::setw(20) << info.name
                  << std::setw(12) << info.version
                  << std::setw(12) << info.architecture
                  << std::setw(10) << info.size.to_string()
                  << std::setw(12) << (info.is_downloaded ? "Yes" : "No")
                  << (info.is_prepared ? "Yes" : "No") << std::endl;
    }
}

void ScratchpadCLI::print_resource_usage(const ResourceUsage& usage) {
    std::cout << "Resource Usage:" << std::endl;
    std::cout << "  Running VMs: " << usage.running_vms << std::endl;
    std::cout << "  Total VMs: " << usage.total_vms << std::endl;
    std::cout << "  Allocated Memory: " << usage.allocated_memory.to_string() << std::endl;
    std::cout << "  Allocated Disk: " << usage.allocated_disk.to_string() << std::endl;
    std::cout << "  Allocated CPUs: " << usage.allocated_cpu_cores << std::endl;
    std::cout << "  Allocated Ports: " << usage.allocated_ports << std::endl;
}

VMId ScratchpadCLI::resolve_vm_id(const std::string& input) {
    if (validate_vm_id_format(input)) {
        return VMId{input};
    }
    
    // Try to find VM by partial ID match
    auto vms = vm_manager_->list_vms();
    std::vector<VMId> matches;
    
    for (const auto& vm_id : vms) {
        if (vm_id.value().starts_with(input)) {
            matches.push_back(vm_id);
        }
    }
    
    if (matches.empty()) {
        throw std::invalid_argument("No VM found matching: " + input);
    }
    
    if (matches.size() > 1) {
        throw std::invalid_argument("Ambiguous VM ID: " + input + " matches multiple VMs");
    }
    
    return matches[0];
}

bool ScratchpadCLI::validate_vm_id_format(const std::string& vm_id) {
    // UUID format validation
    std::regex uuid_regex(R"([0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12})");
    return std::regex_match(vm_id, uuid_regex);
}

std::string ScratchpadCLI::format_duration(std::chrono::milliseconds duration) {
    auto seconds = std::chrono::duration_cast<std::chrono::seconds>(duration);
    auto minutes = std::chrono::duration_cast<std::chrono::minutes>(seconds);
    auto hours = std::chrono::duration_cast<std::chrono::hours>(minutes);
    
    if (hours.count() > 0) {
        return std::to_string(hours.count()) + "h " + 
               std::to_string((minutes % std::chrono::hours(1)).count()) + "m";
    } else if (minutes.count() > 0) {
        return std::to_string(minutes.count()) + "m " + 
               std::to_string((seconds % std::chrono::minutes(1)).count()) + "s";
    } else {
        return std::to_string(seconds.count()) + "s";
    }
}

std::string ScratchpadCLI::format_timestamp(std::chrono::system_clock::time_point timestamp) {
    auto time_t = std::chrono::system_clock::to_time_t(timestamp);
    std::stringstream ss;
    ss << std::put_time(std::localtime(&time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

ScratchpadCLI::ParsedOptions ScratchpadCLI::parse_options(const std::vector<std::string>& args) {
    ParsedOptions result;
    
    for (size_t i = 0; i < args.size(); ++i) {
        const std::string& arg = args[i];
        
        if (arg.starts_with("--")) {
            std::string option = arg.substr(2);
            
            // Check if option has value
            if (i + 1 < args.size() && !args[i + 1].starts_with("-")) {
                result.options[option] = args[i + 1];
                ++i; // Skip value
            } else {
                result.options[option] = "true";
            }
        } else if (arg.starts_with("-") && arg.length() > 1) {
            // Short option
            std::string option = arg.substr(1);
            if (i + 1 < args.size() && !args[i + 1].starts_with("-")) {
                result.options[option] = args[i + 1];
                ++i; // Skip value
            } else {
                result.options[option] = "true";
            }
        } else {
            result.positional.push_back(arg);
        }
    }
    
    return result;
}

bool ScratchpadCLI::has_option(const ParsedOptions& opts, const std::string& name) {
    return opts.options.find(name) != opts.options.end();
}

std::string ScratchpadCLI::get_option(const ParsedOptions& opts, const std::string& name, const std::string& default_value) {
    auto it = opts.options.find(name);
    return (it != opts.options.end()) ? it->second : default_value;
}

void ScratchpadCLI::setup_progress_callbacks() {
    vm_manager_->set_status_callback(
        [this](const VMId& vm_id, VMStatus old_status, VMStatus new_status) {
            on_vm_status_change(vm_id, old_status, new_status);
        });
    
    image_manager_->set_progress_callback(
        [this](const std::string& image_name, const ProgressInfo& info) {
            on_image_progress(image_name, info);
        });
    
    resource_manager_->set_resource_callback(
        [this](const ResourceEvent& event) {
            on_resource_event(event);
        });
}

void ScratchpadCLI::on_vm_status_change(const VMId& vm_id, VMStatus old_status, VMStatus new_status) {
    if (verbose_mode_ && !quiet_mode_) {
        std::cout << "VM " << vm_id.value() << " status changed: " 
                  << static_cast<int>(old_status) << " -> " << static_cast<int>(new_status) << std::endl;
    }
}

void ScratchpadCLI::on_image_progress(const std::string& image_name, const ProgressInfo& info) {
    if (!quiet_mode_) {
        if (info.percentage >= 0) {
            std::cout << "\rDownloading " << image_name << ": " << info.percentage << "% - " << info.message << std::flush;
            if (info.percentage >= 100) {
                std::cout << std::endl;
            }
        } else {
            std::cout << "\r" << image_name << ": " << info.message << std::endl;
        }
    }
}

void ScratchpadCLI::on_resource_event(const ResourceEvent& event) {
    if (verbose_mode_ && !quiet_mode_) {
        std::cout << "Resource event: " << static_cast<int>(event.type) << " - " << event.message << std::endl;
    }
}

} // namespace scratchpad::cli