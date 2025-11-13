#include "ConfigBroadcaster.h"
#include "TS1X.h"
#include <dirent.h>
#include <sys/stat.h>
#include <fstream>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "pi_server_sleep.h"
#include "command_definitions.h"
#include "logger.h"
#include <sys/types.h>
#include <errno.h>
#include <string.h>


ConfigBroadcaster::ConfigBroadcaster()
    : m_rssi_threshold(0)
    , m_rssi_delay(0)
    , m_rssi_increment(0)
    , m_power_adjust(0)
    , m_broadcast_interval_hours(8)
    , m_last_broadcast_time(0)
{
}

ConfigBroadcaster::~ConfigBroadcaster()
{
}

bool ConfigBroadcaster::Initialize(const std::string& config_dir,
    unsigned char rssi_threshold,
    unsigned char rssi_delay,
    unsigned char rssi_increment,
    unsigned char power_adjust,
    int broadcast_interval_hours)
{
    m_config_directory = config_dir;
    m_rssi_threshold = rssi_threshold;
    m_rssi_delay = rssi_delay;
    m_rssi_increment = rssi_increment;
    m_power_adjust = power_adjust;
    m_last_broadcast_time = time(nullptr);
    m_broadcast_interval_hours = broadcast_interval_hours;
    struct stat st;

    // Check if path exists
    if (stat(config_dir.c_str(), &st) == 0) {
        // Path exists - check if it's a directory
        if (!S_ISDIR(st.st_mode)) {
            LOG_ERROR_CTX("broadcast_config", 
                        "Path exists but is not a directory: %s", 
                        config_dir.c_str());
            return false;
        }
        // Directory exists - success
        return true;
    }

    // Directory doesn't exist - try to create it
    if (mkdir(config_dir.c_str(), 0755) == 0) {
        LOG_INFO_CTX("broadcast_config", "Created config directory: %s", 
                    config_dir.c_str());
        return true;
    }

    // Failed to create directory
    LOG_ERROR_CTX("broadcast_config", 
                "Failed to create config directory: %s (error: %s)", 
                config_dir.c_str(), strerror(errno));
    return false;
}

std::vector<std::string> ConfigBroadcaster::GetConfigFiles()
{
    std::vector<std::string> config_files;
    DIR* dir = opendir(m_config_directory.c_str());
    
    if (!dir) {
        std::cerr << "ERROR: Cannot open config directory: " 
                  << m_config_directory << std::endl;
        return config_files;
    }
    
    struct dirent* entry;
    while ((entry = readdir(dir)) != nullptr) {
        std::string filename(entry->d_name);
        // Look for .config files
        if (filename.length() > 7 && 
            filename.substr(filename.length() - 7) == ".config") {
            std::string full_path = m_config_directory + "/" + filename;
            config_files.push_back(full_path);
        }
    }
    
    closedir(dir);
    
    // Sort for consistent ordering
    std::sort(config_files.begin(), config_files.end());
    
    return config_files;
}

bool ConfigBroadcaster::IsTimeForPeriodicBroadcast()
{
    time_t current_time = time(nullptr);
    double hours_elapsed = difftime(current_time, m_last_broadcast_time) / 3600.0;
    
    return (hours_elapsed >= m_broadcast_interval_hours);
}

void ConfigBroadcaster::ResetBroadcastTimer()
{
    m_last_broadcast_time = time(nullptr);
}

bool ConfigBroadcaster::BroadcastAllConfigs(CTS1X* ts1x_core)
{
    ResetBroadcastTimer();

    if (!ts1x_core) {
        std::cerr << "ERROR: ts1x_core is null" << std::endl;
        return false;
    }
    
    std::vector<std::string> config_files = GetConfigFiles();
    
    if (config_files.empty()) {
        LOG_INFO_CTX("broadcast_config", "No config files found in: %s", m_config_directory.c_str());
        return true; // Not an error, just no files
    }
    
    LOG_INFO_CTX("broadcast_config", "Found %d config files", config_files.size());

    bool all_success = true;
    for (const auto& file_path : config_files) {
        // Extract MAC ID from filename (e.g., bbe01aae.config -> 0xbbe01aae)
        unsigned int macid = ExtractMacIdFromFilename(file_path);
        unsigned short time_block = 0; // Can be parameterized if needed
        
        if (!BroadcastSingleConfig(file_path, ts1x_core, macid, time_block)) {
            all_success = false;
        }
    }
    
    return all_success;
}

