# Scratchpad C++ Design Document

## Overview

This document outlines the design for a C++ implementation of the Scratchpad VM tool, following Domain-Driven Design (DDD) principles. The system provides isolated VM execution environments for development and testing, with both standalone executable and linkable library interfaces.

## Current Node.js Implementation Analysis

The existing Node.js implementation provides three main components:

1. **scratchpad-cli.js** - Interactive VM execution and command runner
2. **scratchpad-live.js** - Persistent VM management with background processes
3. **scratchpad-prepare.js** - VM image preparation and provisioning

### Key Features Identified

- **VM Lifecycle Management**: Create, start, stop, connect to VMs
- **QEMU Integration**: Hardware acceleration, disk management, networking
- **SSH-based Communication**: Secure command execution and shell access
- **Cloud-init Provisioning**: Automated VM setup with packages and configuration
- **Multiple VM Types**: Ephemeral (changes discarded) vs Persistent (changes saved)
- **Multi-VM Support**: Spawn multiple instances from same base image
- **Work Directory Mounting**: 9p filesystem integration for host/guest file sharing
- **Resource Management**: Memory allocation, port allocation, disk overlay management
- **Health Monitoring**: Process tracking, SSH connectivity checks
- **Logging and Debugging**: Comprehensive output control and error reporting

## Domain Model

### Core Domains

#### 1. Virtual Machine Domain
**Purpose**: Manage VM lifecycle, configuration, and state

**Entities**:
- `VirtualMachine` - Core VM entity with identity, configuration, and state
- `VMConfiguration` - Resource allocation, networking, and behavior settings
- `VMImage` - Base images and overlays

**Value Objects**:
- `VMId` - Unique VM identifier
- `ResourceLimits` - Memory, CPU, disk constraints
- `NetworkConfiguration` - Port mappings, SSH settings
- `DiskConfiguration` - Storage mode (ephemeral/persistent), size, overlays

**Services**:
- `VMLifecycleService` - Create, start, stop, destroy operations
- `VMHealthService` - Monitor VM status and connectivity

#### 2. Process Management Domain  
**Purpose**: Handle QEMU process spawning, monitoring, and control

**Entities**:
- `QemuProcess` - Running QEMU instance
- `ProcessRegistry` - Track active processes

**Value Objects**:
- `ProcessId` - System process identifier
- `ProcessStatus` - Running, stopped, crashed states
- `AccelerationType` - KVM, HVF, WHPX, TCG

**Services**:
- `ProcessManager` - Spawn and control QEMU processes
- `ProcessMonitor` - Health checks and crash detection

#### 3. Communication Domain
**Purpose**: SSH connectivity and command execution

**Entities**:
- `SSHConnection` - Persistent connection to VM
- `CommandExecution` - Command lifecycle and results

**Value Objects**:
- `SSHCredentials` - Keys, ports, user information
- `CommandResult` - Exit code, stdout, stderr
- `ExecutionContext` - Environment, working directory

**Services**:
- `SSHService` - Connection management and command execution
- `CommandDispatcher` - Execute commands with proper context

#### 4. Image Management Domain
**Purpose**: VM image preparation, provisioning, and base image management

**Entities**:
- `BaseImage` - Downloaded cloud images (Ubuntu, Alpine, Debian)
- `PreparedImage` - Customized images with packages and configuration
- `OverlayDisk` - Copy-on-write disk overlays

**Value Objects**:
- `ImageSource` - Download URLs and metadata
- `PackageList` - Software to install during preparation
- `ProvisioningScript` - Cloud-init and setup commands

**Services**:
- `ImageDownloader` - Fetch base images from cloud providers
- `ImageProvisioner` - Install packages and configure images
- `OverlayManager` - Create and manage disk overlays

#### 5. Resource Management Domain
**Purpose**: Allocate and track system resources

**Value Objects**:
- `PortRange` - Available port ranges for SSH/VNC
- `MemoryAllocation` - RAM limits and allocation
- `DiskSpace` - Storage requirements and limits

**Services**:
- `PortAllocator` - Find available ports for VM services
- `ResourceTracker` - Monitor system resource usage

