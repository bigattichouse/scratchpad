#pragma once

#include <string>
#include <memory>

// Forward declaration to avoid including spdlog in header
namespace spdlog {
    class logger;
}

namespace scratchpad {

/**
 * Log levels
 */
enum class LogLevel {
    Debug,
    Info,
    Warning,
    Error
};

/**
 * Centralized logging service for Scratchpad
 * 
 * Provides structured logging with multiple output sinks and configurable
 * log levels. Thread-safe and singleton-based for global access.
 */
class Logger {
public:
    /**
     * Get singleton instance
     * @return Logger instance
     */
    static Logger& instance();

    /**
     * Set global log level
     * @param level Minimum log level to output
     */
    void set_level(LogLevel level);

    /**
     * Add file output sink
     * @param filename Path to log file
     * @param rotate Enable log rotation
     */
    void add_file_sink(const std::string& filename, bool rotate = true);

    /**
     * Log message at specified level
     * @param level Log level
     * @param message Log message
     */
    void log(LogLevel level, const std::string& message);

    /**
     * Log debug message
     * @param message Debug message
     */
    void debug(const std::string& message);

    /**
     * Log info message
     * @param message Info message
     */
    void info(const std::string& message);

    /**
     * Log warning message
     * @param message Warning message
     */
    void warning(const std::string& message);

    /**
     * Log error message
     * @param message Error message
     */
    void error(const std::string& message);

    /**
     * Flush all log outputs
     */
    void flush();

    // Template methods for formatted logging
    template<typename... Args>
    void debug(const std::string& format, Args&&... args) {
        log_formatted(LogLevel::Debug, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void info(const std::string& format, Args&&... args) {
        log_formatted(LogLevel::Info, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void warning(const std::string& format, Args&&... args) {
        log_formatted(LogLevel::Warning, format, std::forward<Args>(args)...);
    }

    template<typename... Args>
    void error(const std::string& format, Args&&... args) {
        log_formatted(LogLevel::Error, format, std::forward<Args>(args)...);
    }

private:
    Logger();
    ~Logger();

    // Non-copyable, non-movable
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;
    Logger(Logger&&) = delete;
    Logger& operator=(Logger&&) = delete;

    template<typename... Args>
    void log_formatted(LogLevel level, const std::string& format, Args&&... args);

    std::shared_ptr<spdlog::logger> logger_;
};

// Convenience macros for global logging
#define LOG_DEBUG(msg) scratchpad::Logger::instance().debug(msg)
#define LOG_INFO(msg) scratchpad::Logger::instance().info(msg)
#define LOG_WARNING(msg) scratchpad::Logger::instance().warning(msg)
#define LOG_ERROR(msg) scratchpad::Logger::instance().error(msg)

#define LOG_DEBUG_FMT(fmt, ...) scratchpad::Logger::instance().debug(fmt, __VA_ARGS__)
#define LOG_INFO_FMT(fmt, ...) scratchpad::Logger::instance().info(fmt, __VA_ARGS__)
#define LOG_WARNING_FMT(fmt, ...) scratchpad::Logger::instance().warning(fmt, __VA_ARGS__)
#define LOG_ERROR_FMT(fmt, ...) scratchpad::Logger::instance().error(fmt, __VA_ARGS__)

} // namespace scratchpad