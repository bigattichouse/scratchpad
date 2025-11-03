#include "logging/logger.hpp"
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/sinks/rotating_file_sink.h>

namespace scratchpad {

Logger::Logger() {
    // Create default console logger
    auto console_sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
    console_sink->set_level(spdlog::level::info);
    
    logger_ = std::make_shared<spdlog::logger>("scratchpad", console_sink);
    logger_->set_level(spdlog::level::info);
    logger_->flush_on(spdlog::level::err);
    
    spdlog::register_logger(logger_);
}

Logger::~Logger() {
    if (logger_) {
        logger_->flush();
    }
}

Logger& Logger::instance() {
    static Logger instance;
    return instance;
}

void Logger::set_level(LogLevel level) {
    spdlog::level::level_enum spdlog_level;
    switch (level) {
        case LogLevel::Debug:   spdlog_level = spdlog::level::debug; break;
        case LogLevel::Info:    spdlog_level = spdlog::level::info; break;
        case LogLevel::Warning: spdlog_level = spdlog::level::warn; break;
        case LogLevel::Error:   spdlog_level = spdlog::level::err; break;
    }
    
    logger_->set_level(spdlog_level);
}

void Logger::add_file_sink(const std::string& filename, bool rotate) {
    try {
        std::shared_ptr<spdlog::sinks::sink> file_sink;
        
        if (rotate) {
            file_sink = std::make_shared<spdlog::sinks::rotating_file_sink_mt>(
                filename, 1024 * 1024 * 10, 3); // 10MB, 3 files
        } else {
            file_sink = std::make_shared<spdlog::sinks::basic_file_sink_mt>(filename);
        }
        
        file_sink->set_level(spdlog::level::debug); // Log everything to file
        
        // Create new logger with existing sinks plus file sink
        auto sinks = logger_->sinks();
        sinks.push_back(file_sink);
        
        auto new_logger = std::make_shared<spdlog::logger>("scratchpad", sinks.begin(), sinks.end());
        new_logger->set_level(logger_->level());
        new_logger->flush_on(spdlog::level::err);
        
        logger_ = new_logger;
        spdlog::register_logger(logger_);
        
    } catch (const spdlog::spdlog_ex& ex) {
        // Fall back to console logging if file sink fails
        logger_->error("Failed to add file sink: {}", ex.what());
    }
}

void Logger::log(LogLevel level, const std::string& message) {
    if (!logger_) return;
    
    switch (level) {
        case LogLevel::Debug:
            logger_->debug(message);
            break;
        case LogLevel::Info:
            logger_->info(message);
            break;
        case LogLevel::Warning:
            logger_->warn(message);
            break;
        case LogLevel::Error:
            logger_->error(message);
            break;
    }
}

void Logger::debug(const std::string& message) {
    log(LogLevel::Debug, message);
}

void Logger::info(const std::string& message) {
    log(LogLevel::Info, message);
}

void Logger::warning(const std::string& message) {
    log(LogLevel::Warning, message);
}

void Logger::error(const std::string& message) {
    log(LogLevel::Error, message);
}

void Logger::flush() {
    if (logger_) {
        logger_->flush();
    }
}

} // namespace scratchpad