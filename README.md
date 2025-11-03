# Scratchpad VM Tool

A comprehensive virtual machine management tool for isolated development and testing environments. Available in both Node.js and high-performance C++ implementations.

## Overview

Scratchpad provides lightweight VM environments using QEMU for development and testing. Create isolated environments, run commands safely, and experiment without affecting your host system. Perfect for AI-assisted development where you need secure command execution.

## Architecture

```
scratchpad/
├── node/           # Node.js implementation (original)
├── cpp/            # C++ implementation (high-performance)
├── spec/           # Design documentation
└── README.md       # This file
```

## Key Features

- **Ephemeral by default**: Changes discarded unless explicitly saved
- **Persistent mode**: Save changes when building up environments
- **Fast startup**: Pre-built VM images with cloud-init
- **Work directory mounting**: Access local files inside VMs
- **Multiple VMs**: Run specialized environments simultaneously
- **SSH integration**: Secure command execution and shell access
- **Cross-platform**: Linux, macOS, and Windows support

## Quick Start

### Option 1: Node.js Implementation (Recommended for getting started)

```bash
# Install dependencies and global CLI
cd node
npm install -g .

# Prepare a VM with Python
scratchpad-prepare --name python-dev python3 python3-pip git

# Run commands
scratchpad run --vm python-dev "python3 --version"

# Interactive shell
scratchpad shell --vm python-dev
```

### Option 2: C++ Implementation (Recommended for production)

```bash
# Install system dependencies (Ubuntu/Debian)
sudo apt-get install build-essential cmake qemu-system-x86 libssh-dev

# Build and install
cd cpp
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --parallel
sudo cmake --install .

# Use same commands as Node.js version
scratchpad-prepare --name python-dev python3 python3-pip git
scratchpad run --vm python-dev "python3 --version"
```

## Implementations

### Node.js Implementation (`node/`)

- **Fast development iteration**
- **Easy to modify and extend**
- **Comprehensive feature set**
- **Proven stability**

```bash
cd node
npm install
./scratchpad-cli.js run --vm myvm "echo 'Hello World'"
```

### C++ Implementation (`cpp/`)

- **High performance** (~50% faster startup)
- **Lower memory usage** (~60% less memory)
- **Native system integration**
- **Library and executable**

```bash
cd cpp && make cpp
./build/src/cli/scratchpad run --vm myvm "echo 'Hello World'"
```

## Available Commands

### VM Preparation
```bash
# Create Ubuntu VM with development tools
scratchpad-prepare --name devbox nodejs python3 git vim

# Create minimal Alpine VM
scratchpad-prepare --name minimal --base alpine --memory 256M curl wget
```

### VM Execution
```bash
# Execute single command
scratchpad run --vm devbox "node --version"

# Interactive shell
scratchpad shell --vm devbox

# Keep VM running for multiple commands
scratchpad run -k --vm devbox "echo 'VM started'"
scratchpad run --vm devbox "npm install express"
```

### Live VM Management
```bash
# Spawn persistent background VM
scratchpad-live spawn --vm devbox "my-agent" --memory 1G

# List running VMs
scratchpad-live list

# Connect to running VM
scratchpad-live connect my-agent

# Monitor logs
scratchpad-live logs my-agent --follow

# Stop VM
scratchpad-live stop my-agent
```

## Use Cases

### AI-Assisted Development
```bash
# Safe environment for AI to run commands
scratchpad run "rm -rf suspicious_directory"  # Runs in VM, not host

# Test AI-generated code safely
scratchpad run "python3 ai_generated_script.py"

# Install packages without affecting host
scratchpad run -p "pip install experimental-package"
```

### Development and Testing
```bash
# Test across different environments
scratchpad run --vm ubuntu-vm "pytest tests/"
scratchpad run --vm alpine-vm "pytest tests/"

# Isolated dependency testing
scratchpad run "npm install new-package && npm test"

# Database testing with ephemeral changes
scratchpad run "mysql -e 'DROP DATABASE test_db; CREATE DATABASE test_db;'"
```

### Educational and Experimentation
```bash
# Learn system administration safely
scratchpad run "sudo rm -rf /etc/important-config"  # Only affects VM

# Test deployment scripts
scratchpad run "curl -sSL deploy-script.sh | bash"

# Experiment with system changes
scratchpad run -p "sudo apt-get update && sudo apt-get upgrade"
```

## VM Modes

### Ephemeral Mode (Default)
Changes are discarded when VM stops:
```bash
scratchpad run "sudo apt-get install git"  # Temporary installation
scratchpad run "git --version"             # Command fails - git not installed
```

### Persistent Mode
Changes are saved to disk:
```bash
scratchpad run -p "sudo apt-get install git"  # Permanent installation  
scratchpad run "git --version"                # Works - git is installed
```

## Output Control

### Direct Mode (Clean output for scripting)
```bash
scratchpad run --direct "python3 -c 'print(2+2)'"
# Output: 4
```

