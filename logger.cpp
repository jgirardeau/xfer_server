// logger.cpp
#include "logger.h"
#include <iostream>

// Static variables for logger instances
static SimpleLogger* g_logger_instance = nullptr;
static SimpleLogger* g_header_logger_instance = nullptr;

SimpleLogger* get_logger() {
    return g_logger_instance;
}

SimpleLogger* get_header_logger() {
    return g_header_logger_instance;
}

void init_logger(const std::string& log_directory) {
    if (!g_logger_instance) {
        std::cout << "Initializing logger..." << std::endl;
        std::string main_log_path = log_directory + "/pi_server.log";
        g_logger_instance = new SimpleLogger(main_log_path.c_str(), 5120*4, 10);
        if (g_logger_instance) {
            std::cout << "Main logger created at: " << (void*)g_logger_instance << std::endl;
            std::cout << "Main logger path: " << main_log_path << std::endl;
        } else {
            std::cout << "ERROR: Failed to create main logger!" << std::endl;
        }
    }
    
    if (!g_header_logger_instance) {
        std::cout << "Initializing header logger..." << std::endl;
        std::string header_log_path = log_directory + "/header.log";
        // Header log: 5MB max size, keep 10 rotated files
        g_header_logger_instance = new SimpleLogger(header_log_path.c_str(), 5120*4, 10);
        if (g_header_logger_instance) {
            std::cout << "Header logger created at: " << (void*)g_header_logger_instance << std::endl;
            std::cout << "Header logger path: " << header_log_path << std::endl;
        } else {
            std::cout << "ERROR: Failed to create header logger!" << std::endl;
        }
    }
    
    std::cout << "Logger initialization complete" << std::endl;
}

void cleanup_logger() {
    if (g_logger_instance) {
        std::cout << "Cleaning up main logger..." << std::endl;
        delete g_logger_instance;
        g_logger_instance = nullptr;
    }
    
    if (g_header_logger_instance) {
        std::cout << "Cleaning up header logger..." << std::endl;
        delete g_header_logger_instance;
        g_header_logger_instance = nullptr;
    }
}
