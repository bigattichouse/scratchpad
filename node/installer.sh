#!/bin/bash

# Scratchpad VM Tool Installer
# Supports Linux (Debian/Ubuntu, RHEL/CentOS/Fedora, Arch) and macOS

set -e

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

# Helper functions
log() { echo -e "${BLUE}[INFO]${NC} $1"; }
success() { echo -e "${GREEN}[SUCCESS]${NC} $1"; }
warn() { echo -e "${YELLOW}[WARNING]${NC} $1"; }
error() { echo -e "${RED}[ERROR]${NC} $1"; }

echo "======================================"
echo "      Scratchpad VM Tool Installer"
echo "======================================"
echo

# Check if running as root
if [ "$EUID" -eq 0 ]; then
    error "Please do not run this installer as root"
    echo "  Run as your regular user (sudo will be prompted when needed)"
    exit 1
fi

# Detect OS and distribution
detect_os() {
    log "Detecting operating system..."
    
    if [[ "$OSTYPE" == "linux-gnu"* ]]; then
        OS="linux"
        if [ -f /etc/os-release ]; then
            . /etc/os-release
            case $ID in
                ubuntu|debian)
                    DISTRO="debian"
                    PKG_MGR="apt"
                    ;;
                centos|rhel|fedora|rocky|almalinux)
                    DISTRO="redhat"
                    if command -v dnf &> /dev/null; then
                        PKG_MGR="dnf"
                    else
                        PKG_MGR="yum"
                    fi
                    ;;
                arch|manjaro)
                    DISTRO="arch"
                    PKG_MGR="pacman"
                    ;;
                opensuse*|sles)
                    DISTRO="opensuse"
                    PKG_MGR="zypper"
                    ;;
                *)
                    DISTRO="unknown"
                    ;;
            esac
        else
            DISTRO="unknown"
        fi
    elif [[ "$OSTYPE" == "darwin"* ]]; then
        OS="macos"
        DISTRO="macos"
    else
        error "Unsupported OS: $OSTYPE"
        echo "Supported: Linux (Ubuntu/Debian, RHEL/CentOS/Fedora, Arch), macOS"
        exit 1
    fi
    
    success "Detected: $OS ($DISTRO)"
}

# Check for required commands
check_dependencies() {
    log "Checking system requirements..."
    
    local missing_deps=()
    
    # Check Node.js version
    if command -v node &> /dev/null; then
        NODE_VERSION=$(node -v | sed 's/v//')
        NODE_MAJOR=$(echo $NODE_VERSION | cut -d. -f1)
        if [ "$NODE_MAJOR" -ge 14 ]; then
            success "Node.js $NODE_VERSION found"
        else
            warn "Node.js $NODE_VERSION found, but 14+ recommended"
        fi
    else
        missing_deps+=("nodejs")
    fi
    
    # Check npm
    if ! command -v npm &> /dev/null; then
        missing_deps+=("npm")
    fi
    
    # Check curl (for downloads)
    if ! command -v curl &> /dev/null; then
        missing_deps+=("curl")
    fi
    
    if [ ${#missing_deps[@]} -gt 0 ]; then
        error "Missing required dependencies: ${missing_deps[*]}"
        echo "Please install these first:"
        echo "  Ubuntu/Debian: sudo apt install nodejs npm curl"
        echo "  RHEL/CentOS:   sudo dnf install nodejs npm curl"
        echo "  Arch:          sudo pacman -S nodejs npm curl"
        echo "  macOS:         brew install node"
        exit 1
    fi
}

# Install system dependencies
install_system_deps() {
    log "Installing system dependencies..."
    
    case $OS in
        linux)
            case $DISTRO in
                debian)
                    log "Updating package lists..."
                    sudo apt-get update -qq
                    
                    log "Installing QEMU and tools..."
                    sudo apt-get install -y \
                        qemu-system-x86 \
                        qemu-utils \
                        genisoimage \
                        openssh-client \
                        bridge-utils
                    
                    # Add user to kvm group if KVM is available
                    if [ -e /dev/kvm ]; then
                        log "Adding $USER to kvm group for hardware acceleration..."
                        sudo usermod -aG kvm "$USER"
                        KVM_ADDED=1
                    fi
                    ;;
                    
                redhat)
                    log "Installing QEMU and tools..."
                    sudo $PKG_MGR install -y \
                        qemu-system-x86 \
                        qemu-img \
                        genisoimage \
                        openssh-clients \
                        bridge-utils
                    
                    if [ -e /dev/kvm ]; then
                        sudo usermod -aG kvm "$USER"
                        KVM_ADDED=1
                    fi
                    ;;
                    
                arch)
                    log "Installing QEMU and tools..."
                    sudo pacman -S --needed --noconfirm \
                        qemu-system-x86 \
                        qemu-img \
                        cdrtools \
                        openssh \
                        bridge-utils
                    
                    if [ -e /dev/kvm ]; then
                        sudo usermod -aG kvm "$USER"
                        KVM_ADDED=1
                    fi
                    ;;
                    
                opensuse)
                    log "Installing QEMU and tools..."
                    sudo zypper install -y \
                        qemu-x86 \
                        qemu-tools \
                        mkisofs \
                        openssh-clients
                    
                    if [ -e /dev/kvm ]; then
                        sudo usermod -aG kvm "$USER"
                        KVM_ADDED=1
                    fi
                    ;;
                    
                *)
                    error "Unsupported Linux distribution: $DISTRO"
                    echo "Please install manually:"
                    echo "  - qemu-system-x86_64"
                    echo "  - qemu-img"
                    echo "  - genisoimage or mkisofs"
                    echo "  - openssh client"
                    exit 1
                    ;;
            esac
            ;;
            
        macos)
            if command -v brew &> /dev/null; then
                log "Installing QEMU with Homebrew..."
                brew install qemu
            else
                error "Homebrew not found"
                echo "Please install Homebrew first: https://brew.sh"
                echo "Then run: brew install qemu"
                exit 1
            fi
            ;;
    esac
    
    success "System dependencies installed"
}

