#include "scratchpad_cli.hpp"
#include "logging/logger.hpp"

#include <iostream>
#include <exception>

int main(int argc, char* argv[]) {
    try {
        // Initialize logging for CLI
        scratchpad::Logger& logger = scratchpad::Logger::instance();
        logger.set_level(scratchpad::LogLevel::Info);
        
        // Create and run CLI
        scratchpad::cli::ScratchpadCLI cli;
        return cli.run(argc, argv);
        
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
        return 1;
    } catch (...) {
        std::cerr << "Unknown fatal error occurred" << std::endl;
        return 1;
    }
}