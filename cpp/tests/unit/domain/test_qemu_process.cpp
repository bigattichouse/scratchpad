#include <gtest/gtest.h>
#include "domain/process/qemu_process.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>

using namespace scratchpad;
using namespace scratchpad::test;

class QemuProcessTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_pid_ = 12345;
        test_command_line_ = {
            "qemu-system-x86_64",
            "-m", "1024",
            "-hda", "/tmp/test-vm.qcow2",
            "-netdev", "user,id=net0,hostfwd=tcp::2222-:22",
            "-device", "e1000,netdev=net0",
            "-enable-kvm",
            "-daemonize"
        };
    }
    
    ProcessId test_pid_;
    std::vector<std::string> test_command_line_;
};

// Construction tests
TEST_F(QemuProcessTest, ConstructionWithValidParameters) {
    EXPECT_NO_THROW({
        QemuProcess process(test_pid_, test_command_line_);
        EXPECT_EQ(process.process_id(), test_pid_);
        EXPECT_EQ(process.command_line(), test_command_line_);
        EXPECT_EQ(process.status(), ProcessStatus::Starting);
    });
}

TEST_F(QemuProcessTest, ConstructionWithInvalidPid) {
    ProcessId invalid_pid = 0;
    EXPECT_THROW({
        QemuProcess process(invalid_pid, test_command_line_);
    }, ScratchpadError);
    
    ProcessId negative_pid = -1;
    EXPECT_THROW({
        QemuProcess process(negative_pid, test_command_line_);
    }, ScratchpadError);
}

TEST_F(QemuProcessTest, ConstructionWithEmptyCommandLine) {
    std::vector<std::string> empty_command;
    EXPECT_THROW({
        QemuProcess process(test_pid_, empty_command);
    }, ScratchpadError);
}

// Basic property tests
TEST_F(QemuProcessTest, BasicProperties) {
    QemuProcess process(test_pid_, test_command_line_);
    
    EXPECT_EQ(process.process_id(), test_pid_);
    EXPECT_EQ(process.command_line(), test_command_line_);
    EXPECT_GT(process.creation_time(), std::chrono::system_clock::time_point{});
    EXPECT_GT(process.last_status_change(), std::chrono::system_clock::time_point{});
}

// Status management tests
TEST_F(QemuProcessTest, InitialStatus) {
    QemuProcess process(test_pid_, test_command_line_);
    
    EXPECT_EQ(process.status(), ProcessStatus::Starting);
    EXPECT_FALSE(process.is_running());
    EXPECT_FALSE(process.has_exited());
}

TEST_F(QemuProcessTest, StatusTransitions) {
    QemuProcess process(test_pid_, test_command_line_);
    
    auto before_change = std::chrono::system_clock::now();
    
    // Starting -> Running
    process.set_status(ProcessStatus::Running);
    EXPECT_EQ(process.status(), ProcessStatus::Running);
    EXPECT_TRUE(process.is_running());
    EXPECT_FALSE(process.has_exited());
    EXPECT_GE(process.last_status_change(), before_change);
    
    // Running -> Exited
    process.set_status(ProcessStatus::Exited);
    EXPECT_EQ(process.status(), ProcessStatus::Exited);
    EXPECT_FALSE(process.is_running());
    EXPECT_TRUE(process.has_exited());
    
    // Test Killed status
    QemuProcess killed_process(test_pid_ + 1, test_command_line_);
    killed_process.set_status(ProcessStatus::Running);
    killed_process.set_status(ProcessStatus::Killed);
    EXPECT_EQ(killed_process.status(), ProcessStatus::Killed);
    EXPECT_FALSE(killed_process.is_running());
    EXPECT_TRUE(killed_process.has_exited());
}

TEST_F(QemuProcessTest, StatusHistory) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Perform several status changes
    process.set_status(ProcessStatus::Running);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    process.set_status(ProcessStatus::Exited);
    
    auto history = process.status_history();
    EXPECT_GE(history.size(), 2);
    
    // Check that history is ordered chronologically
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
    }
    
    // Check specific transitions
    auto it = std::find_if(history.begin(), history.end(), [](const auto& change) {
        return change.from_status == ProcessStatus::Starting && 
               change.to_status == ProcessStatus::Running;
    });
    EXPECT_NE(it, history.end());
}

// Exit code and signal handling tests
TEST_F(QemuProcessTest, ExitCodeHandling) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Initially no exit code
    EXPECT_FALSE(process.exit_code().has_value());
    
    // Set exit code when process exits
    process.set_status(ProcessStatus::Running);
    process.set_exit_code(0);
    process.set_status(ProcessStatus::Exited);
    
    EXPECT_TRUE(process.exit_code().has_value());
    EXPECT_EQ(process.exit_code().value(), 0);
    EXPECT_TRUE(process.exited_successfully());
    
    // Test non-zero exit code
    QemuProcess failed_process(test_pid_ + 1, test_command_line_);
    failed_process.set_status(ProcessStatus::Running);
    failed_process.set_exit_code(1);
    failed_process.set_status(ProcessStatus::Exited);
    
    EXPECT_EQ(failed_process.exit_code().value(), 1);
    EXPECT_FALSE(failed_process.exited_successfully());
}

