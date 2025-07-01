#!/usr/bin/env node

const { spawn } = require('child_process');
const fs = require('fs').promises;
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const SSH2Promise = require('ssh2-promise');

// Configuration
const CONFIG = {
  vmDir: path.join(os.homedir(), '.scratchpad', 'vms'),
  sshKeysDir: path.join(os.homedir(), '.scratchpad', 'keys'),
  defaultMemory: '512M',
  defaultVM: 'default',
  sshTimeout: 60000, // 1 minute
};

// ANSI color codes
const colors = {
  reset: '\x1b[0m',
  bright: '\x1b[1m',
  green: '\x1b[32m',
  yellow: '\x1b[33m',
  blue: '\x1b[34m',
  red: '\x1b[31m',
  gray: '\x1b[90m',
};

class ScratchpadCLI {
  constructor(options = {}) {
    this.vmName = options.vm || CONFIG.defaultVM;
    this.memory = options.memory || CONFIG.defaultMemory;
    this.workDir = options.workDir || process.cwd();
    this.command = options.command;
    this.interactive = options.interactive || false;
    this.verbose = options.verbose || false;
    this.keepAlive = options.keepAlive || false;
    this.nonInteractive = options.nonInteractive || false;
    this.persistent = options.persistent || false;
    this.direct = options.direct || false;
    this.debug = options.debug || false;
    
    // Generate unique SSH port
    this.sshPort = 2222 + Math.floor(Math.random() * 1000);
    
    // Paths
    this.vmPath = path.join(CONFIG.vmDir, this.vmName);
    this.diskPath = path.join(this.vmPath, 'disk.qcow2');
    this.sshKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa');
    
    this.vmProcess = null;
    this.ssh = null;
  }

  log(message, color = '') {
    if (this.verbose) {
      console.log(`${color}${message}${colors.reset}`);
    }
  }

  info(message, color = '') {
    // Always shown messages (startup/shutdown)
    console.log(`${color}${message}${colors.reset}`);
  }

  error(message) {
    console.error(`${colors.red}❌ ${message}${colors.reset}`);
  }

  async checkVMExists() {
    try {
      await fs.access(this.diskPath);
      const prepInfoPath = path.join(this.vmPath, 'cloud-init-files', 'user-data');
      const prepInfo = await fs.readFile(prepInfoPath, 'utf8').catch(() => 'Unknown');
      
      this.log(`✓ Found VM '${this.vmName}'`, colors.green);
      return true;
    } catch {
      this.error(`VM '${this.vmName}' not found. Create it with: scratchpad-prepare --name ${this.vmName}`);
      return false;
    }
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
      '-name', `${this.vmName}-session`,
      '-machine', 'pc',
      '-m', this.memory,
      '-accel', acceleration,
      '-cpu', 'host',
      '-smp', '2',
      
      // Use the prepared disk - persistent or ephemeral mode
      '-drive', this.persistent 
        ? `file=${this.diskPath},format=qcow2,if=virtio`
        : `file=${this.diskPath},format=qcow2,if=virtio,snapshot=on`,
      
      // Network
      '-netdev', `user,id=net0,hostfwd=tcp::${this.sshPort}-:22`,
      '-device', 'virtio-net-pci,netdev=net0',
      
      // 9p mount for current directory
      '-virtfs', `local,path=${this.workDir},mount_tag=workdir,security_model=mapped-xattr,id=workdir`,
      
      // Display
      '-display', 'none',
    ];

    if (this.interactive) {
      cmd.push('-serial', 'mon:stdio');
    } else {
      cmd.push('-serial', 'null');
      cmd.push('-monitor', 'none');
    }

