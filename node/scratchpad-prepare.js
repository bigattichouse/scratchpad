#!/usr/bin/env node

const { spawn, execSync } = require('child_process');
const fs = require('fs').promises;
const fsSync = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const { promisify } = require('util');
const exec = promisify(require('child_process').exec);
const https = require('https');
const SSH2Promise = require('ssh2-promise');

// Configuration
const CONFIG = {
  vmDir: path.join(os.homedir(), '.scratchpad', 'vms'),
  imagesDir: path.join(os.homedir(), '.scratchpad', 'images'),
  sshKeysDir: path.join(os.homedir(), '.scratchpad', 'keys'),
  cloudInitDir: path.join(os.homedir(), '.scratchpad', 'cloud-init'),
  defaultMemory: '1G',
  defaultDiskSize: '10G',
  sshTimeout: 300000, // 5 minutes
  
  // Image sources
  images: {
    ubuntu: {
      url: 'https://cloud-images.ubuntu.com/jammy/current/jammy-server-cloudimg-amd64.img',
      name: 'ubuntu-22.04-server-cloudimg-amd64.img',
      defaultUser: 'ubuntu'
    },
    alpine: {
      url: 'https://dl-cdn.alpinelinux.org/alpine/v3.19/releases/cloud/nocloud_alpine-3.19.0-x86_64-bios-cloudinit-r0.qcow2',
      name: 'alpine-3.19-cloudimg-amd64.qcow2',
      defaultUser: 'alpine'
    },
    debian: {
      url: 'https://cloud.debian.org/images/cloud/bullseye/latest/debian-11-genericcloud-amd64.qcow2',
      name: 'debian-11-cloudimg-amd64.qcow2',
      defaultUser: 'debian'
    }
  }
};

// ANSI color codes for output
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  red: '\x1b[31m',
  gray: '\x1b[90m',
};

class VMPreparer {
  constructor(options = {}) {
    this.vmName = options.vmName || 'default';
    this.baseImage = options.baseImage || 'ubuntu';
    this.memory = options.memory || CONFIG.defaultMemory;
    this.diskSize = options.diskSize || CONFIG.defaultDiskSize;
    this.packages = options.packages || [];
    this.sshPort = 2222 + Math.floor(Math.random() * 1000);
    this.vmProcess = null;
    this.ssh = null;
    this.verbose = options.verbose || false;
    
    // Paths
    this.vmPath = path.join(CONFIG.vmDir, this.vmName);
    this.diskPath = path.join(this.vmPath, 'disk.qcow2');
    this.cloudInitIso = path.join(this.vmPath, 'cloud-init.iso');
    this.sshKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa');
    this.sshPubKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa.pub');
  }

  log(message, color = '') {
    console.log(`${color}${message}${colors.reset}`);
  }

  debug(message) {
    if (this.verbose) {
      this.log(`[DEBUG] ${message}`, colors.gray);
    }
  }

  async ensureDirectories() {
    this.log('📁 Creating directories...', colors.blue);
    const dirs = [CONFIG.vmDir, CONFIG.imagesDir, CONFIG.sshKeysDir, CONFIG.cloudInitDir, this.vmPath];
    for (const dir of dirs) {
      await fs.mkdir(dir, { recursive: true });
    }
  }

  async downloadFile(url, destPath, description) {
    return new Promise((resolve, reject) => {
      const file = fsSync.createWriteStream(destPath);
      let downloadedBytes = 0;
      let totalBytes = 0;

      https.get(url, (response) => {
        if (response.statusCode === 302 || response.statusCode === 301) {
          // Handle redirect
          file.close();
          this.downloadFile(response.headers.location, destPath, description)
            .then(resolve)
            .catch(reject);
          return;
        }

        if (response.statusCode !== 200) {
          reject(new Error(`Failed to download: ${response.statusCode}`));
          return;
        }

        totalBytes = parseInt(response.headers['content-length'], 10);
        
        response.pipe(file);
        
        response.on('data', (chunk) => {
          downloadedBytes += chunk.length;
          if (totalBytes) {
            const percent = Math.round((downloadedBytes / totalBytes) * 100);
            process.stdout.write(`\r  Downloading ${description}: ${percent}%`);
          }
        });

        file.on('finish', () => {
          file.close();
          console.log(''); // New line after progress
          resolve();
        });
      }).on('error', (err) => {
        fs.unlink(destPath).catch(() => {});
        reject(err);
      });

      file.on('error', (err) => {
        fs.unlink(destPath).catch(() => {});
        reject(err);
      });
    });
  }

