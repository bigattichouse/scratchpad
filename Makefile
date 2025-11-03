# Top-level Makefile for Scratchpad project

.PHONY: all clean build test install help node cpp node-install cpp-build cpp-test cpp-install

# Default target
all: help

help:
	@echo "Scratchpad VM Tool Build System"
	@echo "================================"
	@echo ""
	@echo "Available targets:"
	@echo "  help          - Show this help message"
	@echo ""
	@echo "Node.js Implementation:"
	@echo "  node          - Install Node.js dependencies"
	@echo "  node-install  - Install Node.js version globally"
	@echo ""
	@echo "C++ Implementation:"  
	@echo "  cpp           - Build C++ implementation (Release)"
	@echo "  cpp-debug     - Build C++ implementation (Debug)"
	@echo "  cpp-test      - Run C++ tests"
	@echo "  cpp-install   - Install C++ implementation"
	@echo "  cpp-clean     - Clean C++ build files"
	@echo ""
	@echo "General:"
	@echo "  clean         - Clean all build artifacts"
	@echo "  test          - Run all tests"
	@echo "  install       - Install both implementations"

# Node.js targets
node:
	@echo "Installing Node.js dependencies..."
	cd node && npm install

node-install: node
	@echo "Installing Node.js version globally..."
	cd node && npm install -g .

# C++ targets
cpp:
	@echo "Building C++ implementation (Release)..."
	mkdir -p cpp/build
	cd cpp/build && cmake -DCMAKE_BUILD_TYPE=Release .. && cmake --build . --parallel

cpp-debug:
	@echo "Building C++ implementation (Debug)..."
	mkdir -p cpp/build
	cd cpp/build && cmake -DCMAKE_BUILD_TYPE=Debug .. && cmake --build . --parallel

cpp-test: cpp
	@echo "Running C++ tests..."
	cd cpp/build && ctest --output-on-failure --parallel

cpp-install: cpp
	@echo "Installing C++ implementation..."
	cd cpp/build && sudo cmake --install .

cpp-clean:
	@echo "Cleaning C++ build files..."
	rm -rf cpp/build

# Combined targets
build: cpp

test: cpp-test

install: node-install cpp-install

clean: cpp-clean
	@echo "Cleaning Node.js dependencies..."
	rm -rf node/node_modules

# Development targets
dev-setup:
	@echo "Setting up development environment..."
	@echo "Installing system dependencies..."
	@if command -v apt-get >/dev/null 2>&1; then \
		sudo apt-get update && \
		sudo apt-get install -y build-essential cmake pkg-config \
		                        qemu-system-x86 libssh-dev libcurl4-openssl-dev \
		                        genisoimage ssh-client nodejs npm; \
	elif command -v brew >/dev/null 2>&1; then \
		brew install cmake qemu libssh curl node npm; \
	elif command -v dnf >/dev/null 2>&1; then \
		sudo dnf install -y gcc-c++ cmake pkg-config qemu-system-x86 \
		                    libssh-devel libcurl-devel genisoimage \
		                    openssh-clients nodejs npm; \
	else \
		echo "Please install dependencies manually for your system"; \
	fi
	make node

format:
	@echo "Formatting C++ code..."
	@if command -v clang-format >/dev/null 2>&1; then \
		find cpp/src cpp/include -name "*.cpp" -o -name "*.hpp" | xargs clang-format -i; \
		echo "Code formatted with clang-format"; \
	else \
		echo "clang-format not found, skipping formatting"; \
	fi

lint:
	@echo "Running C++ linting..."
	@if command -v clang-tidy >/dev/null 2>&1; then \
		cd cpp/build && \
		find ../src ../include -name "*.cpp" -o -name "*.hpp" | \
		xargs clang-tidy -p .; \
	else \
		echo "clang-tidy not found, skipping lint"; \
	fi

docs:
	@echo "Generating documentation..."
	@if command -v doxygen >/dev/null 2>&1; then \
		cd cpp && doxygen Doxyfile; \
		echo "Documentation generated in cpp/docs/"; \
	else \
		echo "doxygen not found, skipping documentation generation"; \
	fi

# Packaging targets
package-node: node
	@echo "Creating Node.js package..."
	cd node && npm pack

package-cpp: cpp
	@echo "Creating C++ package..."
	cd cpp/build && cpack

package: package-node package-cpp

# Check system requirements
check-deps:
	@echo "Checking system dependencies..."
	@echo "Checking for required tools:"
	@command -v cmake >/dev/null 2>&1 && echo "✓ cmake" || echo "✗ cmake (required)"
	@command -v qemu-system-x86_64 >/dev/null 2>&1 && echo "✓ qemu-system-x86_64" || echo "✗ qemu-system-x86_64 (required)"
	@command -v ssh >/dev/null 2>&1 && echo "✓ ssh" || echo "✗ ssh (required)"
	@command -v node >/dev/null 2>&1 && echo "✓ node" || echo "✗ node (required for Node.js version)"
	@command -v npm >/dev/null 2>&1 && echo "✓ npm" || echo "✗ npm (required for Node.js version)"
	@pkg-config --exists libssh && echo "✓ libssh" || echo "✗ libssh (required for C++)"
	@pkg-config --exists libcurl && echo "✓ libcurl" || echo "✗ libcurl (required for C++)"
	@echo ""
	@echo "Optional tools:"
	@command -v clang-format >/dev/null 2>&1 && echo "✓ clang-format" || echo "- clang-format (for code formatting)"
	@command -v clang-tidy >/dev/null 2>&1 && echo "✓ clang-tidy" || echo "- clang-tidy (for linting)"
	@command -v doxygen >/dev/null 2>&1 && echo "✓ doxygen" || echo "- doxygen (for documentation)"