### Cross-Cutting Concerns

#### Configuration Management
- **ConfigurationService** - Load/save settings, defaults, validation
- Support for JSON, YAML, or TOML configuration files
- Environment variable overrides

#### Logging and Monitoring
- **LoggingService** - Structured logging with levels and categories
- **MetricsCollector** - Performance and usage statistics
- **HealthMonitor** - System-wide health checks

#### Error Handling
- **ErrorHandler** - Centralized error processing and recovery
- Domain-specific exceptions with error codes
- Graceful degradation strategies

## Architecture

### Layered Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                        │
│  • CLI Interface     • Library API     • Configuration     │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                     Domain Layer                            │
│  • VM Domain    • Process Domain    • Communication        │
│  • Image Domain • Resource Domain   • Cross-cutting        │
└─────────────────────────────────────────────────────────────┘
┌─────────────────────────────────────────────────────────────┐
│                  Infrastructure Layer                       │
│  • QEMU Adapter    • SSH Client     • File System          │
│  • Network Utils   • Process Utils  • Cloud-init           │
└─────────────────────────────────────────────────────────────┘
```

### Key Design Patterns

#### Repository Pattern
- `VMRepository` - Persist VM configurations and state
- `ImageRepository` - Manage base and prepared images
- `ProcessRepository` - Track running processes

#### Factory Pattern
- `VMFactory` - Create VMs with different configurations
- `QemuCommandFactory` - Build QEMU command lines
- `SSHConnectionFactory` - Establish SSH connections

#### Strategy Pattern
- `AccelerationStrategy` - Different hypervisor backends
- `ProvisioningStrategy` - Package managers (apt, apk, yum)
- `DiskStrategy` - Ephemeral vs persistent storage

#### Observer Pattern
- `VMStatusObserver` - React to VM state changes
- `ProcessMonitorObserver` - Handle process events
- `HealthCheckObserver` - Respond to health status changes

#### Command Pattern
- `VMCommand` - Start, stop, connect operations
- `ProvisioningCommand` - Package installation steps
- `ConfigurationCommand` - System configuration changes

## Technology Stack

### Core Libraries
- **C++20** - Modern C++ with concepts, ranges, coroutines
- **fmt** - Fast, safe string formatting
- **spdlog** - Fast logging library
- **CLI11** - Command line parsing
- **nlohmann/json** - JSON parsing and generation

### System Integration
- **libssh** - SSH client functionality
- **libcurl** - HTTP downloads for base images
- **subprocess** - Process management and execution
- **filesystem** - C++17 filesystem operations

### Build System
- **CMake** - Cross-platform build system
- **Conan** - Dependency management
- **Google Test** - Unit and integration testing
- **Catch2** - Alternative testing framework

### Platform Support
- **Linux** - Primary target with KVM acceleration
- **macOS** - HVF acceleration support
- **Windows** - WHPX acceleration support

## API Design

### Library Interface

```cpp
namespace scratchpad {

// Main VM management interface
class VMManager {
public:
    // Lifecycle operations
    VMId create_vm(const VMConfiguration& config);
    void start_vm(const VMId& id);
    void stop_vm(const VMId& id);
    void destroy_vm(const VMId& id);
    
    // Communication
    CommandResult execute_command(const VMId& id, const std::string& command);
    SSHConnection connect_ssh(const VMId& id);
    
    // Management
    std::vector<VirtualMachine> list_vms() const;
    VirtualMachine get_vm(const VMId& id) const;
    HealthReport check_health(const VMId& id) const;
};

// Image preparation interface
class ImageManager {
public:
    // Base image management
    void download_base_image(ImageType type);
    std::vector<BaseImage> list_base_images() const;
    
    // VM preparation
    PreparedImageId prepare_image(const std::string& name,
                                  ImageType base,
                                  const std::vector<std::string>& packages);
    std::vector<PreparedImage> list_prepared_images() const;
};

// Configuration and resource management
class ResourceManager {
public:
    PortRange allocate_port_range(size_t count);
    void release_ports(const PortRange& range);
    ResourceUsage get_resource_usage() const;
    SystemLimits get_system_limits() const;
};

}
```

### CLI Interface

```bash
# Main VM operations
scratchpad run --vm <name> <command>
scratchpad shell --vm <name>
scratchpad list [--all] [--health]