  async ensureBaseImage() {
    const imageConfig = CONFIG.images[this.baseImage];
    if (!imageConfig) {
      throw new Error(`Unknown base image: ${this.baseImage}`);
    }

    const imagePath = path.join(CONFIG.imagesDir, imageConfig.name);
    
    try {
      await fs.access(imagePath);
      this.log(`✓ Base image '${imageConfig.name}' already exists`, colors.green);
    } catch {
      this.log(`📥 Downloading ${this.baseImage} base image...`, colors.blue);
      await this.downloadFile(imageConfig.url, imagePath, imageConfig.name);
      this.log(`✓ Downloaded ${imageConfig.name}`, colors.green);
    }

    return imagePath;
  }

  async createVMDisk(baseImagePath) {
    this.log('💾 Creating VM disk...', colors.blue);
    
    // Check if disk already exists
    try {
      await fs.access(this.diskPath);
      this.log('  ⚠️  VM disk already exists. Backing it up...', colors.yellow);
      const backupPath = `${this.diskPath}.backup-${Date.now()}`;
      await fs.rename(this.diskPath, backupPath);
    } catch {
      // Disk doesn't exist, that's fine
    }

    // Create new disk based on base image
    const cmd = `qemu-img create -f qcow2 -b ${baseImagePath} -F qcow2 ${this.diskPath} ${this.diskSize}`;
    this.debug(`Running: ${cmd}`);
    await exec(cmd);
    this.log('✓ VM disk created', colors.green);
  }

  async generateSSHKeys() {
    this.log('🔑 Generating SSH keys...', colors.blue);
    
    try {
      await fs.access(this.sshKeyPath);
      this.log('  ℹ️  SSH keys already exist', colors.gray);
    } catch {
      const cmd = `ssh-keygen -t rsa -b 2048 -f ${this.sshKeyPath} -N "" -C "scratchpad@vm"`;
      await exec(cmd);
      this.log('✓ SSH keys generated', colors.green);
    }

    return await fs.readFile(this.sshPubKeyPath, 'utf8');
  }

  async createCloudInitISO(sshPubKey) {
    this.log('📀 Creating cloud-init ISO...', colors.blue);
    
    const imageConfig = CONFIG.images[this.baseImage];
    const cloudInitPath = path.join(this.vmPath, 'cloud-init-files');
    await fs.mkdir(cloudInitPath, { recursive: true });

    // Create meta-data
    const metadata = `instance-id: ${this.vmName}-${Date.now()}
local-hostname: ${this.vmName}`;
    await fs.writeFile(path.join(cloudInitPath, 'meta-data'), metadata);

    // Create user-data
    const userdata = `#cloud-config
users:
  - name: ${imageConfig.defaultUser}
    ssh_authorized_keys:
      - ${sshPubKey.trim()}
    sudo: ALL=(ALL) NOPASSWD:ALL
    shell: /bin/bash

# Disable cloud-init after first boot for faster subsequent boots
runcmd:
  - touch /etc/cloud/cloud-init.disabled

# For Alpine Linux
apk:
  update: true

# For Ubuntu/Debian
package_update: true
package_upgrade: false

# Set timezone
timezone: UTC

# Disable unnecessary services for faster boot
bootcmd:
  - echo 'Port 22' >> /etc/ssh/sshd_config
  - echo 'PermitRootLogin no' >> /etc/ssh/sshd_config
  - echo 'PasswordAuthentication no' >> /etc/ssh/sshd_config
`;

    await fs.writeFile(path.join(cloudInitPath, 'user-data'), userdata);

    // Create ISO based on the OS
    let isoCmd;
    if (process.platform === 'darwin') {
      // macOS
      isoCmd = `hdiutil makehybrid -o ${this.cloudInitIso} -hfs -joliet -iso -default-volume-name cidata ${cloudInitPath}`;
    } else {
      // Linux (and potentially Windows with WSL)
      isoCmd = `genisoimage -output ${this.cloudInitIso} -volid cidata -joliet -rock ${cloudInitPath}/user-data ${cloudInitPath}/meta-data`;
      
      // Check if genisoimage is available, if not try mkisofs
      try {
        await exec('which genisoimage');
      } catch {
        isoCmd = `mkisofs -output ${this.cloudInitIso} -volid cidata -joliet -rock ${cloudInitPath}/user-data ${cloudInitPath}/meta-data`;
      }
    }

    this.debug(`Running: ${isoCmd}`);
    await exec(isoCmd);
    this.log('✓ Cloud-init ISO created', colors.green);
  }