### Default Mode (Minimal VM messages)
```bash
scratchpad run "python3 -c 'print(2+2)'"
# Output: Starting VM... ready.
#         4
```

### Verbose Mode (Full details)
```bash
scratchpad run -v "python3 -c 'print(2+2)'"
# Output: 🚀 Starting VM 'default'...
#         ✓ Connected to VM
#         4
#         🛑 Stopping VM...
```

## Installation

### Prerequisites

**All Platforms:**
- QEMU (qemu-system-x86_64)
- SSH client
- ISO creation tools (genisoimage/mkisofs)

**For Node.js:**
- Node.js 14+
- npm

**For C++:**
- C++20 compiler
- CMake 3.20+
- libssh development libraries
- libcurl development libraries

### System-Specific Installation

<details>
<summary><strong>Ubuntu/Debian</strong></summary>

```bash
# Install system dependencies
sudo apt-get update
sudo apt-get install qemu-system-x86 genisoimage ssh-client

# For Node.js version
sudo apt-get install nodejs npm
cd node && npm install -g .

# For C++ version  
sudo apt-get install build-essential cmake libssh-dev libcurl4-openssl-dev
cd cpp && make cpp && sudo make cpp-install
```
</details>

<details>
<summary><strong>macOS</strong></summary>

```bash
# Install system dependencies
brew install qemu

# For Node.js version
brew install node
cd node && npm install -g .

# For C++ version
brew install cmake libssh curl
cd cpp && make cpp && sudo make cpp-install
```
</details>

<details>
<summary><strong>CentOS/RHEL/Fedora</strong></summary>

```bash
# Install system dependencies
sudo dnf install qemu-system-x86 genisoimage openssh-clients

# For Node.js version
sudo dnf install nodejs npm
cd node && npm install -g .

# For C++ version
sudo dnf install gcc-c++ cmake libssh-devel libcurl-devel
cd cpp && make cpp && sudo make cpp-install
```
</details>

### Quick Installation (All Systems)

```bash
# Clone repository
git clone <repository-url>
cd scratchpad

# Check system dependencies
make check-deps

# Set up development environment (installs dependencies)
make dev-setup

# Build everything
make build

# Install globally
make install
```

## Configuration

Create `~/.scratchpad/config.json`:

```json
{
  "vm_directory": "~/.scratchpad/vms",
  "images_directory": "~/.scratchpad/images", 
  "default_memory": "1G",
  "ssh_port_range_start": 2222,
  "ssh_port_range_end": 9999,
  "enable_health_monitoring": true
}
```

Environment variables (prefix with `SCRATCHPAD_`):
```bash
export SCRATCHPAD_VM_DIR="$HOME/my-vms"
export SCRATCHPAD_DEFAULT_MEMORY="2G"
export SCRATCHPAD_SSH_PORT_START=3000
```

## Performance Comparison

| Feature | Node.js | C++ | Improvement |
|---------|---------|-----|-------------|
| Cold start time | ~3s | ~1.5s | 50% faster |
| Memory usage | ~50MB | ~20MB | 60% less |
| Command execution | ~500ms | ~200ms | 60% faster |
| Concurrent VMs | 10-20 | 50+ | 2.5x more |

## Architecture and Design

The project follows Domain-Driven Design principles with clear separation of concerns:

- **VM Domain**: Virtual machine lifecycle and configuration
- **Process Domain**: QEMU process management  
- **Communication Domain**: SSH connectivity and command execution
- **Image Domain**: Base image management and provisioning
- **Resource Domain**: System resource allocation and monitoring

See [spec/design.md](spec/design.md) for detailed architecture documentation.

## Contributing

1. Fork the repository
2. Choose Node.js (`node/`) or C++ (`cpp/`) implementation
3. Follow existing code patterns and architecture
4. Write tests for new features
5. Update documentation
6. Submit pull request

## Troubleshooting

<details>
<summary><strong>Permission denied on /dev/kvm</strong></summary>

```bash
# Add user to kvm group
sudo usermod -aG kvm $USER
# Log out and back in, or run:
newgrp kvm
```
</details>

<details>
<summary><strong>VM won't start</strong></summary>

```bash
# Check QEMU installation
qemu-system-x86_64 --version

# Try with verbose output
scratchpad run -v "echo test"

# Check VM exists
scratchpad list
```
</details>

<details>
<summary><strong>SSH connection fails</strong></summary>

```bash
# Check if VM is running
scratchpad list

# Verify SSH keys exist
ls ~/.scratchpad/keys/

# Try recreating VM
scratchpad-prepare --name test-vm
```
</details>

## License

MIT License - see [LICENSE](LICENSE) file for details.

## Background

This tool was created to provide safe, isolated environments for AI-assisted development. Instead of allowing AI tools to execute potentially dangerous commands on the host system, Scratchpad provides disposable virtual environments where commands can run safely. The ephemeral nature means you can experiment freely, knowing that any mistakes are automatically cleaned up.