# Live VM management
scratchpad-live spawn --vm <base> <name> [options]
scratchpad-live connect <name>
scratchpad-live stop <name> [--force]
scratchpad-live logs <name> [--follow]

# Image preparation
scratchpad-prepare --name <name> [packages...]
scratchpad-prepare --base <image> --name <name> [packages...]
```

## Implementation Plan

### Phase 1: Core Infrastructure
1. **Project Setup**
   - CMake build system configuration
   - Dependency management with Conan
   - CI/CD pipeline setup
   - Code style and formatting standards

2. **Foundation Classes**
   - Basic value objects (VMId, ProcessId, etc.)
   - Configuration management
   - Logging infrastructure
   - Error handling framework

3. **Process Management**
   - QEMU process spawning and control
   - Process monitoring and health checks
   - Command line generation for QEMU

### Phase 2: Domain Implementation
1. **Virtual Machine Domain**
   - VM entity and configuration
   - VM lifecycle management
   - State persistence and recovery

2. **Communication Domain**
   - SSH connection management
   - Command execution framework
   - Secure communication protocols

3. **Image Management Domain**
   - Base image downloading
   - Cloud-init integration
   - Image preparation pipeline

### Phase 3: Application Layer
1. **Library API**
   - Public C++ interface
   - Thread safety and async operations
   - Documentation and examples

2. **CLI Application**
   - Command line parsing
   - Interactive and batch modes
   - User experience optimization

3. **Integration Testing**
   - End-to-end test scenarios
   - Performance benchmarking
   - Platform compatibility testing

### Phase 4: Advanced Features
1. **Multi-VM Support**
   - Resource allocation and scheduling
   - VM networking and communication
   - Cluster management capabilities

2. **Monitoring and Observability**
   - Metrics collection and reporting
   - Health monitoring dashboard
   - Performance profiling tools

3. **Extensibility**
   - Plugin architecture
   - Custom provisioning scripts
   - Third-party integrations

## Migration Strategy

### Directory Structure
```
scratchpad/
├── node/                    # Move existing Node.js code here
│   ├── scratchpad-cli.js
│   ├── scratchpad-live.js
│   ├── scratchpad-prepare.js
│   └── package.json
├── cpp/                     # New C++ implementation
│   ├── include/
│   ├── src/
│   ├── tests/
│   ├── examples/
│   └── CMakeLists.txt
├── spec/
│   └── design.md           # This document
├── README.md
└── Makefile               # Top-level build coordination
```

### Compatibility Considerations
- **Configuration Compatibility** - Ensure C++ version can read Node.js VM configurations
- **Image Compatibility** - Share prepared VM images between implementations
- **API Parity** - Maintain feature compatibility while improving performance
- **Migration Path** - Gradual transition with both versions available

## Benefits of C++ Implementation

### Performance Improvements
- **Faster Startup** - Reduced overhead compared to Node.js
- **Lower Memory Usage** - Native memory management
- **Better Resource Utilization** - Direct system integration

### Platform Integration
- **Native System Calls** - Direct process and network management
- **Hardware Acceleration** - Better QEMU integration
- **System Services** - Native daemon/service support

### Deployment Advantages
- **Single Binary** - No runtime dependencies
- **Container Friendly** - Minimal base images
- **Library Integration** - Easy embedding in other projects

### Development Benefits
- **Type Safety** - Compile-time error detection
- **Performance Profiling** - Better tooling for optimization
- **Memory Safety** - Modern C++ RAII and smart pointers

## Conclusion

This design provides a robust foundation for a C++ implementation of the Scratchpad VM tool that maintains feature parity with the Node.js version while providing improved performance and platform integration. The DDD approach ensures maintainable, testable code with clear separation of concerns.

The implementation plan allows for incremental development and testing, with the ability to run both versions in parallel during the transition period. The library interface enables integration with other C++ projects while the standalone executable provides direct replacement functionality.