  async detectAcceleration() {
    if (process.platform === 'linux') {
      try {
        await fs.access('/dev/kvm');
        return 'kvm';
      } catch {
        return 'tcg';
      }
    } else if (process.platform === 'darwin') {
      return 'hvf';
    } else if (process.platform === 'win32') {
      return 'whpx';
    }
    return 'tcg';
  }

  buildQemuCommand(acceleration) {
    const cmd = [
      'qemu-system-x86_64',
      '-name', this.vmName,
      '-machine', 'pc',
      '-m', this.memory,
      '-accel', acceleration,
      '-cpu', 'host',
      '-smp', '2',
      
      // Disk
      '-drive', `file=${this.diskPath},format=qcow2,if=virtio`,
      
      // Cloud-init
      '-drive', `file=${this.cloudInitIso},format=raw,if=virtio`,
      
      // Network
      '-netdev', `user,id=net0,hostfwd=tcp::${this.sshPort}-:22`,
      '-device', 'virtio-net-pci,netdev=net0',
      
      // Display
      '-display', 'none',
      '-serial', 'mon:stdio',
      
      // Boot optimization
      '-boot', 'c',
    ];

    // Add VGA for debugging if verbose
    if (this.verbose) {
      cmd.push('-vga', 'std');
    }

    return cmd;
  }

  async startVM() {
    this.log(`🚀 Starting VM '${this.vmName}'...`, colors.bright);
    
    const acceleration = await this.detectAcceleration();
    this.log(`  Using acceleration: ${acceleration}`, colors.gray);
    
    const qemuCmd = this.buildQemuCommand(acceleration);
    this.debug(`QEMU command: ${qemuCmd.join(' ')}`);
    
    return new Promise((resolve, reject) => {
      this.vmProcess = spawn(qemuCmd[0], qemuCmd.slice(1), {
        stdio: this.verbose ? 'inherit' : 'ignore'
      });

      this.vmProcess.on('error', (err) => {
        reject(new Error(`Failed to start QEMU: ${err.message}`));
      });

      // Give VM time to start
      setTimeout(resolve, 5000);
    });
  }