    return cmd;
  }

  async startVM() {
    if (this.direct) {
      // Absolutely no output in direct mode
    } else if (!this.verbose) {
      // Minimal startup message for non-verbose mode
      process.stdout.write('Starting VM...');
    } else {
      this.log(`🚀 Starting VM '${this.vmName}'...`, colors.bright);
    }
    
    const acceleration = await this.detectAcceleration();
    this.log(`  Acceleration: ${acceleration}`, colors.gray);
    this.log(`  Work directory: ${this.workDir}`, colors.gray);
    this.log(`  SSH port: ${this.sshPort}`, colors.gray);
    this.log(`  Mode: ${this.persistent ? 'Persistent (changes saved)' : 'Ephemeral (changes discarded)'}`, 
      this.persistent ? colors.yellow : colors.green);
    
    const qemuCmd = this.buildQemuCommand(acceleration);
    
    if (this.verbose) {
      this.log(`  Command: ${qemuCmd.join(' ')}`, colors.gray);
    }
    
    return new Promise((resolve, reject) => {
      const stdio = this.interactive ? 'inherit' : 'ignore';
      
      this.vmProcess = spawn(qemuCmd[0], qemuCmd.slice(1), { stdio });

      this.vmProcess.on('error', (err) => {
        reject(new Error(`Failed to start VM: ${err.message}`));
      });

      this.vmProcess.on('exit', (code) => {
        if (code !== 0 && code !== null) {
          this.log(`VM exited with code ${code}`, colors.yellow);
        }
      });

      // Give VM time to start booting
      setTimeout(resolve, 3000);
    });
  }

  async connectSSH() {
    if (this.direct) {
      // No output in direct mode
    } else if (!this.verbose) {
      process.stdout.write(' connecting...');
    } else {
      this.log('⏳ Connecting to VM...', colors.blue);
    }
    
    // Detect the username from the cloud-init data
    let username = 'ubuntu'; // default
    try {
      const userDataPath = path.join(this.vmPath, 'cloud-init-files', 'user-data');
      const userData = await fs.readFile(userDataPath, 'utf8');
      const match = userData.match(/name:\s*(\w+)/);
      if (match) {
        username = match[1];
      }
    } catch {
      // Use default
    }
    
    const sshConfig = {
      host: 'localhost',
      port: this.sshPort,
      username: username,
      privateKey: await fs.readFile(this.sshKeyPath),
      readyTimeout: CONFIG.sshTimeout,
      reconnect: false,
      keepaliveInterval: 5000,
      algorithms: {
        serverHostKey: ['rsa-sha2-512', 'rsa-sha2-256', 'ssh-rsa']
      }
    };

    this.ssh = new SSH2Promise(sshConfig);

    const maxRetries = 30; // 30 seconds with 1-second intervals
    let retries = 0;

    while (retries < maxRetries) {
      try {
        await this.ssh.connect();
        
        // Test the connection with a simple command
        await this.ssh.exec('echo "SSH_OK"');
        
        if (this.direct) {
          // No output in direct mode
        } else if (!this.verbose) {
          process.stdout.write(' ready.\n');
        } else {
          this.log('✓ Connected to VM', colors.green);
        }
        
        // Mount the work directory
        await this.mountWorkDirectory();
        
        return;
      } catch (err) {
        retries++;
        if (retries % 10 === 0) {
          this.log(`  Still connecting... (${retries}s)`, colors.gray);
        }
        
        // Close any partial connection before retrying
        try {
          this.ssh.close();
        } catch {}
        
        // Recreate the SSH object for next attempt
        this.ssh = new SSH2Promise(sshConfig);
        
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
    }
    
    throw new Error('Failed to connect to VM via SSH');
  }

  async mountWorkDirectory() {
    try {
      // Create mount point
      await this.ssh.exec('sudo mkdir -p /mnt/work');
      
      // Mount 9p filesystem
      const mountCmd = 'sudo mount -t 9p -o trans=virtio,version=9p2000.L workdir /mnt/work';
      await this.ssh.exec(mountCmd);
      
      this.log('✓ Mounted work directory', colors.green);
    } catch (err) {
      this.log('⚠️  Could not mount work directory (9p not available)', colors.yellow);
      // Continue without mounting - user can still work in VM
    }
  }

  async executeCommand() {
    if (!this.command) {
      return;
    }

    this.log(`\n📝 Executing command...`, colors.blue);
    
    try {
      // Change to work directory if mounted
      const cdCmd = 'cd /mnt/work 2>/dev/null || cd ~';
      
      // Add non-interactive environment if requested
      let envPrefix = '';
      if (this.nonInteractive) {
        envPrefix = 'DEBIAN_FRONTEND=noninteractive DEBCONF_NONINTERACTIVE_SEEN=true ';
        this.log('  Using non-interactive mode', colors.gray);
      }
      
      const fullCommand = `${cdCmd} && ${envPrefix}${this.command}`;
      
      if (this.verbose) {
        this.log(`  Command: ${fullCommand}`, colors.gray);
      }
      
      const result = await this.ssh.exec(fullCommand);
      
      // Handle different return formats from SSH2Promise
      let stdout = '';
      let stderr = '';
      let code = 0;
      
      if (typeof result === 'string') {
        stdout = result;
        // For string results, assume success unless error indicators
        if (stdout.toLowerCase().includes('command not found') || 
            stdout.toLowerCase().includes('permission denied')) {
          code = 1;
        }
      } else if (result && typeof result === 'object') {
        stdout = result.stdout || '';
        stderr = result.stderr || '';
        code = result.code !== undefined ? result.code : 0;
      }
      
      // Print output (this is what user wants to see)
      if (stdout) {
        console.log(stdout);
      }
      
      if (stderr) {
        console.error(stderr);
      }
      
      if (code !== 0) {
        this.log(`\nCommand exited with code ${code}`, colors.yellow);
      }
      
      return code;
    } catch (err) {
      let errorMessage = 'Unknown error';
      
      if (err && err.message) {
        errorMessage = err.message;
      } else if (typeof err === 'string') {
        errorMessage = err;
      } else if (err) {
        errorMessage = JSON.stringify(err);
      }
      
      this.error(`Command execution failed: ${errorMessage}`);
      return 1;
    }
  }

  async runInteractiveShell() {
    if (!this.direct) {
      this.log('\n🐚 Starting interactive shell...', colors.blue);
      this.log('  (Type "exit" to quit)', colors.gray);
    }
    
    // Use SSH shell method for interactive session
    const shell = await this.ssh.shell();
    
    // Set up shell environment
    shell.write('cd /mnt/work 2>/dev/null || cd ~\n');
    shell.write('PS1="scratchpad:\\w\\$ "\n');
    shell.write('clear\n');
    
    // Pipe stdin/stdout
    process.stdin.setRawMode(true);
    process.stdin.pipe(shell);
    shell.pipe(process.stdout);
    
    return new Promise((resolve) => {
      shell.on('close', () => {
        process.stdin.setRawMode(false);
        process.stdin.unpipe(shell);
        shell.unpipe(process.stdout);
        resolve(0);
      });
    });
  }

  async stopVM() {
    if (!this.direct) {
      this.log('\n🛑 Stopping VM...', colors.yellow);
    }
    
    if (this.ssh) {
      try {
        // Try graceful shutdown
        await this.ssh.exec('sudo poweroff');
      } catch {
        // SSH connection will be closed by poweroff
      }
      
      this.ssh.close();
    }
    
    // Wait for VM to stop
    if (this.vmProcess) {
      await new Promise(resolve => {
        this.vmProcess.on('exit', resolve);
        setTimeout(() => {
          if (!this.vmProcess.killed) {
            this.vmProcess.kill('SIGTERM');
          }
          resolve();
        }, 5000);
      });
    }
    
    if (!this.direct) {
      this.log('✓ VM stopped', colors.green);
    }
  }

  async run() {
    try {
      // Check VM exists
      if (!await this.checkVMExists()) {
        return 1;
      }
      
      // Start VM
      await this.startVM();
      
      // Connect SSH
      await this.connectSSH();
      
      // Execute command or start shell
      let exitCode = 0;
      if (this.interactive) {
        exitCode = await this.runInteractiveShell();
      } else if (this.command) {
        exitCode = await this.executeCommand();
      }
      
      // Stop VM unless keep-alive
      if (!this.keepAlive) {
        await this.stopVM();
      } else if (!this.direct) {
        this.log(`\n✓ VM still running on SSH port ${this.sshPort}`, colors.green);
        this.log(`  Connect with: ssh -p ${this.sshPort} -i ${this.sshKeyPath} ubuntu@localhost`, colors.gray);
      }
      
      return exitCode;
      
    } catch (err) {
      this.error(err.message);
      
      // Cleanup on error
      if (this.vmProcess) {
        await this.stopVM();
      }
      
      return 1;
    }
  }
}