# Verify QEMU installation
verify_qemu() {
    log "Verifying QEMU installation..."
    
    if command -v qemu-system-x86_64 &> /dev/null; then
        QEMU_VERSION=$(qemu-system-x86_64 --version | head -1)
        success "QEMU installed: $QEMU_VERSION"
        
        # Test basic QEMU functionality
        if qemu-system-x86_64 -M help &> /dev/null; then
            success "QEMU basic functionality verified"
        else
            warn "QEMU installed but may not work properly"
        fi
    else
        error "QEMU installation failed or qemu-system-x86_64 not in PATH"
        exit 1
    fi
}

# Create package.json if it doesn't exist
create_package_json() {
    if [ ! -f "package.json" ]; then
        log "Creating package.json..."
        cat > package.json << 'EOF'
{
  "name": "scratchpad-vm",
  "version": "1.0.0",
  "description": "Lightweight VM scratchpad for development and testing",
  "main": "scratchpad-cli.js",
  "bin": {
    "scratchpad": "./scratchpad-cli.js",
    "scratchpad-prepare": "./scratchpad-prepare.js"
  },
  "dependencies": {
    "ssh2-promise": "^1.0.1"
  },
  "engines": {
    "node": ">=14.0.0"
  },
  "repository": {
    "type": "git",
    "url": "https://github.com/user/scratchpad-vm.git"
  },
  "keywords": ["vm", "qemu", "development", "sandbox"],
  "author": "Scratchpad Contributors",
  "license": "MIT"
}
EOF
        success "Created package.json"
    fi
}

# Install Node.js dependencies
install_node_deps() {
    log "Installing Node.js dependencies..."
    
    create_package_json
    
    if npm install; then
        success "Node.js dependencies installed"
    else
        error "Failed to install Node.js dependencies"
        exit 1
    fi
}

# Set up global CLI access
setup_global_cli() {
    log "Setting up global CLI commands..."
    
    # Make scripts executable
    chmod +x *.js 2>/dev/null || true
    
    # Create symlinks in /usr/local/bin (if writable) or ~/.local/bin
    INSTALL_DIR=""
    if [ -w "/usr/local/bin" ]; then
        INSTALL_DIR="/usr/local/bin"
    elif [ -d "$HOME/.local/bin" ]; then
        INSTALL_DIR="$HOME/.local/bin"
        # Add to PATH if not already there
        if [[ ":$PATH:" != *":$HOME/.local/bin:"* ]]; then
            echo 'export PATH="$HOME/.local/bin:$PATH"' >> ~/.bashrc
            warn "Added ~/.local/bin to PATH in ~/.bashrc"
            warn "Run 'source ~/.bashrc' or restart terminal"
        fi
    else
        mkdir -p "$HOME/.local/bin"
        INSTALL_DIR="$HOME/.local/bin"
    fi
    
    SCRIPT_DIR="$(pwd)"
    
    # Create wrapper scripts instead of symlinks for better portability
    cat > "$INSTALL_DIR/scratchpad" << EOF
#!/bin/bash
cd "$SCRIPT_DIR" && node scratchpad-cli.js "\$@"
EOF
    
    cat > "$INSTALL_DIR/scratchpad-prepare" << EOF
#!/bin/bash
cd "$SCRIPT_DIR" && node scratchpad-prepare.js "\$@"
EOF
    
    chmod +x "$INSTALL_DIR/scratchpad" "$INSTALL_DIR/scratchpad-prepare"
    
    success "Global CLI commands installed to $INSTALL_DIR"
}

# Create scratchpad directories
create_directories() {
    log "Creating Scratchpad directories..."
    
    mkdir -p ~/.scratchpad/{vms,images,keys,cloud-init}
    
    success "Created ~/.scratchpad directory structure"
}


# Main installation
main() {
    detect_os
    check_dependencies
    install_system_deps
    verify_qemu
    install_node_deps
    setup_global_cli
    create_directories
    
    echo
    echo "======================================"
    success "Installation Complete!"
    echo "======================================"
    echo
    echo "Next steps:"
    echo
    echo "1. ${BLUE}Prepare your first VM:${NC}"
    echo "   scratchpad-prepare python3"
    echo
    echo "2. ${BLUE}Run a command:${NC}"
    echo "   scratchpad run \"python3 --version\""
    echo
    echo "3. ${BLUE}Start interactive shell:${NC}"
    echo "   scratchpad shell"
    echo
    
    if [ "$KVM_ADDED" = "1" ]; then
        echo "${YELLOW}Important:${NC} You were added to the 'kvm' group for hardware acceleration."
        echo "Please log out and back in, or run: ${BLUE}newgrp kvm${NC}"
        echo
    fi
    
    if [ "$OS" = "linux" ] && [ -e /dev/kvm ]; then
        if [ ! -r /dev/kvm ] || [ ! -w /dev/kvm ]; then
            warn "KVM may not be accessible yet. Run: newgrp kvm"
        fi
    fi
    
    echo "For help: scratchpad --help"
    echo "Documentation: cat README.md"
}

# Handle Ctrl+C gracefully
trap 'echo; error "Installation interrupted"; exit 1' INT

# Run main installation
main "$@"
