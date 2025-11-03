# Scratchpad C++ Test Suite

A comprehensive test suite for the Scratchpad VM management system, ensuring reliability, performance, and feature parity with the Node.js implementation.

## Overview

The test suite covers all layers of the application with over **600 test cases** across:
- **Domain Logic**: Core business entities and value objects
- **Application Services**: High-level orchestration and workflows
- **Infrastructure**: External system integrations
- **Configuration & Error Handling**: System configuration and comprehensive error scenarios

## Test Structure

```
tests/
├── CMakeLists.txt              # Test configuration with GoogleTest
├── utils/                      # Test utilities and helpers
│   ├── test_helpers.hpp/cpp    # Common test utilities and fixtures
│   ├── mock_qemu_adapter.*     # Mock QEMU process management
│   ├── mock_ssh_client.*       # Mock SSH client implementation
│   └── temp_directory.cpp      # Temporary directory management
├── unit/                       # Unit tests for individual components
│   ├── domain/                 # Domain layer tests
│   │   ├── test_vm_id.cpp           # VM identifier validation & operations
│   │   ├── test_vm_configuration.cpp # VM configuration management
│   │   ├── test_virtual_machine.cpp  # VM entity lifecycle & state
│   │   ├── test_qemu_process.cpp     # QEMU process monitoring
│   │   ├── test_ssh_connection.cpp   # SSH connection management
│   │   ├── test_command_execution.cpp # Command execution tracking
│   │   ├── test_configuration.cpp    # System configuration
│   │   └── test_errors.cpp           # Error hierarchy & handling
│   └── application/            # Application layer tests
│       └── test_vm_manager.cpp      # VM management orchestration
└── integration/                # Integration tests (planned for future implementation)
    └── (planned: CLI interface, VM workflows, file operations)
```

## Test Categories

### 🔧 Domain Layer Tests (400+ tests)

#### VM Domain
- **VMId**: Validation, uniqueness, normalization, comparison operators
- **VMConfiguration**: Resource allocation, feature flags, package management, validation
- **VirtualMachine**: Status transitions, resource tracking, persistence, error handling

#### Process Domain  
- **QemuProcess**: Lifecycle management, resource monitoring, command line analysis, health checks

#### Communication Domain
- **SSHConnection**: Authentication, connection quality, error tracking, keep-alive
- **CommandExecution**: Output handling, timeout management, progress tracking, cancellation

### ⚙️ Configuration & Error Handling (100+ tests)
- **Configuration**: File I/O, environment overrides, type safety, validation, merging
- **Error Hierarchy**: VM, Process, SSH, Image, Configuration, Resource errors with context

### 🎯 Application Services (200+ tests)
- **VMManager**: VM lifecycle, concurrent operations, resource management, health monitoring

## Dependencies

### System Requirements
```bash
# Ubuntu/Debian
sudo apt-get install build-essential cmake
sudo apt-get install libssh-dev libcurl4-openssl-dev libssl-dev
sudo apt-get install qemu-system-x86

# Test dependencies (automatically fetched)
# - GoogleTest v1.14.0
# - GoogleMock (included with GoogleTest)
```

### C++ Requirements
- **C++20** compatible compiler (GCC 10+, Clang 12+, MSVC 2019+)
- **CMake 3.20+**
- **Standard library** with filesystem support

## Building and Running Tests

### Quick Start
```bash
cd cpp
mkdir build && cd build

# Configure with testing enabled
cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON ..

# Build all tests
make -j$(nproc)

# Run all tests
ctest --verbose
```

### Individual Test Execution
```bash
# Run specific test suite
./test_vm_domain
./test_process_domain
./test_communication_domain
./test_vm_manager

# Run with specific GoogleTest filters
./test_vm_domain --gtest_filter="VMIdTest.*"
./test_vm_configuration --gtest_filter="*Validation*"
```

### Test Coverage
```bash
# Build with coverage
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_CXX_FLAGS="--coverage" ..
make -j$(nproc)

# Run tests and generate coverage report
ctest
gcov -r .
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_html
```

## Test Features

### 🛡️ Mock-Based Testing
- **External Dependencies**: QEMU and SSH operations are mocked for reliable, fast tests
- **Deterministic**: Tests run consistently without external system dependencies
- **Isolated**: Each test runs in complete isolation with temporary directories

### 🔄 Concurrent Testing  
- **Thread Safety**: Multi-threaded operations tested with concurrent access patterns
- **Resource Contention**: Tests verify proper handling of concurrent VM operations
- **Async Operations**: Future/promise-based async operations validated

### ⚡ Performance Testing
- **Resource Limits**: Memory, CPU, and disk allocation limits enforced
- **Timeout Handling**: Command execution and connection timeouts tested
- **Health Monitoring**: VM health checks and system resource monitoring

### 🎯 Edge Case Coverage
- **Error Conditions**: Comprehensive error scenario testing
- **Invalid Input**: Malformed configurations and parameters
- **Resource Exhaustion**: Out-of-memory and disk space scenarios
- **Network Issues**: Connection failures and SSH problems

## Test Utilities

### TestHelpers Class
```cpp
// Random test data generation
std::string vm_id = TestHelpers::random_vm_id();
std::string data = TestHelpers::random_string(100);

// SSH key generation for testing
auto [private_key, public_key] = TestHelpers::create_temp_ssh_keys(temp_dir);

// Port availability checking
int port = TestHelpers::find_available_port(2222, 9999);

// Async condition waiting
bool ready = TestHelpers::wait_for([&]() {
    return vm_manager.is_vm_ready(vm_id);
}, std::chrono::seconds{30});
```