  async waitForSSH() {
    this.log('⏳ Waiting for SSH connection...', colors.blue);
    
    const imageConfig = CONFIG.images[this.baseImage];
    const sshConfig = {
      host: 'localhost',
      port: this.sshPort,
      username: imageConfig.defaultUser,
      privateKey: await fs.readFile(this.sshKeyPath),
      readyTimeout: CONFIG.sshTimeout,
      reconnect: false,
      keepaliveInterval: 5000,
      algorithms: {
        serverHostKey: ['rsa-sha2-512', 'rsa-sha2-256', 'ssh-rsa']
      }
    };

    this.ssh = new SSH2Promise(sshConfig);

    const startTime = Date.now();
    const maxRetries = 60; // 5 minutes with 5-second intervals
    let retries = 0;

    while (retries < maxRetries) {
      try {
        await this.ssh.connect();
        
        // Test the connection with a simple command using the correct API
        await this.ssh.exec('echo "SSH_OK"');
        
        this.log('✓ SSH connection established', colors.green);
        return;
      } catch (err) {
        retries++;
        const elapsed = Math.round((Date.now() - startTime) / 1000);
        
        if (retries % 6 === 0) { // Log every 30 seconds
          this.log(`  Still waiting... (${elapsed}s elapsed)`, colors.gray);
        }
        
        // Close any partial connection before retrying
        try {
          this.ssh.close();
        } catch {}
        
        // Recreate the SSH object for next attempt
        this.ssh = new SSH2Promise(sshConfig);
        
        await new Promise(resolve => setTimeout(resolve, 5000));
      }
    }
    
    throw new Error('Failed to establish SSH connection after 5 minutes');
  }

async executeCommand(command, description) {
    this.log(`📦 ${description}`, colors.blue);
    this.debug(`  Command: ${command}`);
    
    try {
      // Use the direct exec method
      const result = await this.ssh.exec(command);
      
      // SSH2Promise returns different formats depending on version
      // Handle both string and object returns
      let stdout = '';
      let stderr = '';
      let code = 0;
      
      if (typeof result === 'string') {
        stdout = result;
        // For string results, we assume success unless it contains error indicators
        if (stdout.toLowerCase().includes('error') || stdout.toLowerCase().includes('failed')) {
          code = 1;
        }
      } else if (result && typeof result === 'object') {
        stdout = result.stdout || '';
        stderr = result.stderr || '';
        code = result.code !== undefined ? result.code : 0;
      } else {
        // Handle case where result is null/undefined
        this.log(`  ⚠️  Command returned unexpected result: ${typeof result}`, colors.yellow);
        this.debug(`  Raw result: ${JSON.stringify(result)}`);
      }
      
      if (code !== 0) {
        this.log(`  ⚠️  Command exited with code ${code}`, colors.yellow);
        if (stderr) {
          this.log(`  stderr: ${stderr.trim()}`, colors.red);
        }
        if (stdout && this.verbose) {
          this.log(`  stdout: ${stdout.trim()}`, colors.gray);
        }
        // For non-zero exit codes, throw an error with meaningful message
        const errorMsg = stderr || stdout || `Command failed with exit code ${code}`;
        throw new Error(errorMsg.trim());
      } else {
        this.log(`  ✓ Success`, colors.green);
      }
      
      if (this.verbose && stdout) {
        this.log('  Output:', colors.gray);
        console.log(stdout.trim());
      }
      
      return { stdout, stderr, code };
    } catch (err) {
      // Improved error handling
      let errorMessage = 'Unknown error';
      
      if (err && err.message) {
        errorMessage = err.message;
      } else if (typeof err === 'string') {
        errorMessage = err;
      } else if (err) {
        errorMessage = JSON.stringify(err);
      }
      
      this.log(`  ❌ Error: ${errorMessage}`, colors.red);
      this.debug(`  Full error object: ${JSON.stringify(err, null, 2)}`);
      
      throw new Error(errorMessage);
    }
  }

async installPackages() {
    this.log('\n📦 Installing packages...', colors.bright);
    
    // Detect package manager and update package lists
    let pkgManager, updateCmd, installCmd, envVars;
    
    if (this.baseImage === 'ubuntu' || this.baseImage === 'debian') {
      pkgManager = 'apt';
      // Set environment variables for non-interactive installation
      envVars = 'DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true';
      updateCmd = `${envVars} sudo -E apt-get update -qq`;
      installCmd = `${envVars} sudo -E apt-get install -y -qq`;
      
      // Configure debconf for non-interactive mode
      await this.executeCommand('echo "debconf debconf/frontend select Noninteractive" | sudo debconf-set-selections', 'Configuring non-interactive mode');
      
      // First update package lists
      await this.executeCommand(updateCmd, 'Updating package lists');
      
      // Install essential packages
      await this.executeCommand(`${installCmd} curl wget git vim`, 'Installing essential tools');
      
    } else if (this.baseImage === 'alpine') {
      pkgManager = 'apk';
      updateCmd = 'sudo apk update';
      installCmd = 'sudo apk add --no-cache';
      
      await this.executeCommand(updateCmd, 'Updating package lists');
      await this.executeCommand(`${installCmd} curl wget git vim`, 'Installing essential tools');
    }
    
    // Install requested packages with special handling for common ones
    for (const pkg of this.packages) {
      try {
        if (pkg === 'nodejs' && pkgManager === 'apt') {
          // For Ubuntu, install nodejs and npm together with non-interactive mode
          await this.executeCommand(`${installCmd} nodejs npm`, `Installing ${pkg} and npm`);
        } else if (pkg === 'git' && pkgManager === 'apt') {
          // Git might already be installed, check first
          try {
            await this.executeCommand('git --version', 'Checking if git is already installed');
            this.log(`  ✓ Git already installed`, colors.green);
          } catch {
            await this.executeCommand(`${installCmd} git`, `Installing ${pkg}`);
          }
        } else if (pkgManager === 'apt') {
          // Use non-interactive mode for all apt packages
          await this.executeCommand(`${installCmd} ${pkg}`, `Installing ${pkg}`);
        } else {
          await this.executeCommand(`${installCmd} ${pkg}`, `Installing ${pkg}`);
        }
      } catch (err) {
        this.log(`  ⚠️  Failed to install ${pkg}: ${err.message}`, colors.yellow);
        // Continue with other packages instead of failing completely
        continue;
      }
    }
    
    // Special handling for common development environments
    if (this.packages.includes('python3') || this.packages.includes('python')) {
      try {
        if (pkgManager === 'apt') {
          await this.executeCommand(`${installCmd} python3-pip python3-venv`, 'Installing Python development tools');
        } else if (pkgManager === 'apk') {
          await this.executeCommand(`${installCmd} py3-pip`, 'Installing pip');
        }
      } catch (err) {
        this.log(`  ⚠️  Failed to install Python dev tools: ${err.message}`, colors.yellow);
      }
    }
  }

