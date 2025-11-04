#include <gtest/gtest.h>
#include "scratchpad/domain/communication/command_execution.hpp"
#include "utils/test_helpers.hpp"
#include "scratchpad/errors.hpp"
#include <thread>
#include <chrono>

using namespace scratchpad;
using namespace scratchpad::test;

class CommandExecutionTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_command_ = "echo 'Hello World'";
        test_context_ = ExecutionContext::create_default();
        test_context_.timeout = std::chrono::seconds{30};
        test_context_.working_directory = "/tmp";
        test_context_.environment_variables["TEST_VAR"] = "test_value";
    }
    
    std::string test_command_;
    ExecutionContext test_context_;
};

// Construction tests
TEST_F(CommandExecutionTest, ConstructionWithValidCommand) {
    EXPECT_NO_THROW({
        CommandExecution execution(test_command_, test_context_);
        EXPECT_EQ(execution.command(), test_command_);
        EXPECT_EQ(execution.context().timeout, test_context_.timeout);
        EXPECT_EQ(execution.status(), ExecutionStatus::NotStarted);
    });
}

TEST_F(CommandExecutionTest, ConstructionWithDefaultContext) {
    EXPECT_NO_THROW({
        CommandExecution execution(test_command_);
        EXPECT_EQ(execution.command(), test_command_);
        EXPECT_EQ(execution.status(), ExecutionStatus::NotStarted);
    });
}

TEST_F(CommandExecutionTest, ConstructionWithEmptyCommand) {
    EXPECT_THROW({
        CommandExecution execution("");
    }, ScratchpadError);
    
    EXPECT_THROW({
        CommandExecution execution("   "); // Only whitespace
    }, ScratchpadError);
}

// Basic property tests
TEST_F(CommandExecutionTest, BasicProperties) {
    CommandExecution execution(test_command_, test_context_);
    
    EXPECT_EQ(execution.command(), test_command_);
    EXPECT_EQ(execution.context().timeout, test_context_.timeout);
    EXPECT_EQ(execution.context().working_directory, test_context_.working_directory);
    EXPECT_GT(execution.creation_time(), std::chrono::system_clock::time_point{});
    EXPECT_FALSE(execution.execution_id().empty());
}

// Status management tests
TEST_F(CommandExecutionTest, InitialStatus) {
    CommandExecution execution(test_command_, test_context_);
    
    EXPECT_EQ(execution.status(), ExecutionStatus::NotStarted);
    EXPECT_FALSE(execution.is_running());
    EXPECT_FALSE(execution.is_completed());
    EXPECT_FALSE(execution.has_failed());
    EXPECT_FALSE(execution.was_cancelled());
    EXPECT_FALSE(execution.has_timed_out());
}

TEST_F(CommandExecutionTest, StatusTransitions) {
    CommandExecution execution(test_command_, test_context_);
    
    auto before_start = std::chrono::system_clock::now();
    
    // NotStarted -> Running
    execution.set_status(ExecutionStatus::Running);
    EXPECT_EQ(execution.status(), ExecutionStatus::Running);
    EXPECT_TRUE(execution.is_running());
    EXPECT_FALSE(execution.is_completed());
    EXPECT_GE(execution.start_time().value(), before_start);
    
    // Running -> Completed
    execution.set_status(ExecutionStatus::Completed);
    EXPECT_EQ(execution.status(), ExecutionStatus::Completed);
    EXPECT_FALSE(execution.is_running());
    EXPECT_TRUE(execution.is_completed());
    EXPECT_TRUE(execution.end_time().has_value());
    
    // Test other final states
    CommandExecution failed_execution("false", test_context_);
    failed_execution.set_status(ExecutionStatus::Running);
    failed_execution.set_status(ExecutionStatus::Failed);
    EXPECT_TRUE(failed_execution.has_failed());
    
    CommandExecution timeout_execution("sleep 60", test_context_);
    timeout_execution.set_status(ExecutionStatus::Running);
    timeout_execution.set_status(ExecutionStatus::Timeout);
    EXPECT_TRUE(timeout_execution.has_timed_out());
    
    CommandExecution cancelled_execution("long_command", test_context_);
    cancelled_execution.set_status(ExecutionStatus::Running);
    cancelled_execution.set_status(ExecutionStatus::Cancelled);
    EXPECT_TRUE(cancelled_execution.was_cancelled());
}

