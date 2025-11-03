/**
 * Basic Scratchpad C++ API Usage Example
 * 
 * This example demonstrates the fundamental operations of the Scratchpad
 * VM management library: creating services, managing VMs, and executing commands.
 */

#include <scratchpad/vm_manager.hpp>
#include <scratchpad/image_manager.hpp>
#include <scratchpad/resource_manager.hpp>
#include <service_factory.hpp>

#include <iostream>
#include <thread>
#include <chrono>

using namespace scratchpad;

int main() {
    try {
        std::cout << "=== Scratchpad C++ API Basic Usage Example ===" << std::endl;
        
        // Create service bundle with all managers
        std::cout << "\n1. Creating service managers..." << std::endl;
        auto services = ServiceFactory::create_service_bundle();
        
        auto& vm_manager = *services.vm_manager;
        auto& image_manager = *services.image_manager;
        auto& resource_manager = *services.resource_manager;
        
        std::cout << "✓ Services created successfully" << std::endl;
        
        // Check available images
        std::cout << "\n2. Checking available images..." << std::endl;
        auto available_images = image_manager.list_available_images();
        
        std::cout << "Available images:" << std::endl;
        for (const auto& image : available_images) {
            std::cout << "  - " << image.name << " (" << image.version << ")" << std::endl;
        }
        
        // Use Ubuntu 22.04 for this example
        std::string image_name = "ubuntu-22.04";
        
        // Check if image is downloaded, download if needed
        std::cout << "\n3. Preparing image: " << image_name << std::endl;
        if (!image_manager.is_image_available(image_name)) {
            std::cout << "✗ Image not available in registry" << std::endl;
            return 1;
        }
        
        auto image_info = image_manager.get_image_info(image_name);
        if (!image_info.is_downloaded) {
            std::cout << "Downloading image..." << std::endl;
            image_manager.download_image(image_name);
            std::cout << "✓ Image downloaded" << std::endl;
        } else {
            std::cout << "✓ Image already downloaded" << std::endl;
        }
        
        // Show system resources
        std::cout << "\n4. System resources:" << std::endl;
        auto system_resources = resource_manager.get_system_resources();
        std::cout << "  Memory: " << system_resources.total_memory.to_string() << std::endl;
        std::cout << "  CPUs: " << system_resources.total_cpu_cores << std::endl;
        std::cout << "  Disk: " << system_resources.total_disk.to_string() << std::endl;
        
        // Create VM
        std::cout << "\n5. Creating VM..." << std::endl;
        CreateParams create_params;
        create_params.image_name = image_name;
        create_params.memory = MemoryAmount::from_string("1G");
        create_params.disk_size = DiskSize::from_string("5G");
        create_params.cpu_cores = 1;
        
        VMId vm_id = vm_manager.create_vm(create_params);
        std::cout << "✓ VM created with ID: " << vm_id.value() << std::endl;
        
        // Start VM
        std::cout << "\n6. Starting VM..." << std::endl;
        vm_manager.start_vm(vm_id);
        std::cout << "✓ VM started successfully" << std::endl;
        
        // Wait a moment for VM to fully boot
        std::cout << "Waiting for VM to be ready..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2));
        
        // Execute some commands
        std::cout << "\n7. Executing commands in VM..." << std::endl;
        
        // Check OS version
        {
            ExecuteParams exec_params;
            exec_params.command = "cat /etc/os-release | head -1";
            exec_params.timeout = std::chrono::seconds(10);
            
            auto result = vm_manager.execute_command(vm_id, exec_params);
            std::cout << "OS Version: " << result.stdout_output;
        }
        
        // Check available memory
        {
            ExecuteParams exec_params;
            exec_params.command = "free -h | grep Mem:";
            exec_params.timeout = std::chrono::seconds(10);
            
            auto result = vm_manager.execute_command(vm_id, exec_params);
            std::cout << "Memory info: " << result.stdout_output;
        }
        
        // Install a package and verify
        {
            std::cout << "Installing and testing Python..." << std::endl;
            
            ExecuteParams exec_params;
            exec_params.command = "sudo apt-get update && sudo apt-get install -y python3";
            exec_params.timeout = std::chrono::seconds(60);
            
            auto result = vm_manager.execute_command(vm_id, exec_params);
            if (result.exit_code == 0) {
                std::cout << "✓ Python installed successfully" << std::endl;
                
                // Test Python
                exec_params.command = "python3 -c 'print(\"Hello from Python in VM!\")'";
                exec_params.timeout = std::chrono::seconds(10);
                
                result = vm_manager.execute_command(vm_id, exec_params);
                std::cout << "Python output: " << result.stdout_output;
            } else {
                std::cout << "✗ Failed to install Python" << std::endl;
            }
        }
        
        // Show VM status
        std::cout << "\n8. VM Status:" << std::endl;
        auto vm_info = vm_manager.get_vm_info(vm_id);
        std::cout << "  Status: " << static_cast<int>(vm_info.status) << std::endl;
        std::cout << "  Memory: " << vm_info.configuration.memory.to_string() << std::endl;
        std::cout << "  CPUs: " << vm_info.configuration.cpu_cores << std::endl;
        std::cout << "  SSH Port: " << vm_info.configuration.ssh_port << std::endl;
        std::cout << "  Commands executed: " << vm_info.statistics.commands_executed << std::endl;
        
        // Show resource usage
        std::cout << "\n9. Current resource usage:" << std::endl;
        auto usage = resource_manager.get_current_usage();
        std::cout << "  Running VMs: " << usage.running_vms << std::endl;
        std::cout << "  Allocated memory: " << usage.allocated_memory.to_string() << std::endl;
        std::cout << "  Allocated CPUs: " << usage.allocated_cpu_cores << std::endl;
        
        // Stop and destroy VM
        std::cout << "\n10. Cleaning up..." << std::endl;
        vm_manager.stop_vm(vm_id);
        std::cout << "✓ VM stopped" << std::endl;
        
        vm_manager.destroy_vm(vm_id);
        std::cout << "✓ VM destroyed" << std::endl;
        
        std::cout << "\n=== Example completed successfully! ===" << std::endl;
        
        return 0;
        
    } catch (const ScratchpadError& e) {
        std::cerr << "Scratchpad error: " << e.what() << std::endl;
        std::cerr << "Error code: " << static_cast<int>(e.code()) << std::endl;
        return 1;
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}