  async finalizeVM() {
    this.log('\n🔧 Finalizing VM...', colors.blue);
    
    // Clean package cache to reduce image size
    try {
      if (this.baseImage === 'ubuntu' || this.baseImage === 'debian') {
        await this.executeCommand('sudo apt-get clean', 'Cleaning package cache');
      } else if (this.baseImage === 'alpine') {
        await this.executeCommand('sudo rm -rf /var/cache/apk/*', 'Cleaning package cache');
      }
    } catch (err) {
      // Non-critical error, continue
      this.debug(`Cache cleaning failed: ${err.message}`);
    }
    
    // Create a marker file to indicate this VM has been prepared
    const prepDate = new Date().toISOString();
    const prepInfo = {
      name: this.vmName,
      baseImage: this.baseImage,
      packages: this.packages,
      preparedAt: prepDate
    };
    
    try {
      // Use a simpler approach to avoid shell escaping issues
      const jsonContent = JSON.stringify(prepInfo, null, 2);
      const encodedContent = Buffer.from(jsonContent).toString('base64');
      await this.executeCommand(
        `echo '${encodedContent}' | base64 -d | sudo tee /etc/scratchpad-prepared.json`,
        'Writing preparation info'
      );
    } catch (err) {
      this.debug(`Failed to write prep info: ${err.message}`);
    }
  }

  async stopVM() {
    this.log('\n🛑 Stopping VM...', colors.yellow);
    
    if (this.ssh) {
      try {
        await this.executeCommand('sudo poweroff', 'Shutting down VM');
      } catch {
        // SSH connection will be closed by poweroff
      }
      
      this.ssh.close();
    }
    
    // Wait for QEMU process to exit
    if (this.vmProcess) {
      await new Promise(resolve => {
        this.vmProcess.on('exit', resolve);
        setTimeout(resolve, 10000); // Max 10 seconds wait
      });
      
      if (!this.vmProcess.killed) {
        this.vmProcess.kill('SIGTERM');
      }
    }
    
    this.log('✓ VM stopped', colors.green);
  }

  async prepare() {
    try {
      await this.ensureDirectories();
      
      // Download base image if needed
      const baseImagePath = await this.ensureBaseImage();
      
      // Create VM disk
      await this.createVMDisk(baseImagePath);
      
      // Generate SSH keys
      const sshPubKey = await this.generateSSHKeys();
      
      // Create cloud-init ISO
      await this.createCloudInitISO(sshPubKey);
      
      // Start VM
      await this.startVM();
      
      // Wait for SSH
      await this.waitForSSH();
      
      // Install packages
      await this.installPackages();
      
      // Finalize
      await this.finalizeVM();
      
      // Stop VM
      await this.stopVM();
      
      this.log(`\n✅ VM '${this.vmName}' prepared successfully!`, colors.green);
      this.log(`\nVM Location: ${this.vmPath}`, colors.blue);
      this.log(`Disk Image: ${this.diskPath}`, colors.blue);
      this.log(`\nTo use this VM with scratchpad-cli:`, colors.gray);
      this.log(`  scratchpad run --vm ${this.vmName} <command>`, colors.gray);
      
    } catch (err) {
      this.log(`\n❌ Error: ${err.message}`, colors.red);
      
      // Cleanup on error
      if (this.vmProcess) {
        await this.stopVM();
      }
      
      throw err;
    }
  }
}