// CLI argument parsing
function parseArgs(args) {
  const options = {
    command: null,
    vm: CONFIG.defaultVM,
    memory: CONFIG.defaultMemory,
    workDir: process.cwd(),
    interactive: false,
    verbose: false,
    keepAlive: false,
    nonInteractive: false,
    persistent: false,
    direct: false,
    debug: false,
  };

  let i = 0;
  while (i < args.length) {
    const arg = args[i];
    
    switch (arg) {
      case 'shell':
        options.interactive = true;
        break;
      
      case 'run':
        // Process remaining args after 'run'
        i++;
        while (i < args.length) {
          const nextArg = args[i];
          if (nextArg.startsWith('-')) {
            // Process flag
            switch (nextArg) {
              case '--vm':
                options.vm = args[++i];
                break;
              case '--memory':
              case '-m':
                options.memory = args[++i];
                break;
              case '--dir':
              case '-d':
                options.workDir = args[++i];
                break;
              case '--verbose':
              case '-v':
                options.verbose = true;
                break;
              case '--keep-alive':
              case '-k':
                options.keepAlive = true;
                break;
              case '--non-interactive':
              case '-n':
                options.nonInteractive = true;
                break;
              case '--persistent':
              case '-p':
                options.persistent = true;
                break;
              case '--direct':
                options.direct = true;
                break;
              case '--debug':
                options.debug = true;
                break;
            }
          } else {
            // This is the command
            options.command = args.slice(i).join(' ');
            return options;
          }
          i++;
        }
        return options;
      
      case '--vm':
        options.vm = args[++i];
        break;
      
      case '--memory':
      case '-m':
        options.memory = args[++i];
        break;
      
      case '--dir':
      case '-d':
        options.workDir = args[++i];
        break;
      
      case '--verbose':
      case '-v':
        options.verbose = true;
        break;
      
      case '--keep-alive':
      case '-k':
        options.keepAlive = true;
        break;
      
      case '--non-interactive':
      case '-n':
        options.nonInteractive = true;
        break;
      
      case '--persistent':
      case '-p':
        options.persistent = true;
        break;
      
      case '--direct':
        options.direct = true;
        break;
      
      case '--debug':
        options.debug = true;
        break;
      
      case '--help':
      case '-h':
        showHelp();
        process.exit(0);
        break;
      
      default:
        // If it's not a flag and we haven't set a mode, treat as command
        if (!arg.startsWith('-') && !options.interactive) {
          options.command = args.slice(i).join(' ');
          return options;
        }
        break;
    }
    i++;
  }

  return options;
}