TEST_F(QemuProcessTest, SignalHandling) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Initially no signal
    EXPECT_FALSE(process.termination_signal().has_value());
    
    // Set signal when process is killed
    process.set_status(ProcessStatus::Running);
    process.set_termination_signal(SIGTERM);
    process.set_status(ProcessStatus::Killed);
    
    EXPECT_TRUE(process.termination_signal().has_value());
    EXPECT_EQ(process.termination_signal().value(), SIGTERM);
    EXPECT_FALSE(process.exited_successfully());
}

// Runtime tracking tests
TEST_F(QemuProcessTest, RuntimeTracking) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // No runtime when not started
    EXPECT_EQ(process.runtime(), std::chrono::seconds{0});
    
    // Start process and check runtime
    auto start_time = std::chrono::system_clock::now();
    process.set_status(ProcessStatus::Running);
    
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    
    auto runtime = process.runtime();
    EXPECT_GT(runtime, std::chrono::seconds{0});
    EXPECT_LT(runtime, std::chrono::seconds{10});
    
    // Stop process and check final runtime
    process.set_status(ProcessStatus::Exited);
    auto final_runtime = process.runtime();
    EXPECT_GE(final_runtime, runtime);
}

// Resource usage tests
TEST_F(QemuProcessTest, ResourceUsageTracking) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Initially no resource usage
    auto usage = process.resource_usage();
    EXPECT_EQ(usage.memory_rss_bytes, 0);
    EXPECT_EQ(usage.memory_vms_bytes, 0);
    EXPECT_DOUBLE_EQ(usage.cpu_percent, 0.0);
    
    // Set resource usage
    ProcessResourceUsage new_usage;
    new_usage.memory_rss_bytes = 512 * 1024 * 1024; // 512MB
    new_usage.memory_vms_bytes = 1024 * 1024 * 1024; // 1GB
    new_usage.cpu_percent = 25.5;
    new_usage.open_files = 42;
    new_usage.threads = 8;
    
    process.set_resource_usage(new_usage);
    
    auto retrieved_usage = process.resource_usage();
    EXPECT_EQ(retrieved_usage.memory_rss_bytes, new_usage.memory_rss_bytes);
    EXPECT_EQ(retrieved_usage.memory_vms_bytes, new_usage.memory_vms_bytes);
    EXPECT_DOUBLE_EQ(retrieved_usage.cpu_percent, new_usage.cpu_percent);
    EXPECT_EQ(retrieved_usage.open_files, new_usage.open_files);
    EXPECT_EQ(retrieved_usage.threads, new_usage.threads);
}

TEST_F(QemuProcessTest, ResourceUsageHistory) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Add several resource usage samples
    for (int i = 0; i < 3; ++i) {
        ProcessResourceUsage usage;
        usage.memory_rss_bytes = 100 * 1024 * 1024 * (i + 1); // 100MB * (i+1)
        usage.cpu_percent = 10.0 * (i + 1);
        process.set_resource_usage(usage);
        
        std::this_thread::sleep_for(std::chrono::milliseconds{10});
    }
    
    auto history = process.resource_usage_history();
    EXPECT_EQ(history.size(), 3);
    
    // Check that samples are stored correctly
    EXPECT_EQ(history[0].usage.memory_rss_bytes, 100 * 1024 * 1024);
    EXPECT_EQ(history[2].usage.memory_rss_bytes, 300 * 1024 * 1024);
    EXPECT_DOUBLE_EQ(history[0].usage.cpu_percent, 10.0);
    EXPECT_DOUBLE_EQ(history[2].usage.cpu_percent, 30.0);
}

// Command line analysis tests
TEST_F(QemuProcessTest, CommandLineAnalysis) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Test memory extraction
    auto memory = process.get_memory_from_cmdline();
    EXPECT_TRUE(memory.has_value());
    EXPECT_EQ(memory.value(), "1024");
    
    // Test disk image extraction
    auto disk_image = process.get_disk_image_from_cmdline();
    EXPECT_TRUE(disk_image.has_value());
    EXPECT_EQ(disk_image.value(), "/tmp/test-vm.qcow2");
    
    // Test port forwarding extraction
    auto ssh_port = process.get_ssh_port_from_cmdline();
    EXPECT_TRUE(ssh_port.has_value());
    EXPECT_EQ(ssh_port.value(), 2222);
    
    // Test KVM detection
    EXPECT_TRUE(process.is_kvm_enabled());
    
    // Test daemon mode detection
    EXPECT_TRUE(process.is_daemon_mode());
}

