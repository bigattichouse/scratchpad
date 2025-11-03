/**
 * Async Operations Example
 * 
 * This example demonstrates asynchronous operations, progress monitoring,
 * and concurrent VM management using the Scratchpad C++ API.
 */

#include <scratchpad/vm_manager.hpp>
#include <scratchpad/image_manager.hpp>
#include <scratchpad/resource_manager.hpp>
#include <service_factory.hpp>

#include <iostream>
#include <vector>
#include <future>
#include <thread>
#include <chrono>
#include <atomic>

using namespace scratchpad;

class ProgressMonitor {
public:
    void on_vm_status_change(const VMId& vm_id, VMStatus old_status, VMStatus new_status) {
        std::cout << "[VM " << vm_id.value().substr(0, 8) << "...] "
                  << "Status: " << static_cast<int>(old_status) 
                  << " -> " << static_cast<int>(new_status) << std::endl;
    }
    
    void on_image_progress(const std::string& image_name, const ProgressInfo& info) {
        if (info.percentage >= 0) {
            std::cout << "[IMG " << image_name << "] " 
                      << info.percentage << "% - " << info.message << std::endl;
        } else {
            std::cout << "[IMG " << image_name << "] " << info.message << std::endl;
        }
    }
    
    void on_resource_event(const ResourceEvent& event) {
        std::cout << "[RESOURCE] " << static_cast<int>(event.type) 
                  << " - " << event.message << std::endl;
    }
};

