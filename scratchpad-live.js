#!/usr/bin/env node

const { spawn, execSync } = require('child_process');
const fs = require('fs').promises;
const fsSync = require('fs');
const path = require('path');
const os = require('os');
const crypto = require('crypto');
const net = require('net');
const SSH2Promise = require('ssh2-promise');

// Configuration
const CONFIG = {
  baseDir: path.join(os.homedir(), '.scratchpad', 'live'),
  registryFile: path.join(os.homedir(), '.scratchpad', 'live', 'registry.json'),
  pidDir: path.join(os.homedir(), '.scratchpad', 'live', 'pids'),
  logDir: path.join(os.homedir(), '.scratchpad', 'live', 'logs'),
  vmDir: path.join(os.homedir(), '.scratchpad', 'vms'),
  sshKeysDir: path.join(os.homedir(), '.scratchpad', 'keys'),
  sshPortRange: { start: 2222, end: 9999 },
  vncPortRange: { start: 5900, end: 5999 },
  sshTimeout: 60000,
  gracefulShutdownTimeout: 30000,
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

// Status indicators
const statusIcons = {
  running: `${colors.green}●${colors.reset}`,
  stopped: `${colors.gray}○${colors.reset}`,
  crashed: `${colors.red}✗${colors.reset}`,
  starting: `${colors.yellow}◐${colors.reset}`,
};

// VM Registry - manages persistent state
class VMRegistry {
  constructor() {
    this.registry = { version: '1.0', vms: [], lastUpdated: null };
  }

  async load() {
    try {
      const data = await fs.readFile(CONFIG.registryFile, 'utf8');
      this.registry = JSON.parse(data);
    } catch (err) {
      // Registry doesn't exist yet, use empty
      this.registry = { version: '1.0', vms: [], lastUpdated: null };
    }
  }

  async save() {
    this.registry.lastUpdated = new Date().toISOString();
    await fs.mkdir(path.dirname(CONFIG.registryFile), { recursive: true });
    await fs.writeFile(CONFIG.registryFile, JSON.stringify(this.registry, null, 2));
  }

  async register(vm) {
    await this.load();
    // Remove any existing VM with same name
    this.registry.vms = this.registry.vms.filter(v => v.name !== vm.name);
    this.registry.vms.push(vm);
    await this.save();
  }

  async unregister(name) {
    await this.load();
    this.registry.vms = this.registry.vms.filter(v => v.name !== name);
    await this.save();
  }

  async get(name) {
    await this.load();
    return this.registry.vms.find(v => v.name === name);
  }

  async list(filter) {
    await this.load();
    if (filter) {
      return this.registry.vms.filter(v => v.status === filter);
    }
    return this.registry.vms;
  }

  async updateStatus(name, status) {
    await this.load();
    const vm = this.registry.vms.find(v => v.name === name);
    if (vm) {
      vm.status = status;
      vm.lastVerified = new Date().toISOString();
      await this.save();
    }
  }

  async verifyAndUpdate() {
    await this.load();
    
    for (const vm of this.registry.vms) {
      if (vm.status === 'running' || vm.status === 'starting') {
        const isRunning = await ProcessManager.isProcessRunning(vm.pid);
        if (!isRunning) {
          vm.status = 'crashed';
          vm.lastVerified = new Date().toISOString();
        }
      }
    }
    
    await this.save();
  }

  async cleanupCrashed() {
    await this.load();
    const crashed = this.registry.vms.filter(v => v.status === 'crashed');
    this.registry.vms = this.registry.vms.filter(v => v.status !== 'crashed');
    await this.save();
    return crashed.map(v => v.name);
  }
}

// Process Manager - handles VM lifecycle
class ProcessManager {
  static async spawnDetached(config) {
    // Create log directory
    const logFile = path.join(CONFIG.logDir, config.name, 'qemu.log');
    await fs.mkdir(path.dirname(logFile), { recursive: true });
    
    // Build QEMU command
    const qemuCmd = await this.buildQemuCommand(config);
    
    // Create a shell script to spawn the process
    const scriptContent = `#!/bin/bash
exec nohup ${qemuCmd.join(' ')} > "${logFile}" 2>&1 &
echo $!
`;
    
    const scriptPath = path.join(CONFIG.pidDir, `${config.name}-launch.sh`);
    await fs.mkdir(CONFIG.pidDir, { recursive: true });
    await fs.writeFile(scriptPath, scriptContent, { mode: 0o755 });
    
    // Execute the script to get PID
    return new Promise((resolve, reject) => {
      const child = spawn('bash', [scriptPath], {
        detached: true,
        stdio: ['ignore', 'pipe', 'pipe']
      });
      
      let pid = '';
      let error = '';
      
      child.stdout.on('data', (data) => {
        pid += data.toString();
      });
      
      child.stderr.on('data', (data) => {
        error += data.toString();
      });
      
      child.on('close', async (code) => {
        // Clean up launch script
        try {
          await fs.unlink(scriptPath);
        } catch {}
        
        if (code !== 0 || !pid.trim()) {
          reject(new Error(`Failed to spawn VM: ${error || 'Unknown error'}`));
          return;
        }
        
        const processInfo = {
          pid: parseInt(pid.trim()),
          method: 'detached',
          logFile: logFile,
          startedAt: new Date().toISOString()
        };
        
        // Save PID file
        await fs.writeFile(
          path.join(CONFIG.pidDir, `${config.name}.pid`),
          processInfo.pid.toString()
        );
        
        // Verify process started
        await new Promise(resolve => setTimeout(resolve, 1000));
        const running = await this.isProcessRunning(processInfo.pid);
        
        if (!running) {
          reject(new Error('Process died immediately after starting'));
          return;
        }
        
        resolve(processInfo);
      });
      
      child.unref();
    });
  }

  static async buildQemuCommand(config) {
    const vmPath = path.join(CONFIG.vmDir, config.baseVm);
    const diskPath = path.join(vmPath, 'disk.qcow2');
    const cloudInitPath = path.join(vmPath, 'cloud-init.iso');
    
    // Check if base VM exists
    try {
      await fs.access(diskPath);
    } catch {
      throw new Error(`Base VM '${config.baseVm}' not found. Create it with: scratchpad-prepare --name ${config.baseVm}`);
    }
    
    // Detect acceleration
    const acceleration = await this.detectAcceleration();
    
    const cmd = [
      'qemu-system-x86_64',
      '-name', `live-${config.name}`,
      '-machine', 'pc',
      '-m', config.memory,
      '-accel', acceleration,
      '-cpu', 'host',
      '-smp', '2',
      
      // Disk - ephemeral or persistent
      '-drive', config.diskMode === 'persistent' 
        ? `file=${diskPath},format=qcow2,if=virtio`
        : `file=${diskPath},format=qcow2,if=virtio,snapshot=on`,
      
      // Cloud-init if exists
    ];
    
    // Add cloud-init if available
    try {
      await fs.access(cloudInitPath);
      cmd.push('-drive', `file=${cloudInitPath},format=raw,if=virtio`);
    } catch {}
    
    // Network
    cmd.push('-netdev', `user,id=net0,hostfwd=tcp::${config.sshPort}-:22`);
    cmd.push('-device', 'virtio-net-pci,netdev=net0');
    
    // VNC if enabled
    if (config.vncEnabled && config.vncPort) {
      cmd.push('-vnc', `:${config.vncPort - 5900}`);
    } else {
      cmd.push('-display', 'none');
    }
    
    // Work directory mount if specified
    if (config.workDir) {
      cmd.push('-virtfs', `local,path=${config.workDir},mount_tag=workdir,security_model=mapped-xattr,id=workdir`);
    }
    
    // Serial output for monitoring
    cmd.push('-serial', 'file:' + path.join(CONFIG.logDir, config.name, 'serial.log'));
    cmd.push('-monitor', 'none');
    
    return cmd;
  }

  static async detectAcceleration() {
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

  static async isProcessRunning(pid) {
    try {
      // Check if process exists
      process.kill(pid, 0);
      
      // Verify it's a QEMU process
      if (process.platform === 'linux' || process.platform === 'darwin') {
        try {
          const cmdline = await fs.readFile(`/proc/${pid}/cmdline`, 'utf8').catch(() => '');
          if (cmdline.includes('qemu-system')) {
            return true;
          }
          
          // Fallback for macOS
          const psOutput = execSync(`ps -p ${pid} -o command=`, { encoding: 'utf8' });
          return psOutput.includes('qemu-system');
        } catch {
          return true; // Process exists but can't verify type
        }
      }
      
      return true;
    } catch {
      return false;
    }
  }

  static async stopProcess(vm) {
    if (vm.spawnMethod === 'detached') {
      // Try graceful shutdown via SSH first
      try {
        await this.gracefulShutdown(vm);
        return;
      } catch (err) {
        // Continue with forceful shutdown
        console.log(`${colors.yellow}Graceful shutdown failed, forcing...${colors.reset}`);
      }
      
      // Force kill
      try {
        process.kill(vm.pid, 'SIGTERM');
        await new Promise(resolve => setTimeout(resolve, 5000));
        
        if (await this.isProcessRunning(vm.pid)) {
          process.kill(vm.pid, 'SIGKILL');
        }
      } catch {}
      
      // Clean up PID file
      try {
        await fs.unlink(path.join(CONFIG.pidDir, `${vm.name}.pid`));
      } catch {}
    }
  }

  static async gracefulShutdown(vm) {
    const sshKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa');
    const username = await this.detectUsername(vm.baseVm);
    
    const ssh = new SSH2Promise({
      host: 'localhost',
      port: vm.sshPort,
      username: username,
      privateKey: await fs.readFile(sshKeyPath),
      readyTimeout: 5000,
      algorithms: {
        serverHostKey: ['rsa-sha2-512', 'rsa-sha2-256', 'ssh-rsa']
      }
    });
    
    try {
      await ssh.connect();
      await ssh.exec('sudo poweroff');
      ssh.close();
      
      // Wait for process to exit
      const timeout = Date.now() + CONFIG.gracefulShutdownTimeout;
      while (await this.isProcessRunning(vm.pid) && Date.now() < timeout) {
        await new Promise(resolve => setTimeout(resolve, 1000));
      }
      
      if (!await this.isProcessRunning(vm.pid)) {
        return; // Success
      }
    } catch (err) {
      throw new Error(`SSH shutdown failed: ${err.message}`);
    }
  }

  static async detectUsername(baseVm) {
    // Try to detect from cloud-init
    try {
      const userDataPath = path.join(CONFIG.vmDir, baseVm, 'cloud-init-files', 'user-data');
      const userData = await fs.readFile(userDataPath, 'utf8');
      const match = userData.match(/name:\s*(\w+)/);
      if (match) {
        return match[1];
      }
    } catch {}
    
    // Default based on common patterns
    if (baseVm.includes('ubuntu')) return 'ubuntu';
    if (baseVm.includes('debian')) return 'debian';
    if (baseVm.includes('alpine')) return 'alpine';
    return 'ubuntu'; // fallback
  }
}

// Port Allocator
class PortAllocator {
  static async allocateSSHPort() {
    const registry = new VMRegistry();
    const vms = await registry.list();
    const usedPorts = new Set(vms.map(vm => vm.sshPort).filter(p => p));
    
    // Start from random port in range
    const start = CONFIG.sshPortRange.start + Math.floor(Math.random() * 1000);
    
    for (let port = start; port <= CONFIG.sshPortRange.end; port++) {
      if (!usedPorts.has(port) && await this.isPortAvailable(port)) {
        return port;
      }
    }
    
    // Wrap around
    for (let port = CONFIG.sshPortRange.start; port < start; port++) {
      if (!usedPorts.has(port) && await this.isPortAvailable(port)) {
        return port;
      }
    }
    
    throw new Error('No available SSH ports');
  }

  static async isPortAvailable(port) {
    return new Promise((resolve) => {
      const server = net.createServer();
      
      server.once('error', () => {
        resolve(false);
      });
      
      server.once('listening', () => {
        server.close();
        resolve(true);
      });
      
      server.listen(port, 'localhost');
    });
  }
}

// Health Monitor
class HealthMonitor {
  static async checkAllVMs() {
    const registry = new VMRegistry();
    await registry.verifyAndUpdate();
    
    const vms = await registry.list();
    const report = {
      totalVMs: vms.length,
      running: 0,
      stopped: 0,
      crashed: 0,
      warnings: []
    };
    
    for (const vm of vms) {
      switch (vm.status) {
        case 'running':
          report.running++;
          break;
        case 'stopped':
          report.stopped++;
          break;
        case 'crashed':
          report.crashed++;
          report.warnings.push(`VM '${vm.name}' has crashed`);
          break;
      }
    }
    
    return report;
  }

  static async checkVM(name) {
    const registry = new VMRegistry();
    const vm = await registry.get(name);
    
    if (!vm) {
      throw new Error(`VM '${name}' not found`);
    }
    
    const health = {
      name: vm.name,
      status: vm.status,
      checksPassed: {},
      uptime: null
    };
    
    // Check process exists
    if (vm.status === 'running') {
      health.checksPassed.processExists = await ProcessManager.isProcessRunning(vm.pid);
      
      if (health.checksPassed.processExists) {
        // Calculate uptime
        const created = new Date(vm.createdAt);
        const now = new Date();
        const uptimeMs = now - created;
        health.uptime = this.formatDuration(uptimeMs);
        
        // Check SSH connectivity
        health.checksPassed.sshResponsive = await this.checkSSH(vm);
        
        // Check disk accessible
        const diskPath = path.join(CONFIG.vmDir, vm.baseVm, 'disk.qcow2');
        health.checksPassed.diskAccessible = await fs.access(diskPath).then(() => true).catch(() => false);
      }
    }
    
    return health;
  }

  static async checkSSH(vm) {
    try {
      const port = await PortAllocator.isPortAvailable(vm.sshPort);
      return !port; // Port should be in use
    } catch {
      return false;
    }
  }

  static formatDuration(ms) {
    const seconds = Math.floor(ms / 1000);
    const minutes = Math.floor(seconds / 60);
    const hours = Math.floor(minutes / 60);
    const days = Math.floor(hours / 24);
    
    if (days > 0) {
      return `${days}d ${hours % 24}h`;
    } else if (hours > 0) {
      return `${hours}h ${minutes % 60}m`;
    } else if (minutes > 0) {
      return `${minutes}m ${seconds % 60}s`;
    } else {
      return `${seconds}s`;
    }
  }
}

// CLI Implementation
class ScratchpadLiveCLI {
  constructor() {
    this.registry = new VMRegistry();
  }

  async spawn(args) {
    const config = this.parseSpawnArgs(args);
    
    // Validate base VM
    const vmPath = path.join(CONFIG.vmDir, config.baseVm);
    try {
      await fs.access(vmPath);
    } catch {
      this.error(`Base VM '${config.baseVm}' not found. Create it with: scratchpad-prepare --name ${config.baseVm}`);
      return 1;
    }
    
    // Check if name already exists
    const existing = await this.registry.get(config.name);
    if (existing) {
      this.error(`VM '${config.name}' already exists (status: ${existing.status})`);
      console.log(`Use 'scratchpad-live connect ${config.name}' to connect to it`);
      return 1;
    }
    
    try {
      console.log(`${colors.blue}Spawning VM '${config.name}'...${colors.reset}`);
      
      // Allocate port
      config.sshPort = await PortAllocator.allocateSSHPort();
      
      // Spawn the VM
      const processInfo = await ProcessManager.spawnDetached(config);
      
      // Create VM instance
      const vm = {
        id: crypto.randomUUID(),
        name: config.name,
        baseVm: config.baseVm,
        sshPort: config.sshPort,
        vncPort: config.vncPort,
        pid: processInfo.pid,
        status: 'starting',
        spawnMethod: processInfo.method,
        createdAt: processInfo.startedAt,
        lastVerified: processInfo.startedAt,
        memory: config.memory,
        diskMode: config.diskMode,
        logFile: processInfo.logFile,
        workDir: config.workDir
      };
      
      // Register VM
      await this.registry.register(vm);
      
      // Wait for SSH
      console.log(`${colors.blue}Waiting for SSH...${colors.reset}`);
      const sshReady = await this.waitForSSH(vm);
      
      if (sshReady) {
        await this.registry.updateStatus(config.name, 'running');
        
        const sshKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa');
        const username = await ProcessManager.detectUsername(config.baseVm);
        
        console.log(`\n${colors.green}✓ VM '${config.name}' started successfully${colors.reset}`);
        console.log(`${colors.bright}SSH Port:${colors.reset} ${config.sshPort}`);
        console.log(`${colors.bright}Connect:${colors.reset} scratchpad-live connect ${config.name}`);
        console.log(`${colors.bright}SSH:${colors.reset} ssh -p ${config.sshPort} -i ${sshKeyPath} ${username}@localhost`);
        console.log(`${colors.bright}Logs:${colors.reset} ${processInfo.logFile}`);
        
        if (config.diskMode === 'persistent') {
          console.log(`${colors.yellow}⚠️  Persistent mode - changes will be saved${colors.reset}`);
        }
      } else {
        await this.registry.updateStatus(config.name, 'crashed');
        throw new Error('VM started but SSH never became available');
      }
      
      return 0;
    } catch (err) {
      this.error(`Failed to spawn VM: ${err.message}`);
      
      // Clean up on failure
      try {
        await this.registry.unregister(config.name);
      } catch {}
      
      return 1;
    }
  }

  async list(args) {
    const showAll = args.includes('--all');
    const runHealth = args.includes('--health');
    const jsonOutput = args.includes('--json');
    const quiet = args.includes('--quiet') || args.includes('-q');
    
    if (runHealth) {
      await this.registry.verifyAndUpdate();
    }
    
    const vms = await this.registry.list();
    const filtered = showAll ? vms : vms.filter(v => v.status === 'running' || v.status === 'starting');
    
    if (jsonOutput) {
      console.log(JSON.stringify(filtered, null, 2));
      return 0;
    }
    
    if (quiet) {
      filtered.forEach(vm => console.log(vm.name));
      return 0;
    }
    
    if (filtered.length === 0) {
      console.log('No VMs running');
      if (!showAll && vms.length > 0) {
        console.log(`(${vms.length} stopped VMs, use --all to show)`);
      }
      return 0;
    }
    
    // Table header
    const headers = ['NAME', 'BASE', 'STATUS', 'PORT', 'UPTIME', 'MEM', 'METHOD'];
    const widths = [20, 15, 10, 6, 12, 6, 10];
    
    // Print header
    console.log(headers.map((h, i) => h.padEnd(widths[i])).join(' '));
    console.log(widths.map(w => '-'.repeat(w - 1)).join(' '));
    
    // Print VMs
    for (const vm of filtered) {
      const icon = statusIcons[vm.status] || ' ';
      const status = `${icon} ${vm.status.substring(0, 7)}`;
      const uptime = vm.status === 'running' ? HealthMonitor.formatDuration(Date.now() - new Date(vm.createdAt)) : '-';
      const disk = vm.diskMode === 'persistent' ? '*' : '';
      
      const row = [
        vm.name.substring(0, 19),
        vm.baseVm.substring(0, 14),
        status,
        vm.sshPort.toString(),
        uptime,
        vm.memory,
        vm.spawnMethod + disk
      ];
      
      console.log(row.map((c, i) => c.padEnd(widths[i])).join(' '));
    }
    
    if (filtered.some(vm => vm.diskMode === 'persistent')) {
      console.log(`\n${colors.gray}* = persistent disk${colors.reset}`);
    }
    
    return 0;
  }

  async connect(args) {
    const name = args[0];
    if (!name) {
      this.error('Usage: scratchpad-live connect <name>');
      return 1;
    }
    
    const vm = await this.registry.get(name);
    if (!vm) {
      this.error(`VM '${name}' not found`);
      return 1;
    }
    
    if (vm.status === 'stopped') {
      console.log(`VM '${name}' is stopped. Would you like to start it? (y/N)`);
      // TODO: Implement restart functionality
      return 1;
    }
    
    if (vm.status === 'crashed') {
      console.log(`${colors.red}VM '${name}' has crashed${colors.reset}`);
      console.log(`Check logs at: ${vm.logFile}`);
      return 1;
    }
    
    // Update last verified
    await this.registry.updateStatus(name, 'running');
    
    // Connect via SSH
    const sshKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa');
    const username = await ProcessManager.detectUsername(vm.baseVm);
    
    console.log(`${colors.blue}Connecting to '${name}'...${colors.reset}`);
    
    // Use system SSH client for interactive session
    const sshArgs = [
      '-p', vm.sshPort.toString(),
      '-i', sshKeyPath,
      '-o', 'StrictHostKeyChecking=no',
      '-o', 'UserKnownHostsFile=/dev/null',
      `${username}@localhost`
    ];
    
    return new Promise((resolve) => {
      const ssh = spawn('ssh', sshArgs, {
        stdio: 'inherit'
      });
      
      ssh.on('exit', (code) => {
        resolve(code || 0);
      });
    });
  }

  async stop(args) {
    const force = args.includes('--force');
    const stopAll = args.includes('--all');
    const keep = args.includes('--keep');
    
    let targets = [];
    
    if (stopAll) {
      const vms = await this.registry.list('running');
      targets = vms.map(vm => vm.name);
      
      if (targets.length === 0) {
        console.log('No VMs running');
        return 0;
      }
      
      if (!force) {
        console.log(`Stop ${targets.length} VMs? (y/N)`);
        // TODO: Implement confirmation
      }
    } else {
      const name = args.find(arg => !arg.startsWith('-'));
      if (!name) {
        this.error('Usage: scratchpad-live stop <name> [--force] [--all]');
        return 1;
      }
      targets = [name];
    }
    
    let failed = 0;
    
    for (const name of targets) {
      const vm = await this.registry.get(name);
      if (!vm) {
        this.error(`VM '${name}' not found`);
        failed++;
        continue;
      }
      
      if (vm.status !== 'running' && vm.status !== 'starting') {
        console.log(`VM '${name}' is not running (status: ${vm.status})`);
        continue;
      }
      
      try {
        console.log(`${colors.yellow}Stopping VM '${name}'...${colors.reset}`);
        await ProcessManager.stopProcess(vm);
        
        if (keep) {
          await this.registry.updateStatus(name, 'stopped');
        } else {
          await this.registry.unregister(name);
        }
        
        console.log(`${colors.green}✓ VM '${name}' stopped${colors.reset}`);
      } catch (err) {
        this.error(`Failed to stop '${name}': ${err.message}`);
        failed++;
      }
    }
    
    return failed > 0 ? 1 : 0;
  }

  async info(args) {
    const name = args[0];
    if (!name) {
      this.error('Usage: scratchpad-live info <name>');
      return 1;
    }
    
    const vm = await this.registry.get(name);
    if (!vm) {
      this.error(`VM '${name}' not found`);
      return 1;
    }
    
    const health = await HealthMonitor.checkVM(name);
    const sshKeyPath = path.join(CONFIG.sshKeysDir, 'id_rsa');
    const username = await ProcessManager.detectUsername(vm.baseVm);
    
    console.log(`\n${colors.bright}VM Information${colors.reset}`);
    console.log('─'.repeat(50));
    
    console.log(`${colors.bright}Basic:${colors.reset}`);
    console.log(`  Name:       ${vm.name}`);
    console.log(`  Base VM:    ${vm.baseVm}`);
    console.log(`  Status:     ${statusIcons[vm.status]} ${vm.status}`);
    console.log(`  Created:    ${new Date(vm.createdAt).toLocaleString()}`);
    
    console.log(`\n${colors.bright}Resources:${colors.reset}`);
    console.log(`  Memory:     ${vm.memory}`);
    console.log(`  Disk Mode:  ${vm.diskMode}`);
    if (vm.workDir) {
      console.log(`  Work Dir:   ${vm.workDir}`);
    }
    
    console.log(`\n${colors.bright}Network:${colors.reset}`);
    console.log(`  SSH Port:   ${vm.sshPort}`);
    if (vm.vncPort) {
      console.log(`  VNC Port:   ${vm.vncPort}`);
    }
    
    console.log(`\n${colors.bright}Process:${colors.reset}`);
    console.log(`  PID:        ${vm.pid}`);
    console.log(`  Method:     ${vm.spawnMethod}`);
    if (health.uptime) {
      console.log(`  Uptime:     ${health.uptime}`);
    }
    
    console.log(`\n${colors.bright}Files:${colors.reset}`);
    console.log(`  Log File:   ${vm.logFile}`);
    
    console.log(`\n${colors.bright}Connection:${colors.reset}`);
    console.log(`  SSH Command: ssh -p ${vm.sshPort} -i ${sshKeyPath} ${username}@localhost`);
    console.log(`  Quick Connect: scratchpad-live connect ${vm.name}`);
    
    if (Object.keys(health.checksPassed).length > 0) {
      console.log(`\n${colors.bright}Health Checks:${colors.reset}`);
      for (const [check, passed] of Object.entries(health.checksPassed)) {
        const icon = passed ? `${colors.green}✓${colors.reset}` : `${colors.red}✗${colors.reset}`;
        console.log(`  ${icon} ${check}`);
      }
    }
    
    return 0;
  }

  async health(args) {
    const fix = args.includes('--fix');
    
    console.log(`${colors.blue}Running health check...${colors.reset}\n`);
    
    const report = await HealthMonitor.checkAllVMs();
    
    console.log(`${colors.bright}VM Health Report${colors.reset}`);
    console.log('─'.repeat(30));
    console.log(`Total VMs:     ${report.totalVMs}`);
    console.log(`Running:       ${report.running} ${colors.green}●${colors.reset}`);
    console.log(`Stopped:       ${report.stopped} ${colors.gray}○${colors.reset}`);
    console.log(`Crashed:       ${report.crashed} ${colors.red}✗${colors.reset}`);
    
    if (report.warnings.length > 0) {
      console.log(`\n${colors.yellow}Warnings:${colors.reset}`);
      report.warnings.forEach(w => console.log(`  - ${w}`));
    }
    
    if (fix && report.crashed > 0) {
      console.log(`\n${colors.blue}Cleaning up crashed VMs...${colors.reset}`);
      const cleaned = await this.registry.cleanupCrashed();
      console.log(`Removed ${cleaned.length} crashed VMs from registry`);
    }
    
    // Check for orphaned processes
    // TODO: Implement orphan detection
    
    return 0;
  }

  async clean(args) {
    const force = args.includes('--force');
    
    if (!force) {
      console.log(`${colors.bright}This will clean up:${colors.reset}`);
      console.log('  - Stopped VMs from registry');
      console.log('  - Orphaned PID files');
      console.log('  - Old log files');
      console.log('\nProceed? (y/N)');
      // TODO: Implement confirmation
      return 0;
    }
    
    // Clean stopped VMs
    const vms = await this.registry.list();
    const stopped = vms.filter(vm => vm.status === 'stopped' || vm.status === 'crashed');
    
    for (const vm of stopped) {
      await this.registry.unregister(vm.name);
      console.log(`Removed '${vm.name}' from registry`);
    }
    
    // Clean orphaned PID files
    try {
      const pidFiles = await fs.readdir(CONFIG.pidDir);
      for (const file of pidFiles) {
        if (file.endsWith('.pid')) {
          const name = file.replace('.pid', '');
          const vm = await this.registry.get(name);
          if (!vm) {
            await fs.unlink(path.join(CONFIG.pidDir, file));
            console.log(`Removed orphaned PID file: ${file}`);
          }
        }
      }
    } catch {}
    
    console.log(`\n${colors.green}✓ Cleanup complete${colors.reset}`);
    return 0;
  }

  async waitForSSH(vm, timeout = 60000) {
    const start = Date.now();
    
    while (Date.now() - start < timeout) {
      const available = await PortAllocator.isPortAvailable(vm.sshPort);
      if (!available) {
        // Port is in use, likely SSH is up
        return true;
      }
      
      await new Promise(resolve => setTimeout(resolve, 1000));
    }
    
    return false;
  }

  parseSpawnArgs(args) {
    const config = {
      name: null,
      baseVm: null,
      memory: '512M',
      diskMode: 'ephemeral',
      vncEnabled: false,
      workDir: null,
      spawnMethod: 'detached'
    };
    
    let i = 0;
    while (i < args.length) {
      const arg = args[i];
      
      switch (arg) {
        case '--vm':
          config.baseVm = args[++i];
          break;
        case '--memory':
        case '-m':
          config.memory = args[++i];
          break;
        case '--persistent':
        case '-p':
          config.diskMode = 'persistent';
          break;
        case '--vnc':
          config.vncEnabled = true;
          break;
        case '--port':
          config.sshPort = parseInt(args[++i]);
          break;
        case '--dir':
          config.workDir = args[++i];
          break;
        case '--method':
          config.spawnMethod = args[++i];
          break;
        default:
          if (!arg.startsWith('-')) {
            if (!config.baseVm && arg.includes('--vm')) {
              // Handle: spawn --vm base name
              continue;
            }
            config.name = arg;
          }
          break;
      }
      i++;
    }
    
    if (!config.name) {
      throw new Error('VM name is required');
    }
    
    if (!config.baseVm) {
      config.baseVm = 'default';
    }
    
    return config;
  }

  error(message) {
    console.error(`${colors.red}❌ ${message}${colors.reset}`);
  }
}

// Main execution
async function main() {
  const args = process.argv.slice(2);
  
  if (args.length === 0 || args[0] === '--help' || args[0] === '-h') {
    showHelp();
    process.exit(0);
  }
  
  // Ensure directories exist
  await fs.mkdir(CONFIG.baseDir, { recursive: true });
  await fs.mkdir(CONFIG.pidDir, { recursive: true });
  await fs.mkdir(CONFIG.logDir, { recursive: true });
  
  const cli = new ScratchpadLiveCLI();
  let exitCode = 0;
  
  const command = args[0];
  const commandArgs = args.slice(1);
  
  switch (command) {
    case 'spawn':
      exitCode = await cli.spawn(commandArgs);
      break;
    
    case 'list':
      exitCode = await cli.list(commandArgs);
      break;
    
    case 'connect':
      exitCode = await cli.connect(commandArgs);
      break;
    
    case 'stop':
      exitCode = await cli.stop(commandArgs);
      break;
    
    case 'info':
      exitCode = await cli.info(commandArgs);
      break;
    
    case 'health':
      exitCode = await cli.health(commandArgs);
      break;
    
    case 'clean':
      exitCode = await cli.clean(commandArgs);
      break;
    
    default:
      console.error(`Unknown command: ${command}`);
      showHelp();
      exitCode = 1;
  }
  
  process.exit(exitCode);
}

function showHelp() {
  console.log(`
${colors.bright}Scratchpad Live - Persistent VM Management${colors.reset}

${colors.blue}Usage:${colors.reset}
  scratchpad-live spawn --vm <base> <name> [options]    Spawn a new VM
  scratchpad-live list [--all] [--health]               List VMs
  scratchpad-live connect <name>                         Connect to VM
  scratchpad-live stop <name> [--force] [--all]         Stop VM(s)
  scratchpad-live info <name>                           Show VM details
  scratchpad-live health [--fix]                         Check VM health
  scratchpad-live clean [--force]                       Clean up resources

${colors.blue}Spawn Options:${colors.reset}
  --vm <base>           Base VM to use (default: 'default')
  -m, --memory <size>   Memory allocation (default: 512M)
  -p, --persistent      Save disk changes permanently
  --vnc                 Enable VNC display
  --port <num>          Use specific SSH port
  --dir <path>          Mount directory in VM
  --method <type>       Spawn method: detached|systemd|tmux

${colors.blue}Examples:${colors.reset}
  # Spawn multiple agents
  scratchpad-live spawn --vm myvm "agent-1" --memory 1G
  scratchpad-live spawn --vm myvm "agent-2" --memory 1G
  
  # List running VMs
  scratchpad-live list
  
  # Connect to a VM
  scratchpad-live connect agent-1
  
  # Stop a VM
  scratchpad-live stop agent-1
  
  # Persistent VM with mounted directory
  scratchpad-live spawn --vm devbox "project" -p --dir ./src

${colors.gray}VMs run in the background and persist across terminal sessions.${colors.reset}
`);
}

if (require.main === module) {
  main().catch(err => {
    console.error(`${colors.red}Fatal error: ${err.message}${colors.reset}`);
    if (process.env.DEBUG) {
      console.error(err.stack);
    }
    process.exit(1);
  });
}

module.exports = { VMRegistry, ProcessManager, PortAllocator, HealthMonitor };