function showHelp() {
  console.log(`
${colors.bright}Scratchpad CLI - Run Commands in VM${colors.reset}

${colors.blue}Usage:${colors.reset}
  scratchpad run [options] <command>    Execute a command in VM
  scratchpad shell [options]            Start interactive shell
  scratchpad [options] <command>        Execute command (shorthand)

${colors.blue}Options:${colors.reset}
  --vm <n>         Use specific VM (default: 'default')
  -m, --memory <size> Memory allocation (default: '512M')
  -d, --dir <path>    Directory to mount (default: current)
  -k, --keep-alive    Keep VM running after command
  -n, --non-interactive  Run command in non-interactive mode
  -p, --persistent    Save changes to VM disk (default: ephemeral)
  -v, --verbose       Show startup/connection details
  --direct            Show only command output (no VM messages)
  --debug             Show debug information during parsing and execution
  -h, --help          Show this help

${colors.blue}VM Modes:${colors.reset}
  ${colors.green}Ephemeral (default):${colors.reset} Changes are discarded when VM stops
    - Perfect for testing and experimentation
    - VM always returns to original state
    - Example: scratchpad run "sudo apt-get install git"

  ${colors.yellow}Persistent (-p):${colors.reset} Changes are saved permanently
    - Use when you want to modify the VM permanently
    - Installs software, creates files, etc.
    - Example: scratchpad run -p "sudo apt-get install git"

${colors.blue}Output Modes:${colors.reset}
  ${colors.bright}Direct (--direct):${colors.reset} Pure command output only
    scratchpad run --direct "python3 --version"  # Shows: Python 3.10.12
    
  ${colors.green}Default:${colors.reset} Minimal VM messages + command output
    scratchpad run "python3 --version"  # Shows: Starting VM... ready.\\nPython 3.10.12
    
  ${colors.blue}Verbose (-v):${colors.reset} Full VM startup details + command output
    scratchpad run -v "python3 --version"  # Shows all startup messages + Python 3.10.12

${colors.blue}Examples:${colors.reset}
  # Pure command output (direct mode)
  scratchpad run --direct "python3 --version"
  
  # Default mode with minimal messages
  scratchpad run "python3 --version"
  
  # Verbose mode with full details
  scratchpad run -v "python3 --version"

  # Install permanently with direct output
  scratchpad run --direct -p "sudo apt-get install -y nodejs"

  # Interactive shell
  scratchpad shell

  # Use specific VM
  scratchpad run --vm datascience "jupyter --version"

${colors.blue}Non-Interactive Mode:${colors.reset}
  Use -n/--non-interactive when running commands that might prompt:
  - Package installation: sudo apt-get install
  - Configuration commands
  - Any command that might ask for user input

${colors.blue}Prepared VMs:${colors.reset}
  First create a VM with scratchpad-prepare:
    scratchpad-prepare --name myvm python3 nodejs

  Then use it:
    scratchpad run --vm myvm python3 app.py
`);
}

