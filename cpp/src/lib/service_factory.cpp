#include "service_factory.hpp"
#include "application/vm_manager_impl.hpp"
#include "application/image_manager_impl.hpp"
#include "application/resource_manager_impl.hpp"
#include "logging/logger.hpp"

namespace scratchpad {

std::unique_ptr<VMManager> ServiceFactory::create_vm_manager() {
    return std::make_unique<VMManagerImpl>();
}

std::unique_ptr<ImageManager> ServiceFactory::create_image_manager() {
    return std::make_unique<ImageManagerImpl>();
}

std::unique_ptr<ResourceManager> ServiceFactory::create_resource_manager() {
    return std::make_unique<ResourceManagerImpl>();
}

ServiceFactory::ServiceBundle ServiceFactory::create_service_bundle() {
    Logger& logger = Logger::instance();
    logger.info("Creating service bundle");
    
    ServiceBundle bundle;
    
    // Create services in dependency order
    // Resource Manager has no dependencies
    bundle.resource_manager = create_resource_manager();
    
    // Image Manager has no dependencies on other services
    bundle.image_manager = create_image_manager();
    
    // VM Manager depends on Resource Manager (though not directly injected in current implementation)
    bundle.vm_manager = create_vm_manager();
    
    logger.info("Service bundle created successfully");
    
    return bundle;
}

} // namespace scratchpad