TEST_F(QemuProcessTest, CommandLineAnalysisEdgeCases) {
    // Test command line without specific options
    std::vector<std::string> minimal_cmdline = {"qemu-system-x86_64", "-hda", "disk.img"};
    QemuProcess minimal_process(test_pid_, minimal_cmdline);
    
    EXPECT_FALSE(minimal_process.get_memory_from_cmdline().has_value());
    EXPECT_TRUE(minimal_process.get_disk_image_from_cmdline().has_value());
    EXPECT_FALSE(minimal_process.get_ssh_port_from_cmdline().has_value());
    EXPECT_FALSE(minimal_process.is_kvm_enabled());
    EXPECT_FALSE(minimal_process.is_daemon_mode());
}

// Logging tests
TEST_F(QemuProcessTest, LoggingFunctionality) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Initially no logs
    EXPECT_TRUE(process.logs().empty());
    
    // Add logs
    process.add_log(QemuProcess::LogLevel::Info, "Process started");
    process.add_log(QemuProcess::LogLevel::Warning, "Memory allocation warning");
    process.add_log(QemuProcess::LogLevel::Error, "Failed to initialize device");
    
    auto logs = process.logs();
    EXPECT_EQ(logs.size(), 3);
    EXPECT_EQ(logs[0].level, QemuProcess::LogLevel::Info);
    EXPECT_EQ(logs[0].message, "Process started");
    EXPECT_EQ(logs[2].level, QemuProcess::LogLevel::Error);
    
    // Test log filtering
    auto error_logs = process.logs(QemuProcess::LogLevel::Error);
    EXPECT_EQ(error_logs.size(), 1);
    EXPECT_EQ(error_logs[0].message, "Failed to initialize device");
    
    // Test log limiting
    process.add_log(QemuProcess::LogLevel::Debug, "Debug message");
    auto recent_logs = process.logs(QemuProcess::LogLevel::Debug, 2);
    EXPECT_LE(recent_logs.size(), 2);
}

// Monitoring and health checks
TEST_F(QemuProcessTest, HealthChecking) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Healthy running process
    process.set_status(ProcessStatus::Running);
    EXPECT_TRUE(process.is_healthy());
    
    // Process with high CPU usage
    ProcessResourceUsage high_cpu_usage;
    high_cpu_usage.cpu_percent = 95.0;
    process.set_resource_usage(high_cpu_usage);
    EXPECT_FALSE(process.is_healthy()); // Should detect high CPU
    
    // Process with memory issues
    ProcessResourceUsage high_memory_usage;
    high_memory_usage.memory_rss_bytes = 8LL * 1024 * 1024 * 1024; // 8GB
    process.set_resource_usage(high_memory_usage);
    EXPECT_FALSE(process.is_healthy()); // Should detect excessive memory
    
    // Exited process is not healthy
    process.set_status(ProcessStatus::Exited);
    EXPECT_FALSE(process.is_healthy());
}

TEST_F(QemuProcessTest, ProcessMonitoring) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Test responsiveness checking
    process.set_status(ProcessStatus::Running);
    EXPECT_TRUE(process.is_responsive());
    
    // Simulate unresponsive process (this would be detected by external monitoring)
    process.mark_as_unresponsive();
    EXPECT_FALSE(process.is_responsive());
    
    // Test recovery
    process.mark_as_responsive();
    EXPECT_TRUE(process.is_responsive());
}

// Copy and move semantics tests
TEST_F(QemuProcessTest, CopySemantics) {
    QemuProcess original(test_pid_, test_command_line_);
    original.set_status(ProcessStatus::Running);
    original.set_exit_code(0);
    original.add_log(QemuProcess::LogLevel::Info, "Test log");
    
    QemuProcess copy(original);
    
    EXPECT_EQ(copy.process_id(), original.process_id());
    EXPECT_EQ(copy.status(), original.status());
    EXPECT_EQ(copy.exit_code(), original.exit_code());
    EXPECT_EQ(copy.logs().size(), original.logs().size());
    
    // Changes to copy shouldn't affect original
    copy.set_status(ProcessStatus::Exited);
    EXPECT_EQ(original.status(), ProcessStatus::Running);
}

TEST_F(QemuProcessTest, MoveSemantics) {
    QemuProcess original(test_pid_, test_command_line_);
    original.set_status(ProcessStatus::Running);
    ProcessId original_pid = original.process_id();
    
    QemuProcess moved(std::move(original));
    
    EXPECT_EQ(moved.process_id(), original_pid);
    EXPECT_EQ(moved.status(), ProcessStatus::Running);
}

// Edge cases and error conditions
TEST_F(QemuProcessTest, EdgeCases) {
    QemuProcess process(test_pid_, test_command_line_);
    
    // Test setting exit code on running process (should be allowed for monitoring)
    process.set_status(ProcessStatus::Running);
    process.set_exit_code(0);
    EXPECT_TRUE(process.exit_code().has_value());
    
    // Test multiple status changes
    for (int i = 0; i < 100; ++i) {
        process.set_status(ProcessStatus::Running);
        process.set_status(ProcessStatus::Exited);
    }
    
    // History should be limited to prevent memory issues
    auto history = process.status_history();
    EXPECT_LE(history.size(), 1000); // Should have reasonable limit
}