bool ConfigBroadcaster::BroadcastSingleConfig(const std::string& file_path,
                                              CTS1X* ts1x_core,
                                              unsigned int macid,
                                              unsigned short time_block)
{
    unsigned char config_data[NEW_CONFIG_LENGTH];
    int bytes_read = 0;
    
    if (!ReadConfigFile(file_path, config_data, bytes_read)) {
        return false;
    }
    
    if (bytes_read != NEW_CONFIG_LENGTH) {
        std::cerr << "WARNING: Config file size mismatch. Expected " 
                  << NEW_CONFIG_LENGTH << " bytes, got " 
                  << bytes_read << " bytes: " << file_path << std::endl;
    }
    
    // Build the full 80-byte config packet
    unsigned char config_packet_long[PARAM_SEND_WORDS * 8];
    BuildConfigPacket(config_packet_long, config_data, macid, time_block);
    
    // Build the 128-byte broadcast command buffer
    unsigned char cmd_buffer[BROADCAST_COMMAND_BUFFER_SIZE];
    if (!BuildBroadcastCommand(cmd_buffer, config_packet_long, PARAM_SEND_WORDS * 8)) {
        std::cerr << "ERROR: Failed to build broadcast command" << std::endl;
        return false;
    }
    
    // Extract filename for logging
    size_t pos = file_path.find_last_of("/\\");
    std::string filename = (pos != std::string::npos) ? 
                          file_path.substr(pos + 1) : file_path;

    // Broadcast 
    for (int i=0;i<6;i++){
        LOG_INFO_CTX("broadcast_config", "Broadcasting config: %s (Unit: 0x%08X), %d", 
             filename.c_str(), macid,i+1);
        ts1x_core->send_command(cmd_buffer, BROADCAST_COMMAND_BUFFER_SIZE);
        ts1x_core->flush_tx_buffer();
        Server_sleep_ms(100);
    }
    return true;
}

bool ConfigBroadcaster::ReadConfigFile(const std::string& file_path,
                                       unsigned char* buffer,
                                       int& bytes_read)
{
    std::ifstream file(file_path.c_str(), std::ios::binary);
    
    if (!file.is_open()) {
        std::cerr << "ERROR: Cannot open config file: " << file_path << std::endl;
        return false;
    }
    
    file.read((char*)buffer, NEW_CONFIG_LENGTH);
    bytes_read = file.gcount();
    file.close();
    
    return true;
}

void ConfigBroadcaster::BuildConfigPacket(unsigned char* config_packet_long,
                                          const unsigned char* config_data,
                                          unsigned int macid,
                                          unsigned short time_block)
{
    int xpnt = 0;
    
    // Copy config data
    for (xpnt = 0; xpnt < NEW_CONFIG_LENGTH; xpnt++) {
        config_packet_long[xpnt] = config_data[xpnt];
    }
    
    // Add MAC ID (4 bytes, big-endian)
    config_packet_long[xpnt++] = (macid >> 24) & 0xff;
    config_packet_long[xpnt++] = (macid >> 16) & 0xff;
    config_packet_long[xpnt++] = (macid >> 8) & 0xff;
    config_packet_long[xpnt++] = macid & 0xff;
    
    // Add time_block (2 bytes)
    config_packet_long[xpnt++] = time_block >> 7;
    config_packet_long[xpnt++] = time_block >> 7;
    
    // Calculate and add CRC32 (4 bytes)
    unsigned char crc32[8];
    CalculateCRC32(config_packet_long, xpnt, crc32);
    config_packet_long[xpnt++] = crc32[0];
    config_packet_long[xpnt++] = crc32[1];
    config_packet_long[xpnt++] = crc32[2];
    config_packet_long[xpnt++] = crc32[3];
    
    // Add RSSI parameters
    config_packet_long[xpnt++] = 0xfa;
    config_packet_long[xpnt++] = 0xde;
    config_packet_long[xpnt++] = m_rssi_threshold;
    config_packet_long[xpnt++] = m_rssi_delay;
    config_packet_long[xpnt++] = m_rssi_increment;
    config_packet_long[xpnt++] = m_power_adjust;
    
    // Pad to PARAM_SEND_WORDS * 8 bytes
    while (xpnt < PARAM_SEND_WORDS * 8) {
        config_packet_long[xpnt++] = 0;
    }
}

