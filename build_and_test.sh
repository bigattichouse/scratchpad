#!/bin/bash

set -e  # Exit on any error

echo "=== Scratchpad C++ Build and Test Script ==="

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Function to print colored output
print_status() {
    echo -e "${GREEN}[INFO]${NC} $1"
}

print_warning() {
    echo -e "${YELLOW}[WARN]${NC} $1"
}

print_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

# Check if we're in the right directory
if [ ! -f "cpp/CMakeLists.txt" ]; then
    print_error "Please run this script from the project root directory"
    exit 1
fi

# Check system dependencies
print_status "Checking system dependencies..."

check_command() {
    if command -v "$1" >/dev/null 2>&1; then
        echo "  ✓ $1 found"
    else
        print_error "$1 not found. Please install $1"
        exit 1
    fi
}

check_command cmake
check_command make
check_command qemu-system-x86_64
check_command pkg-config

# Check for development libraries
print_status "Checking development libraries..."

check_pkg_config() {
    if pkg-config --exists "$1"; then
        echo "  ✓ $1 found ($(pkg-config --modversion $1))"
    else
        print_error "$1 not found. Please install the development package"
        exit 1
    fi
}

check_pkg_config libssh
check_pkg_config libcurl

# Check for OpenSSL
if pkg-config --exists openssl; then
    echo "  ✓ OpenSSL found ($(pkg-config --modversion openssl))"
elif [ -f "/usr/include/openssl/ssl.h" ]; then
    echo "  ✓ OpenSSL headers found"
else
    print_error "OpenSSL not found. Please install OpenSSL development package"
    exit 1
fi

# Create build directory
print_status "Setting up build directory..."
cd cpp
rm -rf build
mkdir -p build
cd build

# Configure with CMake
print_status "Configuring with CMake..."
cmake -DCMAKE_BUILD_TYPE=Release \
      -DBUILD_EXAMPLES=ON \
      -DBUILD_TESTING=OFF \
      ..

# Build the project
print_status "Building project..."
make -j$(nproc)

# Check if executables were created
print_status "Verifying build outputs..."

if [ -f "bin/scratchpad" ]; then
    echo "  ✓ Main CLI executable created"
else
    print_error "Main CLI executable not found"
    exit 1
fi

if [ -f "lib/libscratchpad.so" ] || [ -f "lib/libscratchpad.dylib" ]; then
    echo "  ✓ Shared library created"
else
    print_error "Shared library not found"
    exit 1
fi

if [ -f "examples/basic_usage_example" ]; then
    echo "  ✓ Examples built successfully"
else
    print_warning "Examples not found (optional)"
fi

# Test basic CLI functionality
print_status "Testing basic CLI functionality..."

# Test help command
if ./bin/scratchpad help >/dev/null 2>&1; then
    echo "  ✓ Help command works"
else
    print_error "Help command failed"
    exit 1
fi

# Test version command
if ./bin/scratchpad version >/dev/null 2>&1; then
    echo "  ✓ Version command works"
else
    print_error "Version command failed"
    exit 1
fi

# Test images command (should work without VMs)
if ./bin/scratchpad images >/dev/null 2>&1; then
    echo "  ✓ Images command works"
else
    print_warning "Images command failed (may be normal if no images configured)"
fi

# Test resource command
if ./bin/scratchpad resources >/dev/null 2>&1; then
    echo "  ✓ Resources command works"
else
    print_error "Resources command failed"
    exit 1
fi

print_status "Basic functionality tests passed!"

# Copy executable to project root for convenience
print_status "Installing executable to project root..."
cd ../..
cp cpp/build/bin/scratchpad ./scratchpad
chmod +x ./scratchpad

echo ""
print_status "Build completed successfully!"
echo ""
echo "You can now use Scratchpad:"
echo "  ./scratchpad help              # Show help"
echo "  ./scratchpad images            # List available images"
echo "  ./scratchpad resources         # Show system resources"
echo ""
echo "To install system-wide:"
echo "  cd cpp/build && sudo make install"
echo ""
echo "To run examples:"
echo "  ./cpp/build/examples/basic_usage_example"
echo "  ./cpp/build/examples/async_operations_example"
echo ""

# Show build summary
echo "Build Summary:"
echo "  Executable: ./scratchpad"
echo "  Library: cpp/build/lib/libscratchpad.*"
echo "  Headers: cpp/include/scratchpad/"
echo "  Examples: cpp/build/examples/"
echo ""

print_status "All tests passed! Scratchpad is ready to use."