### TempDirectory Management
```cpp
TempDirectory temp_dir;  // Automatically cleaned up
temp_dir.create_file("test.txt", "content");
auto subdir = temp_dir.create_subdirectory("configs");
```

### VMTestFixture
```cpp
class MyVMTest : public VMTestFixture {
protected:
    void SetUp() override {
        // temp_dir_, vm_dir_, ssh_keys_dir_ available
        // test_vm_id_ pre-generated
    }
};
```

## Test Configuration

### Environment Variables
```bash
# Customize test behavior
export SCRATCHPAD_TEST_VM_DIR="/custom/test/path"
export SCRATCHPAD_TEST_TIMEOUT_MS=30000
export SCRATCHPAD_TEST_SSH_PORT_START=3000
export SCRATCHPAD_TEST_VERBOSE=1
```

### CMake Options
```bash
# Enable/disable test categories
cmake -DBUILD_UNIT_TESTS=ON \
      -DBUILD_INTEGRATION_TESTS=OFF \
      -DBUILD_PERFORMANCE_TESTS=ON \
      ..
```

## Continuous Integration

### GitHub Actions Example
```yaml
name: C++ Tests
on: [push, pull_request]
jobs:
  test:
    runs-on: ubuntu-latest
    steps:
    - uses: actions/checkout@v3
    - name: Install dependencies
      run: |
        sudo apt-get update
        sudo apt-get install -y build-essential cmake libssh-dev
    - name: Build and test
      run: |
        cd cpp && mkdir build && cd build
        cmake -DCMAKE_BUILD_TYPE=Debug -DBUILD_TESTING=ON ..
        make -j$(nproc)
        ctest --output-on-failure
```

## Writing New Tests

### Test Naming Convention
- **Test Files**: `test_<component>.cpp`
- **Test Fixtures**: `<Component>Test` 
- **Test Cases**: `<Component>Test.<Feature><Scenario>`

### Example Test Structure
```cpp
#include <gtest/gtest.h>
#include "utils/test_helpers.hpp"
#include "component/my_component.hpp"

class MyComponentTest : public ::testing::Test {
protected:
    void SetUp() override {
        // Test setup
    }
    
    void TearDown() override {
        // Test cleanup
    }
    
    // Test data members
};

TEST_F(MyComponentTest, BasicFunctionality) {
    // Arrange
    MyComponent component(test_config);
    
    // Act
    auto result = component.do_something();
    
    // Assert
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.value, expected_value);
}

TEST_F(MyComponentTest, ErrorHandling) {
    MyComponent component;
    
    // Test error conditions
    EXPECT_THROW(component.invalid_operation(), ScratchpadError);
}
```

### Mock Usage
```cpp
#include "utils/mock_qemu_adapter.hpp"

TEST_F(MyTest, QemuIntegration) {
    auto mock_qemu = MockQemuAdapterFactory::create_mock();
    
    // Set expectations
    EXPECT_CALL(*mock_qemu, start_vm_async(_))
        .WillOnce(Return(std::async(std::launch::deferred, []() { return true; })));
    
    // Test with mock
    MyComponent component(std::move(mock_qemu));
    auto result = component.start_vm(vm_id);
    
    EXPECT_TRUE(result);
}
```

## Test Metrics

### Coverage Goals
- **Line Coverage**: >90%
- **Branch Coverage**: >85% 
- **Function Coverage**: >95%

### Performance Benchmarks
- **Test Execution**: <30 seconds for full suite
- **Memory Usage**: <500MB peak during testing
- **Parallel Execution**: Support for -j8 concurrent test execution

## Troubleshooting

### Common Issues

**Build Failures:**
```bash
# Missing dependencies
sudo apt-get install libssh-dev libcurl4-openssl-dev

# CMake version too old
# Install CMake 3.20+ from CMake website

# C++20 support missing  
# Use GCC 10+ or Clang 12+
```

**Test Failures:**
```bash
# Run with verbose output
ctest --verbose --output-on-failure

# Run specific failing test
./test_vm_domain --gtest_filter="VMIdTest.Construction"

# Enable debug logging
export SCRATCHPAD_TEST_VERBOSE=1
```

**Performance Issues:**
```bash
# Reduce parallel test execution
make -j2  # Instead of -j$(nproc)

# Run subset of tests
ctest -R "domain.*"  # Only domain tests
```

## Future Enhancements

### Planned Test Additions
- **Integration Tests**: End-to-end CLI workflows
- **Performance Tests**: Load testing with multiple VMs
- **Fuzz Testing**: Input validation with random data
- **Property-Based Testing**: Invariant checking

### Test Infrastructure Improvements
- **Test Reporting**: HTML test reports with coverage
- **CI/CD Integration**: Automated testing on multiple platforms
- **Test Data Management**: Shared test fixtures and datasets
- **Benchmark Integration**: Performance regression detection

## Contributing

When adding new features to Scratchpad:

1. **Write tests first** (TDD approach preferred)
2. **Test both success and failure paths**
3. **Include edge cases and boundary conditions**
4. **Use appropriate mocks** for external dependencies
5. **Follow existing naming conventions**
6. **Update this documentation** for new test categories

The test suite is the foundation of Scratchpad's reliability and maintainability. Comprehensive testing ensures the C++ implementation maintains feature parity with the Node.js version while delivering superior performance.