bool ConfigBroadcaster::BuildBroadcastCommand(unsigned char* cmd_buffer,
                                              const unsigned char* config_packet,
                                              int packet_size)
{
    if (packet_size != PARAM_SEND_WORDS * 8) {
        std::cerr << "ERROR: Invalid packet size: " << packet_size << std::endl;
        return false;
    }
    
    // Build command buffer following the protocol from write_command
    // Format: tS + hop + base_addr + forward_addr + command + binary_data + uP
    
    int pos = 0;
    
    // Prefix "tS"
    cmd_buffer[pos++] = 't';
    cmd_buffer[pos++] = 'S';
    
    // Hop count (1 for broadcast)
    cmd_buffer[pos++] = 1;
    
    // Base station address (0xffff)
    cmd_buffer[pos++] = 0xff;
    cmd_buffer[pos++] = 0xff;
    cmd_buffer[pos++] = 0xff;
    cmd_buffer[pos++] = 0xff;
    
    // Padding (3 bytes)
    cmd_buffer[pos++] = '0';
    cmd_buffer[pos++] = '0';
    cmd_buffer[pos++] = '0';
    cmd_buffer[pos++] = '0';
    cmd_buffer[pos++] = '0';
    cmd_buffer[pos++] = '0';
    
    // Forward address - all '0' for broadcast (32 bytes)
    for (int i = 0; i < 32; i++) {
        cmd_buffer[pos++] = '0';
    }
    
    cmd_buffer[pos++] = CMD_DATA_RESPONSE;
    
    // Binary config data (80 bytes)
    for (int i = 0; i < packet_size; i++) {
        cmd_buffer[pos++] = config_packet[i];
    }
    
    // Suffix "uP"
    cmd_buffer[pos++] = 'u';
    cmd_buffer[pos++] = 'P';
    
    // Verify we used exactly 128 bytes
    if (pos != BROADCAST_COMMAND_BUFFER_SIZE) {
        std::cerr << "ERROR: Command buffer size mismatch. Expected 128, got " 
                  << pos << std::endl;
        return false;
    }
    
    return true;
}

void ConfigBroadcaster::CalculateCRC32(unsigned char* message,
                                       int message_length,
                                       unsigned char* crcout)
{
    int i, j;
    unsigned long int byte, crc, mask;
    
    i = 0;
    crc = 0xFFFFFFFF;
    while (i < message_length) {
        byte = message[i];
        crc = crc ^ byte;
        for (j = 7; j >= 0; j--) {
            mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
        i = i + 1;
    }
    
    crc = ~crc;
    crcout[0] = (crc >> 24) & 0xff;
    crcout[1] = (crc >> 16) & 0xff;
    crcout[2] = (crc >> 8) & 0xff;
    crcout[3] = crc & 0xff;
}

unsigned int ConfigBroadcaster::ExtractMacIdFromFilename(const std::string& filename)
{
    // Extract filename without path
    size_t pos = filename.find_last_of("/\\");
    std::string base_filename = (pos != std::string::npos) ? 
                                filename.substr(pos + 1) : filename;
    
    // Remove .config extension
    pos = base_filename.find(".config");
    if (pos != std::string::npos) {
        base_filename = base_filename.substr(0, pos);
    }
    
    // Convert hex string to integer
    unsigned int macid = 0;
    sscanf(base_filename.c_str(), "%x", &macid);
    
    return macid;
}