int main() {
    try {
        std::cout << "=== Scratchpad Async Operations Example ===" << std::endl;
        
        // Create services and progress monitor
        auto services = ServiceFactory::create_service_bundle();
        auto& vm_manager = *services.vm_manager;
        auto& image_manager = *services.image_manager;
        auto& resource_manager = *services.resource_manager;
        
        ProgressMonitor monitor;
        
        // Set up progress callbacks
        vm_manager.set_status_callback(
            [&monitor](const VMId& vm_id, VMStatus old_status, VMStatus new_status) {
                monitor.on_vm_status_change(vm_id, old_status, new_status);
            });
        
        image_manager.set_progress_callback(
            [&monitor](const std::string& image_name, const ProgressInfo& info) {
                monitor.on_image_progress(image_name, info);
            });
        
        resource_manager.set_resource_callback(
            [&monitor](const ResourceEvent& event) {
                monitor.on_resource_event(event);
            });
        
        std::cout << "✓ Progress monitoring configured" << std::endl;
        
        // Download multiple images asynchronously
        std::cout << "\n1. Starting async image downloads..." << std::endl;
        
        std::vector<std::string> images_to_download = {"ubuntu-22.04", "debian-12"};
        std::vector<std::future<void>> download_futures;
        
        for (const auto& image_name : images_to_download) {
            if (image_manager.is_image_available(image_name)) {
                auto image_info = image_manager.get_image_info(image_name);
                if (!image_info.is_downloaded) {
                    std::cout << "Starting download of " << image_name << std::endl;
                    download_futures.push_back(
                        image_manager.download_image_async(image_name)
                    );
                } else {
                    std::cout << "Image " << image_name << " already downloaded" << std::endl;
                }
            }
        }
        
        // Wait for all downloads to complete
        std::cout << "Waiting for downloads to complete..." << std::endl;
        for (auto& future : download_futures) {
            future.wait();
        }
        std::cout << "✓ All downloads completed" << std::endl;
        
        // Create multiple VMs concurrently
        std::cout << "\n2. Creating multiple VMs..." << std::endl;
        
        std::vector<VMId> vm_ids;
        const int num_vms = 3;
        
        for (int i = 0; i < num_vms; ++i) {
            CreateParams params;
            params.image_name = "ubuntu-22.04";
            params.memory = MemoryAmount::from_string("512M");
            params.disk_size = DiskSize::from_string("3G");
            params.cpu_cores = 1;
            
            VMId vm_id = vm_manager.create_vm(params);
            vm_ids.push_back(vm_id);
            
            std::cout << "Created VM " << (i + 1) << ": " 
                      << vm_id.value().substr(0, 8) << "..." << std::endl;
        }
        
        // Start all VMs concurrently
        std::cout << "\n3. Starting VMs concurrently..." << std::endl;
        
        std::vector<std::thread> start_threads;
        std::atomic<int> vms_started{0};
        
        for (const auto& vm_id : vm_ids) {
            start_threads.emplace_back([&vm_manager, &vms_started, vm_id]() {
                try {
                    vm_manager.start_vm(vm_id);
                    ++vms_started;
                } catch (const std::exception& e) {
                    std::cerr << "Failed to start VM " << vm_id.value() 
                              << ": " << e.what() << std::endl;
                }
            });
        }
        
        // Wait for all VMs to start
        for (auto& thread : start_threads) {
            thread.join();
        }
        
        std::cout << "✓ " << vms_started.load() << " VMs started successfully" << std::endl;
        
        // Wait for VMs to be ready
        std::cout << "Waiting for VMs to be ready..." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(5));
        
        // Execute commands asynchronously on all VMs
        std::cout << "\n4. Executing commands on all VMs..." << std::endl;
        
        std::vector<std::future<CommandResult>> command_futures;
        
        for (size_t i = 0; i < vm_ids.size(); ++i) {
            ExecuteParams params;
            params.command = "echo 'Hello from VM " + std::to_string(i + 1) + "' && "
                           "hostname && "
                           "uptime";
            params.timeout = std::chrono::seconds(30);
            
            command_futures.push_back(
                vm_manager.execute_command_async(vm_ids[i], params)
            );
        }
        
        // Collect results
        for (size_t i = 0; i < command_futures.size(); ++i) {
            auto result = command_futures[i].get();
            std::cout << "VM " << (i + 1) << " output:\n" << result.stdout_output << std::endl;
        }
        
        // Show resource usage while VMs are running
        std::cout << "\n5. Resource usage with " << vms_started.load() << " VMs:" << std::endl;
        auto usage = resource_manager.get_current_usage();
        std::cout << "  Running VMs: " << usage.running_vms << std::endl;
        std::cout << "  Allocated memory: " << usage.allocated_memory.to_string() << std::endl;
        std::cout << "  Allocated CPUs: " << usage.allocated_cpu_cores << std::endl;
        std::cout << "  Allocated ports: " << usage.allocated_ports << std::endl;
        
        // Test concurrent file operations
        std::cout << "\n6. Testing concurrent file operations..." << std::endl;
        
        std::vector<std::thread> file_threads;
        
        for (size_t i = 0; i < vm_ids.size(); ++i) {
            file_threads.emplace_back([&vm_manager, &vm_ids, i]() {
                try {
                    // Create a test file locally
                    std::string local_file = "/tmp/test_file_" + std::to_string(i) + ".txt";
                    std::string content = "Test content for VM " + std::to_string(i + 1);
                    
                    std::ofstream file(local_file);
                    file << content << std::endl;
                    file.close();
                    
                    // Copy to VM
                    CopyParams copy_params;
                    copy_params.source = local_file;
                    copy_params.destination = "/tmp/received_file.txt";
                    
                    vm_manager.copy_file_to_vm(vm_ids[i], copy_params);
                    
                    // Copy back from VM
                    copy_params.source = "/tmp/received_file.txt";
                    copy_params.destination = local_file + ".back";
                    
                    vm_manager.copy_file_from_vm(vm_ids[i], copy_params);
                    
                    std::cout << "✓ File operations completed for VM " << (i + 1) << std::endl;
                    
                    // Cleanup
                    std::remove(local_file.c_str());
                    std::remove((local_file + ".back").c_str());
                    
                } catch (const std::exception& e) {
                    std::cerr << "File operation failed for VM " << (i + 1) 
                              << ": " << e.what() << std::endl;
                }
            });
        }
        
        // Wait for file operations to complete
        for (auto& thread : file_threads) {
            thread.join();
        }
        
        // Show final status of all VMs
        std::cout << "\n7. Final VM status:" << std::endl;
        auto all_vms = vm_manager.list_vm_info();
        
        for (const auto& vm_info : all_vms) {
            std::cout << "VM " << vm_info.vm_id.value().substr(0, 8) << "... "
                      << "Status: " << static_cast<int>(vm_info.status) << ", "
                      << "Commands: " << vm_info.statistics.commands_executed << ", "
                      << "Files: " << vm_info.statistics.files_transferred << std::endl;
        }
        
        // Cleanup all VMs concurrently
        std::cout << "\n8. Cleaning up all VMs..." << std::endl;
        
        std::vector<std::thread> cleanup_threads;
        
        for (const auto& vm_id : vm_ids) {
            cleanup_threads.emplace_back([&vm_manager, vm_id]() {
                try {
                    vm_manager.stop_vm(vm_id);
                    vm_manager.destroy_vm(vm_id);
                } catch (const std::exception& e) {
                    std::cerr << "Cleanup failed for VM " << vm_id.value() 
                              << ": " << e.what() << std::endl;
                }
            });
        }
        
        // Wait for cleanup to complete
        for (auto& thread : cleanup_threads) {
            thread.join();
        }
        
        std::cout << "✓ All VMs cleaned up" << std::endl;
        
        // Final resource check
        std::cout << "\n9. Final resource usage:" << std::endl;
        usage = resource_manager.get_current_usage();
        std::cout << "  Running VMs: " << usage.running_vms << std::endl;
        std::cout << "  Allocated memory: " << usage.allocated_memory.to_string() << std::endl;
        std::cout << "  Allocated CPUs: " << usage.allocated_cpu_cores << std::endl;
        
        std::cout << "\n=== Async operations example completed successfully! ===" << std::endl;
        
        return 0;
        
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
}