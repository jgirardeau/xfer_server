#ifndef CONFIG_BROADCASTER_H
#define CONFIG_BROADCASTER_H

#include <string>
#include <vector>
#include <ctime>

#define NEW_CONFIG_LENGTH 38
#define PARAM_SEND_WORDS 10
#define COMMAND_BROADCAST "Dx"
#define BROADCAST_COMMAND_BUFFER_SIZE 128

class LogFile;
class CTS1X;  // Forward declaration

class ConfigBroadcaster {
public:
    ConfigBroadcaster();
    ~ConfigBroadcaster();
    
    // Initialize with config file path and parameters
    bool Initialize(const std::string& config_dir,
               unsigned char rssi_threshold,
               unsigned char rssi_delay,
               unsigned char rssi_increment,
               unsigned char power_adjust,
               int broadcast_interval_hours); 
    
    // Broadcast all config files using CTS1X send_command
    bool BroadcastAllConfigs(CTS1X* ts1x_core);
    
    // Check if it's time for periodic broadcast
    bool IsTimeForPeriodicBroadcast();
    
    // Reset the periodic broadcast timer
    void ResetBroadcastTimer();
    
    // Get list of config files
    std::vector<std::string> GetConfigFiles();

private:
    std::string m_config_directory;
    unsigned char m_rssi_threshold;
    unsigned char m_rssi_delay;
    unsigned char m_rssi_increment;
    unsigned char m_power_adjust;
    time_t m_last_broadcast_time;
    int m_broadcast_interval_hours;
    
    // Helper functions
    bool BroadcastSingleConfig(const std::string& file_path, 
                              CTS1X* ts1x_core,
                              unsigned int macid,
                              unsigned short time_block);
    
    bool ReadConfigFile(const std::string& file_path, 
                       unsigned char* buffer,
                       int& bytes_read);
    
    void BuildConfigPacket(unsigned char* config_packet_long,
                          const unsigned char* config_data,
                          unsigned int macid,
                          unsigned short time_block);
    
    bool BuildBroadcastCommand(unsigned char* cmd_buffer,
                              const unsigned char* config_packet,
                              int packet_size);
    
    void CalculateCRC32(unsigned char* message, 
                       int message_length, 
                       unsigned char* crcout);
    
    unsigned int ExtractMacIdFromFilename(const std::string& filename);
};

#endif // CONFIG_BROADCASTER_H