// CLI argument parsing
function parseArgs(args) {
  const options = {
    vmName: 'default',
    baseImage: 'ubuntu',
    packages: [],
    memory: CONFIG.defaultMemory,
    diskSize: CONFIG.defaultDiskSize,
    verbose: false
  };

  for (let i = 0; i < args.length; i++) {
    const arg = args[i];
    
    switch (arg) {
      case '--name':
      case '-n':
        options.vmName = args[++i];
        break;
      
      case '--base':
      case '-b':
        options.baseImage = args[++i];
        break;
      
      case '--memory':
      case '-m':
        options.memory = args[++i];
        break;
      
      case '--disk':
      case '-d':
        options.diskSize = args[++i];
        break;
      
      case '--verbose':
      case '-v':
        options.verbose = true;
        break;
      
      case '--help':
      case '-h':
        showHelp();
        process.exit(0);
        break;
      
      default:
        if (!arg.startsWith('-')) {
          options.packages.push(arg);
        }
    }
  }

  return options;
}

function showHelp() {
  console.log(`
${colors.bright}Scratchpad VM Preparation Tool${colors.reset}

${colors.blue}Usage:${colors.reset}
  scratchpad-prepare [options] [packages...]

${colors.blue}Options:${colors.reset}
  -n, --name <name>      VM name (default: 'default')
  -b, --base <image>     Base image: ubuntu, alpine, debian (default: 'ubuntu')
  -m, --memory <size>    Memory size (default: '1G')
  -d, --disk <size>      Disk size (default: '10G')
  -v, --verbose          Enable verbose output
  -h, --help             Show this help

${colors.blue}Examples:${colors.reset}
  # Create a default Ubuntu VM with Python
  scratchpad-prepare python3

  # Create a named VM with multiple packages
  scratchpad-prepare --name datascience --memory 2G python3 jupyter pandas numpy

  # Create an Alpine-based VM
  scratchpad-prepare --base alpine --name lightweight nodejs npm

  # Create a Debian VM for development
  scratchpad-prepare --base debian --name devbox build-essential git vim

${colors.blue}Available base images:${colors.reset}
  - ubuntu (22.04 LTS)
  - alpine (3.19)
  - debian (11)
`);
}

// Main execution
async function main() {
  const args = process.argv.slice(2);
  
  if (args.length === 0 || args.includes('--help') || args.includes('-h')) {
    showHelp();
    process.exit(0);
  }
  
  const options = parseArgs(args);
  
  console.log(`${colors.bright}🚀 Scratchpad VM Preparation${colors.reset}`);
  console.log(`${colors.gray}═══════════════════════════${colors.reset}\n`);
  
  console.log(`VM Name: ${colors.blue}${options.vmName}${colors.reset}`);
  console.log(`Base Image: ${colors.blue}${options.baseImage}${colors.reset}`);
  console.log(`Memory: ${colors.blue}${options.memory}${colors.reset}`);
  console.log(`Disk Size: ${colors.blue}${options.diskSize}${colors.reset}`);
  if (options.packages.length > 0) {
    console.log(`Packages: ${colors.blue}${options.packages.join(', ')}${colors.reset}`);
  }
  console.log('');
  
  const preparer = new VMPreparer(options);
  
  try {
    await preparer.prepare();
    process.exit(0);
  } catch (err) {
    console.error(`\n${colors.red}Preparation failed: ${err.message}${colors.reset}`);
    if (options.verbose) {
      console.error(err.stack);
    }
    process.exit(1);
  }
}

if (require.main === module) {
  main();
}

module.exports = { VMPreparer };
