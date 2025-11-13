#include "Ts1xSamplingReader.h"
#include "logger.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <chrono>
#include <thread>
#include <utility>
#include <sys/stat.h>
#include <unistd.h>
#include <time.h>

// Parse a line from the file into a Ts1xChannel
static bool parseLine(const std::string& line, Ts1xChannel& channel, int line_num) {
    std::istringstream iss(line);
    std::string token;
    std::vector<std::string> tokens;
    
    // Split by " | "
    while (std::getline(iss, token, '|')) {
        // Trim whitespace
        size_t start = token.find_first_not_of(" \t\r\n");
        size_t end = token.find_last_not_of(" \t\r\n");
        if (start != std::string::npos && end != std::string::npos) {
            tokens.push_back(token.substr(start, end - start + 1));
        } else if (start == std::string::npos) {
            // Empty token
            tokens.push_back("");
        }
    }
    
    // Need exactly 15 fields
    if (tokens.size() != 15) {
        LOG_ERROR_CTX("ts1x_reader", "Line %d has %zu fields, expected 15", 
                      line_num, tokens.size());
        return false;
    }
    
    try {
        channel.hw_type = tokens[0];
        channel.serial = tokens[1];
        channel.port = std::stoi(tokens[2]);
        channel.channel_num = std::stoi(tokens[3]);
        channel.channel_type = tokens[4];
        channel.channel_id = tokens[5];
        channel.interval = std::stod(tokens[6]);
        channel.adj_interval = std::stod(tokens[7]);
        
        // Max frequency: "-" for DC channels
        if (tokens[8] == "-") {
            channel.max_freq = 0.0;
        } else {
            channel.max_freq = std::stod(tokens[8]);
        }
        
        // Resolution: "-" for DC channels
        if (tokens[9] == "-") {
            channel.resolution = 0;
        } else {
            channel.resolution = std::stoi(tokens[9]);
        }
        
        channel.last_sampled = tokens[10];
        channel.priority = std::stoi(tokens[11]);
        channel.is_demod = std::stoi(tokens[12]);
        channel.external_input = tokens[13];
        channel.external_name = tokens[14];
        
        return true;
    } catch (const std::exception& e) {
        LOG_ERROR_CTX("ts1x_reader", "Error parsing line %d: %s", line_num, e.what());
        return false;
    }
}

// Read the file safely - handles the atomic rename pattern
std::vector<Ts1xChannel> readTs1xSamplingFile(const std::string& filepath) {
    std::vector<Ts1xChannel> channels;
    
    // Pre-allocate space to avoid reallocations (reasonable estimate for typical configs)
    channels.reserve(32);
    
    // Check if file exists using POSIX access()
    if (access(filepath.c_str(), R_OK) != 0) {
        LOG_WARN_CTX("ts1x_reader", "File does not exist or is not readable: %s", filepath.c_str());
        return channels;
    }
    
    // Optional: Check file age to avoid reading while it's being written
    // This is extra safety for atomic rename pattern
    struct stat file_stat;
    if (stat(filepath.c_str(), &file_stat) == 0) {
        time_t now = time(nullptr);
        time_t mtime = file_stat.st_mtime;
        double age_seconds = difftime(now, mtime);
        
        if (age_seconds < 2.0) {
            // File was modified less than 2 seconds ago, wait a bit
            LOG_INFO_CTX("ts1x_reader", "File recently modified, waiting 2 seconds...");
            std::this_thread::sleep_for(std::chrono::seconds(2));
        }
    } else {
        LOG_WARN_CTX("ts1x_reader", "Could not check file age: %s", filepath.c_str());
        // Continue anyway
    }
    
    // Open and read the file
    std::ifstream file(filepath);
    if (!file.is_open()) {
        LOG_ERROR_CTX("ts1x_reader", "Failed to open file: %s", filepath.c_str());
        return channels;
    }
    
    std::string line;
    int line_num = 0;
    int parse_failures = 0;
    
    // Skip header lines (first 2 lines: header and separator)
    if (std::getline(file, line)) {
        line_num++;  // Header line
    } else {
        LOG_WARN_CTX("ts1x_reader", "File is empty: %s", filepath.c_str());
        file.close();
        return channels;
    }
    
    if (std::getline(file, line)) {
        line_num++;  // Separator line
    } else {
        LOG_WARN_CTX("ts1x_reader", "File has no data rows: %s", filepath.c_str());
        file.close();
        return channels;
    }
    
    // Read data lines
    while (std::getline(file, line)) {
        line_num++;
        
        // Remove any trailing '\r' (Windows line endings)
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        
        // Skip empty lines
        if (line.empty() || line.find_first_not_of(" \t\r\n") == std::string::npos) {
            continue;
        }
        
        Ts1xChannel channel;
        if (parseLine(line, channel, line_num)) {
            channels.emplace_back(std::move(channel));
        } else {
            parse_failures++;
            // Error already logged by parseLine
        }
    }
    
    file.close();
    
    if (parse_failures > 0) {
        LOG_WARN_CTX("ts1x_reader", "Successfully parsed %zu channels with %d failures from %s", 
                     channels.size(), parse_failures, filepath.c_str());
    } else {
        LOG_INFO_CTX("ts1x_reader", "Successfully read %zu TS1X/StormX channels from %s", 
                     channels.size(), filepath.c_str());
    }
    
    return channels;
}