TEST_F(CommandExecutionTest, StatusHistory) {
    CommandExecution execution(test_command_, test_context_);
    
    // Perform several status changes
    execution.set_status(ExecutionStatus::Running);
    std::this_thread::sleep_for(std::chrono::milliseconds{10});
    execution.set_status(ExecutionStatus::Completed);
    
    auto history = execution.status_history();
    EXPECT_GE(history.size(), 2);
    
    // Check that history is ordered chronologically
    for (size_t i = 1; i < history.size(); ++i) {
        EXPECT_GE(history[i].timestamp, history[i-1].timestamp);
    }
    
    // Check specific transitions
    auto it = std::find_if(history.begin(), history.end(), [](const auto& change) {
        return change.from_status == ExecutionStatus::NotStarted && 
               change.to_status == ExecutionStatus::Running;
    });
    EXPECT_NE(it, history.end());
}

// Timing tests
TEST_F(CommandExecutionTest, ExecutionTiming) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no timing information
    EXPECT_FALSE(execution.start_time().has_value());
    EXPECT_FALSE(execution.end_time().has_value());
    EXPECT_EQ(execution.execution_duration(), std::chrono::seconds{0});
    
    // Start execution
    auto start_time = std::chrono::system_clock::now();
    execution.set_status(ExecutionStatus::Running);
    
    EXPECT_TRUE(execution.start_time().has_value());
    EXPECT_GE(execution.start_time().value(), start_time);
    EXPECT_FALSE(execution.end_time().has_value());
    
    // Small delay
    std::this_thread::sleep_for(std::chrono::milliseconds{100});
    
    // Complete execution
    execution.set_status(ExecutionStatus::Completed);
    
    EXPECT_TRUE(execution.end_time().has_value());
    EXPECT_GE(execution.end_time().value(), execution.start_time().value());
    
    auto duration = execution.execution_duration();
    EXPECT_GT(duration, std::chrono::seconds{0});
    EXPECT_LT(duration, std::chrono::seconds{10});
}

// Output handling tests
TEST_F(CommandExecutionTest, OutputHandling) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no output
    EXPECT_TRUE(execution.stdout_output().empty());
    EXPECT_TRUE(execution.stderr_output().empty());
    EXPECT_EQ(execution.combined_output(), "");
    
    // Add output
    execution.append_stdout("Hello ");
    execution.append_stdout("World\n");
    execution.append_stderr("Warning: test\n");
    
    EXPECT_EQ(execution.stdout_output(), "Hello World\n");
    EXPECT_EQ(execution.stderr_output(), "Warning: test\n");
    EXPECT_EQ(execution.combined_output(), "Hello World\nWarning: test\n");
    
    // Test output streaming
    std::string captured_stdout;
    std::string captured_stderr;
    
    execution.set_stdout_callback([&captured_stdout](const std::string& data) {
        captured_stdout += data;
    });
    
    execution.set_stderr_callback([&captured_stderr](const std::string& data) {
        captured_stderr += data;
    });
    
    execution.append_stdout("More output\n");
    execution.append_stderr("Error message\n");
    
    EXPECT_EQ(captured_stdout, "More output\n");
    EXPECT_EQ(captured_stderr, "Error message\n");
}

TEST_F(CommandExecutionTest, OutputLimiting) {
    CommandExecution execution(test_command_, test_context_);
    
    // Set output limits
    execution.set_max_output_size(100); // 100 bytes
    
    // Add output that exceeds limit
    std::string large_output(150, 'A');
    execution.append_stdout(large_output);
    
    // Output should be truncated
    EXPECT_LE(execution.stdout_output().size(), 100);
    EXPECT_TRUE(execution.output_was_truncated());
}

// Exit code and signal handling
TEST_F(CommandExecutionTest, ExitCodeHandling) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no exit code
    EXPECT_FALSE(execution.exit_code().has_value());
    EXPECT_FALSE(execution.was_successful());
    
    // Set successful exit code
    execution.set_status(ExecutionStatus::Running);
    execution.set_exit_code(0);
    execution.set_status(ExecutionStatus::Completed);
    
    EXPECT_TRUE(execution.exit_code().has_value());
    EXPECT_EQ(execution.exit_code().value(), 0);
    EXPECT_TRUE(execution.was_successful());
    
    // Test failure exit code
    CommandExecution failed_execution("false", test_context_);
    failed_execution.set_status(ExecutionStatus::Running);
    failed_execution.set_exit_code(1);
    failed_execution.set_status(ExecutionStatus::Failed);
    
    EXPECT_EQ(failed_execution.exit_code().value(), 1);
    EXPECT_FALSE(failed_execution.was_successful());
}

