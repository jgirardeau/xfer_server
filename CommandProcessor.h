#ifndef COMMAND_PROCESSOR_H
#define COMMAND_PROCESSOR_H

#include <cstdint>
#include <string>
// Forward declaration
struct Sampleset;
#include <cstring>
#include <map>
#include "TS1X.h"
#include <vector>
class CTS1X;
#define PACKET_LENGTH 128
#define HEADER_OFFSET 0
#define HEADER_BYTE1 0x74  // 't'
#define HEADER_BYTE2 0x53  // 'S'
#define TAIL_OFFSET 126
#define TAIL_BYTE1 0x75    // 'u'
#define TAIL_BYTE2 0x50    // 'P'
#define CHECKSUM_OFFSET 124
#define COMMAND_START 45
#define BROADCAST_MAC 0xFFFFFFFF  // BASEâ†’UNIT always uses broadcast MAC

// Forward declarations
class CommandTransmitter;
class CommandReceiver;

// Packet direction
enum PacketDirection {
    BASE_TO_UNIT,  // Command from base to remote unit
    UNIT_TO_BASE,  // Response from remote unit to base
    UNKNOWN        // Cannot determine direction
};

// Time structure
struct PacketTime {
    uint16_t year;
    uint8_t month;
    uint8_t day;
    uint8_t hour;
    uint8_t min;
    uint8_t sec;
};

// Header info structure (32 bytes at offset 13-44)
struct HeaderInfo {
    uint8_t reserved1;
    uint8_t reserved2;
    uint8_t marker;
    uint32_t delta;          // 24-bit value
    uint8_t data_control_bits;
    uint32_t macid;
    uint16_t descriptor;
    PacketTime dataset_pi_time;
    uint32_t current_mistlx_time;
    uint32_t data_collection_time;
    uint8_t battery;
    uint16_t temperature;
    uint8_t rssi;
};

// Command response structure
struct CommandResponse {
    // Raw packet data
    unsigned char data[128];
    
    // Packet validation
    bool packet_valid;
    bool crc_valid;
    
    // Basic header (bytes 0-12)
    uint8_t hops;
    uint32_t source_macid;
    uint32_t unit_id;
    PacketDirection direction;
    
    // Header info (bytes 13-44, only for UNITâ†’BASE responses)
    bool has_header_info;
    HeaderInfo header_info;

    // Decoded descriptor fields (from header_info.descriptor)
    bool descriptor_rms_only;           // Bit 15: Send RMS only, not raw data
    uint8_t descriptor_sample_rate;     // Bits 14-12: 0=20K, 1=10K, 2=5K, 3=2.5K, 4=1.25K, 5=625, 6=312.5, 7=156.5
    uint8_t descriptor_channel_mask;    // Bits 11-8: Bit0=Ultrasonic, Bit1=X, Bit2=Y, Bit3=Z
    uint8_t descriptor_length_code;     // Bits 7-0: (L+1)*256 = sample length
    uint32_t descriptor_sample_length;  // Decoded sample length in samples
    std::string descriptor_sample_rate_str;  // Human-readable sample rate
    
    // Command parameters (bytes 46-125, for BASEâ†’UNIT commands)
    bool has_command_params;
    uint32_t command_params[10];  // 10 parameters from ASCII hex
    
    // Decoded 'R' (SAMPLE_DATA) command parameters
    uint32_t sample_capture_segments;
    uint32_t sample_channel;
    uint32_t sample_decimation;
    bool advanced_checksum;  // Bit 12 flag from command params[1] - indicates advanced checksum mode
    uint32_t sample_tach_delay;
    uint32_t sample_dc_control;
    uint32_t sample_wakeup_delay;
    uint32_t sample_bluewave_interval;
    
    // Calculated sample parameters (derived from above)
    uint32_t sample_length;  // Calculated as sample_capture_segments * 16
    double sample_rate;      // Calculated as 20000.0 / (2^(sample_decimation - 1))
    
    // Command info (byte 45 onwards)
    char command_code;
    std::string command_name;
    std::string command_description;
    uint8_t command_hops;
    uint32_t command_macid;
    uint8_t command_count;
    char version[11];
    std::string unit_type;
    std::string firmware_version;
    
    // Age field for 'E' (erase) command
    uint8_t erase_age;
    
    // RSSI and status
    uint8_t rssi_value;
    uint8_t ambient_rssi;
    uint8_t ram_corruption_reset_count;
    uint8_t firmware;
    uint32_t on_deck_crc;

    // Sensor data arrays (raw)
    uint16_t buf_data[16];
    uint16_t buf_spread[16];
    uint16_t buf_tach[16];
    
    // Decoded buf_data fields (bytes 74-89)
    uint16_t datasets_processed;        // buf_data[0]
    uint16_t packet_correction;         // buf_data[1]
    uint16_t on_deck_dataset_count;     // buf_data[2]
    uint16_t pi_time_year;              // buf_data[3]
    uint8_t pi_time_month;              // buf_data[4] high byte
    uint8_t pi_time_day;                // buf_data[4] low byte
    uint8_t pi_time_hour;               // buf_data[5] high byte
    uint8_t pi_time_min;                // buf_data[5] low byte
    uint8_t pi_spi_restart_count;       // buf_data[6] high byte
    uint8_t global_power_control;       // buf_data[6] low byte
    uint8_t reboot_count;               // buf_data[7] high byte
    uint8_t undervoltage_count;         // buf_data[7] low byte
    