async function listVMs() {
  console.log(`${colors.bright}Available VMs:${colors.reset}\n`);
  
  try {
    const vms = await fs.readdir(CONFIG.vmDir);
    
    for (const vmName of vms) {
      const vmPath = path.join(CONFIG.vmDir, vmName);
      const stat = await fs.stat(vmPath);
      
      if (stat.isDirectory()) {
        try {
          const diskPath = path.join(vmPath, 'disk.qcow2');
          await fs.access(diskPath);
          
          console.log(`  ${colors.green}•${colors.reset} ${vmName}`);
          
          // Try to read prep info
          try {
            const prepInfoPath = path.join(vmPath, 'scratchpad-prepared.json');
            const prepInfo = JSON.parse(await fs.readFile(prepInfoPath, 'utf8'));
            console.log(`    Base: ${prepInfo.baseImage}, Packages: ${prepInfo.packages.join(', ')}`);
          } catch {
            // No prep info
          }
        } catch {
          // No disk file
        }
      }
    }
  } catch {
    console.log('  No VMs found. Create one with scratchpad-prepare');
  }
}

// Main execution
async function main() {
  const args = process.argv.slice(2);
  
  if (args.length === 0 || args.includes('--help') || args.includes('-h')) {
    showHelp();
    process.exit(0);
  }
  
  if (args[0] === 'list') {
    await listVMs();
    process.exit(0);
  }
  
  const options = parseArgs(args);
  
  if (!options.command && !options.interactive) {
    showHelp();
    process.exit(1);
  }
  
  const cli = new ScratchpadCLI(options);
  const exitCode = await cli.run();
  process.exit(exitCode);
}

if (require.main === module) {
  main();
}

module.exports = { ScratchpadCLI };