TEST_F(CommandExecutionTest, SignalHandling) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no signal
    EXPECT_FALSE(execution.termination_signal().has_value());
    
    // Set termination signal
    execution.set_status(ExecutionStatus::Running);
    execution.set_termination_signal(SIGTERM);
    execution.set_status(ExecutionStatus::Cancelled);
    
    EXPECT_TRUE(execution.termination_signal().has_value());
    EXPECT_EQ(execution.termination_signal().value(), SIGTERM);
    EXPECT_FALSE(execution.was_successful());
}

// Environment and context tests
TEST_F(CommandExecutionTest, ExecutionContext) {
    CommandExecution execution(test_command_, test_context_);
    
    const auto& context = execution.context();
    EXPECT_EQ(context.timeout, test_context_.timeout);
    EXPECT_EQ(context.working_directory, test_context_.working_directory);
    EXPECT_EQ(context.environment_variables.at("TEST_VAR"), "test_value");
}

TEST_F(CommandExecutionTest, EnvironmentVariables) {
    ExecutionContext custom_context = test_context_;
    custom_context.environment_variables["PATH"] = "/custom/bin:/usr/bin";
    custom_context.environment_variables["HOME"] = "/home/test";
    
    CommandExecution execution(test_command_, custom_context);
    
    const auto& env_vars = execution.context().environment_variables;
    EXPECT_EQ(env_vars.at("PATH"), "/custom/bin:/usr/bin");
    EXPECT_EQ(env_vars.at("HOME"), "/home/test");
    EXPECT_EQ(env_vars.at("TEST_VAR"), "test_value");
}

// Timeout handling tests
TEST_F(CommandExecutionTest, TimeoutDetection) {
    ExecutionContext short_timeout_context = test_context_;
    short_timeout_context.timeout = std::chrono::milliseconds{100};
    
    CommandExecution execution("sleep 10", short_timeout_context);
    
    // Simulate timeout scenario
    execution.set_status(ExecutionStatus::Running);
    
    // In real implementation, timeout would be detected by the executor
    // Here we simulate it
    std::this_thread::sleep_for(std::chrono::milliseconds{150});
    
    auto elapsed = std::chrono::system_clock::now() - execution.start_time().value();
    EXPECT_GE(elapsed, short_timeout_context.timeout);
    
    // Simulate timeout handling
    execution.set_status(ExecutionStatus::Timeout);
    EXPECT_TRUE(execution.has_timed_out());
}

// Cancellation tests
TEST_F(CommandExecutionTest, Cancellation) {
    CommandExecution execution("long_running_command", test_context_);
    
    // Start execution
    execution.set_status(ExecutionStatus::Running);
    EXPECT_TRUE(execution.is_running());
    
    // Cancel execution
    execution.cancel();
    EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
    EXPECT_TRUE(execution.was_cancelled());
    EXPECT_FALSE(execution.is_running());
}

TEST_F(CommandExecutionTest, CancellationBeforeStart) {
    CommandExecution execution(test_command_, test_context_);
    
    // Cancel before starting
    execution.cancel();
    EXPECT_EQ(execution.status(), ExecutionStatus::Cancelled);
    EXPECT_TRUE(execution.was_cancelled());
}

// Progress tracking tests
TEST_F(CommandExecutionTest, ProgressTracking) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no progress
    EXPECT_DOUBLE_EQ(execution.progress_percent(), 0.0);
    EXPECT_TRUE(execution.progress_message().empty());
    
    // Set progress
    execution.set_progress(25.5, "Processing data...");
    EXPECT_DOUBLE_EQ(execution.progress_percent(), 25.5);
    EXPECT_EQ(execution.progress_message(), "Processing data...");
    
    // Update progress
    execution.set_progress(75.0, "Almost done...");
    EXPECT_DOUBLE_EQ(execution.progress_percent(), 75.0);
    EXPECT_EQ(execution.progress_message(), "Almost done...");
    
    // Complete with 100% progress
    execution.set_progress(100.0, "Completed");
    execution.set_status(ExecutionStatus::Completed);
    EXPECT_DOUBLE_EQ(execution.progress_percent(), 100.0);
}

