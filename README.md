# Scratchpad VM Tool

A lightweight VM tool for development and testing. Create isolated environments, run commands, and experiment safely without affecting your host system.

## Background

Using AI commandline tools can require allowing some scary permissions (ex: "allow model to rm -rf?"), I wanted to isolate commands using a VM that could be ephemeral (erased each time), or persistent, as needed.  So instead of the AI trying to "reason out" math, it can write a little program and run it to get the answer directly. This VASTLY increases good output.  This was also an experiment to use claude to create what I needed, and I'm very happy with the result.

## Is this Docker or similar tool?

This is a Virtual Machine image being run through QEMU.  You can prep the image with packages you need (I've been using Ubuntu), then have your tool call into the VM directly to run commands.

## Features

- **Ephemeral by default**: Changes are discarded unless explicitly saved
- **Persistent mode**: Save changes when you want to build up a VM
- **Fast startup**: Pre-built VM images with cloud-init
- **Work directory mounting**: Access your local files inside the VM
- **Multiple VMs**: Create specialized environments for different projects
- **Flexible output**: Direct, minimal, or verbose output modes
- **Non-interactive support**: Install packages without prompts

## Requirements

- **Linux** (Ubuntu/Debian, RHEL/CentOS/Fedora, Arch) or **macOS**
- **QEMU/KVM** (for virtualization)
- **Node.js 14+** and npm
- **SSH client tools**
- **ISO creation tools** (genisoimage/mkisofs)

## Installation

1. **Clone or download** this repository
2. **Run the installer**:
   ```bash
   ./install.sh
   ```
3. **Follow the prompts** - the installer will:
   - Install system dependencies (QEMU, etc.)
   - Install Node.js dependencies
   - Set up global CLI commands
   - Create necessary directories

After installation, you can use `scratchpad` and `scratchpad-prepare` commands globally.

## Quick Start

1. **Prepare a VM** with your desired packages:
   ```bash
   scratchpad-prepare --name dev nodejs python3 git
   ```

2. **Run commands** in the VM:
   ```bash
   scratchpad run --vm dev "node --version"
   ```

3. **Start an interactive shell**:
   ```bash
   scratchpad shell --vm dev
   ```

## VM Preparation

Use `scratchpad-prepare` to create VMs with pre-installed software:

### Basic Syntax
```bash
scratchpad-prepare [options] [packages...]
```

### Options
- `-n, --name <name>` - VM name (default: 'default')
- `-b, --base <image>` - Base image: ubuntu, alpine, debian (default: 'ubuntu')
- `-m, --memory <size>` - Memory allocation (default: '1G')
- `-d, --disk <size>` - Disk size (default: '10G')
- `-v, --verbose` - Show detailed output
- `-h, --help` - Show help

### Example: Node.js Development VM
```bash
# Create a VM with Node.js and development tools
scratchpad-prepare --name nodedev nodejs npm git vim

# The VM will be created with all packages installed and ready to use
```

## Usage

Use `scratchpad` to run commands or start shells in prepared VMs:

### Basic Syntax
```bash
scratchpad run [options] <command>    # Execute a command
scratchpad shell [options]            # Start interactive shell
scratchpad [options] <command>        # Execute command (shorthand)
```

### CLI Options

| Option | Description |
|--------|-------------|
| `--vm <name>` | Use specific VM (default: 'default') |
| `-m, --memory <size>` | Memory allocation (default: '512M') |
| `-d, --dir <path>` | Directory to mount (default: current) |
| `-k, --keep-alive` | Keep VM running after command |
| `-n, --non-interactive` | Run in non-interactive mode |
| `-p, --persistent` | Save changes to VM disk |
| `-v, --verbose` | Show startup/connection details |
| `--direct` | Show only command output |
| `--debug` | Show debug information |
| `-h, --help` | Show help |

### VM Modes

**Ephemeral (default)**: Changes are discarded when VM stops
```bash
scratchpad run "sudo apt-get install git"  # Test installation, changes lost
```

**Persistent**: Changes are saved permanently 
```bash
scratchpad run -p "sudo apt-get install git"  # Install permanently
```

### Output Modes

**Direct (`--direct`)**: Pure command output only - perfect for scripting
```bash
scratchpad run --direct "node --version"
# Output: v18.17.0
```

**Default**: Minimal VM messages + command output
```bash
scratchpad run "node --version"
# Output: Starting VM... connecting... ready.
#         v18.17.0
```

**Verbose (`-v`)**: Full VM details + command output
```bash
scratchpad run -v "node --version"
# Output: 🚀 Starting VM 'default'...
#         ⏳ Connecting to VM...
#         ✓ Connected to VM
#         v18.17.0
#         🛑 Stopping VM...
```

## Examples

### Prepare and Test Node.js Environment

```bash
# 1. Create a Node.js development VM
scratchpad-prepare --name nodedev nodejs npm git

# 2. Check the installed version (direct output)
scratchpad run --vm nodedev --direct "node --version"
# Output: v18.17.0

# 3. Test npm (minimal output)  
scratchpad run --vm nodedev "npm --version"
# Output: Starting VM... connecting... ready.
#         9.6.7

# 4. Install a package temporarily (ephemeral)
scratchpad run --vm nodedev "npm install -g typescript"

# 5. Install a package permanently 
scratchpad run --vm nodedev -p "npm install -g typescript"

# 6. Interactive development session
scratchpad shell --vm nodedev
```

### Python Data Science

```bash
# Create VM with Python and data tools
scratchpad-prepare --name datascience python3 python3-pip

# Install packages permanently
scratchpad run --vm datascience -p -n "pip3 install pandas numpy jupyter"

# Run analysis script with direct output
scratchpad run --vm datascience --direct "python3 analysis.py"

# Start Jupyter notebook (persistent session)
scratchpad run --vm datascience -p -k "jupyter notebook --ip=0.0.0.0"
```

### Testing and Development

```bash
# Quick test in default VM (ephemeral)
scratchpad run "python3 -c 'print(\"Hello VM!\")'"

# Test with more memory
scratchpad run -m 2G "python3 memory_intensive_script.py"

# Debug with verbose output
scratchpad run -v "python3 problematic_script.py"

# Work with local files (automatically mounted)
scratchpad run "ls -la"  # Shows your current directory files

# Install something just for testing (changes discarded)
scratchpad run "sudo apt-get install -y htop && htop"

# Install something permanently
scratchpad run -p -n "sudo apt-get install -y htop"
```

### Automation and Scripting

```bash
# Get command output for scripts (direct mode)
VERSION=$(scratchpad run --direct "python3 --version")
echo "VM Python version: $VERSION"

# Run tests quietly
scratchpad run --direct "pytest tests/ -v" > test_results.txt

# Batch processing with different VMs
scratchpad run --vm nodedev --direct "npm test" > node_results.txt
scratchpad run --vm python --direct "python3 test.py" > python_results.txt

# Check if a package is available
if scratchpad run --direct "which docker" > /dev/null 2>&1; then
    echo "Docker is available in the VM"
fi
```

### Multi-VM Workflow

```bash
# Create specialized VMs
scratchpad-prepare --name frontend nodejs npm yarn
scratchpad-prepare --name backend python3 postgresql-client redis-tools
scratchpad-prepare --name tools git docker.io curl

# Use different VMs for different tasks
scratchpad run --vm frontend "npm run build"
scratchpad run --vm backend "python3 manage.py test"  
scratchpad run --vm tools "docker ps"

# Compare versions across VMs
echo "Frontend Node: $(scratchpad run --vm frontend --direct 'node --version')"
echo "Backend Python: $(scratchpad run --vm backend --direct 'python3 --version')"
```

### Advanced Usage

#### Keep VM Running for Multiple Commands
```bash
# Start VM and keep it running
scratchpad run -k "echo 'VM started'"

# Run more commands on the same VM instance (faster)
scratchpad run --vm default "python3 script1.py"
scratchpad run --vm default "python3 script2.py"
```

#### Non-Interactive Package Installation
```bash
# Install packages without prompts
scratchpad run -n -p "sudo apt-get update && sudo apt-get install -y nginx"

# Configure system settings non-interactively  
scratchpad run -n -p "sudo dpkg-reconfigure -f noninteractive tzdata"
```

#### Working with Different Base Images
```bash
# Alpine Linux VM (smaller, faster)
scratchpad-prepare --base alpine --name minimal python3

# Debian VM  
scratchpad-prepare --base debian --name stable nodejs

# Use the different VMs
scratchpad run --vm minimal "python3 --version"
scratchpad run --vm stable "node --version"
```

#### Combining Options
```bash
# Install packages permanently, non-interactively, with direct output
scratchpad run --vm myvm -p -n --direct "sudo apt-get install -y git vim curl"

# Debug a failing command with full verbosity
scratchpad run --vm problematic -v -p "failing_command_here"

# Test in ephemeral mode, then install permanently
scratchpad run --vm dev "npm install lodash"  # Test first
scratchpad run --vm dev -p "npm install lodash"  # Install permanently
```

## List Available VMs

```bash
scratchpad list
```

Output:
```
Available VMs:
  • default
    Base: ubuntu, Packages: python3
  • nodedev  
    Base: ubuntu, Packages: nodejs, npm, git
  • datascience
    Base: ubuntu, Packages: python3, python3-pip
```

## Best Practices

### VM Management
- **Start with ephemeral mode** to test things safely
- **Create specialized VMs** for different projects/languages
- **Use persistent mode sparingly** - keep VMs minimal and rebuild when needed
- **Name your VMs descriptively** (`--name frontend`, `--name datascience`)

### Output Control
- **Use `--direct` for scripting** to get clean output
- **Use default mode for interactive work** - shows progress without clutter
- **Use `--verbose` for debugging** when things go wrong

### Performance Tips
- **Use `--keep-alive`** when running multiple commands on the same VM
- **Allocate appropriate memory** with `-m` for memory-intensive tasks
- **Use Alpine base** for lightweight, fast-starting VMs

### Package Management
- **Test installations in ephemeral mode first**
- **Use `-n` flag for automated package installation**
- **Group related installations together** in single commands

## Troubleshooting

### Common Issues

**Permission denied on /dev/kvm**
```bash
# Add yourself to kvm group and re-login
sudo usermod -aG kvm $USER
# Then log out and back in, or run:
newgrp kvm
```

**Command not found: scratchpad**
```bash
# Check if ~/.local/bin is in your PATH
echo $PATH | grep -q "$HOME/.local/bin" || echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
source ~/.bashrc
```

**VM won't start**
```bash
# Check QEMU installation
qemu-system-x86_64 --version

# Check if VM exists
scratchpad list

# Try with verbose mode
scratchpad run -v "echo test"
```

**SSH connection fails**
```bash
# Try with verbose mode to see connection details
scratchpad run -v --vm myvm "echo test"

# Check if VM disk exists
ls ~/.scratchpad/vms/myvm/disk.qcow2
```

**Package installation prompts**
```bash
# Use non-interactive mode
scratchpad run -n "sudo apt-get install package"

# For persistent installation
scratchpad run -p -n "sudo apt-get install package"
```

### Debug Mode

Use `--debug` to see detailed parsing and execution information:
```bash
scratchpad run --debug --vm myvm "python3 --version"
```

This shows:
- Argument parsing steps
- VM startup details
- SSH connection process
- Command execution details

---

**Need help?**
- `scratchpad --help` - CLI help
- `scratchpad-prepare --help` - VM preparation help
- `scratchpad list` - Show available VMs