    // Decoded buf_spread fields (bytes 90-105)
    uint16_t header_debug;              // buf_spread[0]
    uint16_t header_bleon;              // buf_spread[1]
    uint16_t header_fpgaon;             // buf_spread[2]
    uint32_t header_mincount;           // buf_spread[3-4] combined
    uint32_t header_failcount;          // buf_spread[5-6] combined
    
    // Session info
    uint16_t session_id_command;
    uint8_t fips_status;
    uint32_t dest_macid;
    
    // Upload data (for command '3')
    bool has_upload_data;
    bool is_fast_mode;
    uint16_t upload_segment_addr;
    int16_t upload_data[32];  // 32 samples per segment
    
    // Upload partial request (for command 'U' / 0x55)
    bool has_upload_partial_request;
    uint16_t upload_partial_start_addr;
    std::vector<uint16_t> upload_partial_segments;
    
    // Push config (for 'D' command BASEâ†’UNIT)
    bool has_push_config;
    unsigned char config_packet[38];
    uint32_t config_target_macid;
    uint8_t config_time_block;
    uint32_t config_crc32;
    bool config_crc_valid;
    uint8_t rssi_threshold;
    uint8_t rssi_delay;
    uint8_t rssi_increment;
    uint8_t power_adjust;
    
    // Constructor
    CommandResponse() : packet_valid(false), crc_valid(false), hops(0), 
                       source_macid(0), unit_id(0), direction(UNKNOWN),
                       has_header_info(false), has_command_params(false),
                       command_code(0), command_hops(0), command_macid(0),
                       command_count(0), erase_age(0), rssi_value(0), 
                       ambient_rssi(0), ram_corruption_reset_count(0),
                       firmware(0), on_deck_crc(0), session_id_command(0),
                       fips_status(0), dest_macid(0), has_upload_data(false),
                       is_fast_mode(false), upload_segment_addr(0),
                       has_upload_partial_request(false), upload_partial_start_addr(0),
                       has_push_config(false), config_target_macid(0),
                       config_time_block(0), config_crc32(0), config_crc_valid(false),
                       rssi_threshold(0), rssi_delay(0), rssi_increment(0), power_adjust(0),
                       sample_capture_segments(0), sample_channel(0), sample_decimation(0),
                       advanced_checksum(false),
                       sample_tach_delay(0), sample_dc_control(0), sample_wakeup_delay(0),
                       sample_bluewave_interval(0),
                       sample_length(0), sample_rate(0.0),
                       datasets_processed(0), packet_correction(0), on_deck_dataset_count(0),
                       pi_time_year(0), pi_time_month(0), pi_time_day(0),
                       pi_time_hour(0), pi_time_min(0), pi_spi_restart_count(0),
                       global_power_control(0), reboot_count(0), undervoltage_count(0),
                       header_debug(0), header_bleon(0), header_fpgaon(0),
                       header_mincount(0), header_failcount(0),
                       descriptor_rms_only(false), descriptor_sample_rate(0),
                       descriptor_channel_mask(0), descriptor_length_code(0),
                       descriptor_sample_length(0)
    {
        for (int i = 0; i < 128; i++) data[i] = 0;
        for (int i = 0; i < 11; i++) version[i] = 0;
        for (int i = 0; i < 16; i++) {
            buf_data[i] = 0;
            buf_spread[i] = 0;
            buf_tach[i] = 0;
        }
        for (int i = 0; i < 32; i++) upload_data[i] = 0;
        for (int i = 0; i < 38; i++) config_packet[i] = 0;
        for (int i = 0; i < 10; i++) command_params[i] = 0;
    }

};

// Command info structure
struct CommandInfo {
    char code;
    std::string name;
    std::string description;
    PacketDirection typical_direction;  // Typical direction this command is used
};

// Constants
#define BROADCAST_MAC 0xFFFFFFFF

// Command processor - facade/wrapper for CommandTransmitter and CommandReceiver
class CommandProcessor {
public:
    // Constructor
    CommandProcessor(CTS1X* core, char* buffer, int* input_cnt, int* output_cnt);
    ~CommandProcessor();
    
    // Transmit operations (delegate to CommandTransmitter)
    void send_command(const unsigned char* cmd_buffer, int length);
    bool make_command(unsigned char* output, int command, uint32_t macid, const unsigned char* body_data = nullptr, const Sampleset* sampleset = nullptr);
    void print_tx_command(const unsigned char* data, int length);
    bool make_erase_command(unsigned char* output, uint8_t age);
    
    // Receive operations (delegate to CommandReceiver)
    void print_command();
    CommandResponse parse_response();
    void print_response(const CommandResponse& response);
    void set_print_upload_data(bool enable);
    
    // Static utility functions
    static const CommandInfo* get_command_info(char command_code);
    static PacketDirection determine_direction(const unsigned char* data, char command_code);
    static bool is_header_info_present(const unsigned char* data);
    static std::string get_direction_string(PacketDirection dir);
    static std::string hex_dump_buffer(const unsigned char* buffer, int length);
    static std::string sanitize_string(const char* str, size_t max_len = 256);
    static uint16_t calculate_checksum(const unsigned char* data, int length);
    
private:
    CTS1X* ts1x_core;
    char* ibuf;
    int* icnt;
    int* ocnt;
    
    CommandTransmitter* transmitter;
    CommandReceiver* receiver;
    
    // Command registry
    static std::map<char, CommandInfo> init_command_registry();
    static const std::map<char, CommandInfo> command_registry;
};

#endif // COMMAND_PROCESSOR_H
