#pragma once

#include "scratchpad/vm_manager.hpp"
#include "scratchpad/image_manager.hpp"
#include "scratchpad/resource_manager.hpp"

#include <memory>

namespace scratchpad {

/**
 * Factory class for creating service instances
 * 
 * This factory provides a centralized way to create and configure
 * all application services with their dependencies properly injected.
 */
class ServiceFactory {
public:
    /**
     * Create a VM Manager instance
     * @return Unique pointer to VM Manager implementation
     */
    static std::unique_ptr<VMManager> create_vm_manager();
    
    /**
     * Create an Image Manager instance
     * @return Unique pointer to Image Manager implementation
     */
    static std::unique_ptr<ImageManager> create_image_manager();
    
    /**
     * Create a Resource Manager instance
     * @return Unique pointer to Resource Manager implementation
     */
    static std::unique_ptr<ResourceManager> create_resource_manager();
    
    /**
     * Create all services and return them as a bundle
     * This ensures proper initialization order and dependency injection
     */
    struct ServiceBundle {
        std::unique_ptr<ResourceManager> resource_manager;
        std::unique_ptr<ImageManager> image_manager;
        std::unique_ptr<VMManager> vm_manager;
    };
    
    static ServiceBundle create_service_bundle();
    
private:
    ServiceFactory() = default;
};

} // namespace scratchpad