// Resource usage tests
TEST_F(CommandExecutionTest, ResourceUsageTracking) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no resource usage
    auto usage = execution.resource_usage();
    EXPECT_EQ(usage.memory_peak_bytes, 0);
    EXPECT_DOUBLE_EQ(usage.cpu_time_seconds, 0.0);
    
    // Set resource usage
    ExecutionResourceUsage new_usage;
    new_usage.memory_peak_bytes = 1024 * 1024; // 1MB
    new_usage.cpu_time_seconds = 2.5;
    new_usage.io_read_bytes = 512 * 1024; // 512KB
    new_usage.io_write_bytes = 256 * 1024; // 256KB
    
    execution.set_resource_usage(new_usage);
    
    auto retrieved_usage = execution.resource_usage();
    EXPECT_EQ(retrieved_usage.memory_peak_bytes, new_usage.memory_peak_bytes);
    EXPECT_DOUBLE_EQ(retrieved_usage.cpu_time_seconds, new_usage.cpu_time_seconds);
    EXPECT_EQ(retrieved_usage.io_read_bytes, new_usage.io_read_bytes);
    EXPECT_EQ(retrieved_usage.io_write_bytes, new_usage.io_write_bytes);
}

// Error tracking tests
TEST_F(CommandExecutionTest, ErrorTracking) {
    CommandExecution execution(test_command_, test_context_);
    
    // Initially no error
    EXPECT_TRUE(execution.error_message().empty());
    EXPECT_FALSE(execution.has_error());
    
    // Set error
    execution.set_error("Command not found");
    EXPECT_EQ(execution.error_message(), "Command not found");
    EXPECT_TRUE(execution.has_error());
    
    // Error should cause failure
    execution.set_status(ExecutionStatus::Failed);
    EXPECT_TRUE(execution.has_failed());
}

// Move semantics tests (copy is deleted)
TEST_F(CommandExecutionTest, MoveSemantics) {
    CommandExecution original(test_command_, test_context_);
    original.set_status(ExecutionStatus::Running);
    original.append_stdout("test output");
    
    std::string original_command = original.command();
    
    CommandExecution moved(std::move(original));
    
    EXPECT_EQ(moved.command(), original_command);
    EXPECT_EQ(moved.status(), ExecutionStatus::Running);
    EXPECT_EQ(moved.stdout_output(), "test output");
}

// Thread safety tests
TEST_F(CommandExecutionTest, ThreadSafeOutputHandling) {
    CommandExecution execution(test_command_, test_context_);
    
    // Test concurrent output appending
    std::vector<std::thread> threads;
    std::atomic<int> append_count{0};
    
    for (int i = 0; i < 5; ++i) {
        threads.emplace_back([&execution, &append_count, i]() {
            for (int j = 0; j < 10; ++j) {
                execution.append_stdout("Thread " + std::to_string(i) + " Line " + std::to_string(j) + "\n");
                append_count++;
            }
        });
    }
    
    for (auto& thread : threads) {
        thread.join();
    }
    
    EXPECT_EQ(append_count.load(), 50);
    
    // Output should contain all lines (order may vary due to threading)
    std::string output = execution.stdout_output();
    for (int i = 0; i < 5; ++i) {
        for (int j = 0; j < 10; ++j) {
            std::string expected_line = "Thread " + std::to_string(i) + " Line " + std::to_string(j) + "\n";
            EXPECT_NE(output.find(expected_line), std::string::npos);
        }
    }
}

// Unique ID tests
TEST_F(CommandExecutionTest, UniqueExecutionIds) {
    std::set<std::string> execution_ids;
    
    // Create multiple executions and ensure IDs are unique
    for (int i = 0; i < 100; ++i) {
        CommandExecution execution("echo " + std::to_string(i), test_context_);
        std::string id = execution.execution_id();
        
        EXPECT_FALSE(id.empty());
        EXPECT_TRUE(execution_ids.insert(id).second) << "Duplicate execution ID: " << id;
    }
}