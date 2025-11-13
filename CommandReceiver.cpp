#include "CommandReceiver.h"
#include "CommandReceiverSubs.h"
#include "logger.h"
#include "buffer_constants.h"
#include <cctype>
#include "SensorConversions.h"

CommandReceiver::CommandReceiver(char* buffer, int* input_cnt, int* output_cnt)
    : ibuf(buffer), icnt(input_cnt), ocnt(output_cnt), print_upload_data_samples(false)
{
}

void CommandReceiver::print_command()
{
    //printf("command receiver icnt/ocnt %d,%d, clipped ocnt %d\n",*icnt,*ocnt,(*ocnt + COMMAND_START) & IBUF_MASK);
    char cmd = ibuf[(*ocnt + COMMAND_START) & IBUF_MASK];
    cmd = tolower(cmd);
    
    unsigned char temp_buffer[128];
    for (int idx = 0; idx < 128; ++idx) {
        temp_buffer[idx] = ibuf[(*ocnt + idx) & IBUF_MASK];
    }
    
    // Determine direction for logging
    PacketDirection dir = CommandProcessor::determine_direction(temp_buffer, cmd);
    
    std::string data = CommandProcessor::hex_dump_buffer(temp_buffer, 128);
    
    const CommandInfo* info = CommandProcessor::get_command_info(cmd);
    if (info != nullptr) {
        LOG_INFO_CTX("cmd_receiver", "RX Command: %c [%s] %s, Data: %s", 
                     cmd, info->name.c_str(), 
                     CommandProcessor::get_direction_string(dir).c_str(),
                     data.c_str());
    } else {
        LOG_INFO_CTX("cmd_receiver", "RX Command: %c [UNKNOWN] %s, Data: %s", 
                     cmd, CommandProcessor::get_direction_string(dir).c_str(), data.c_str());
    }
}




// Complete corrected parse_response() function for CommandReceiver.cpp
// This version properly distinguishes between BASE→UNIT and UNIT→BASE packets

CommandResponse CommandReceiver::parse_response()
{
    CommandResponse response;

    // Copy all 128 bytes of data
    for (int idx = 0; idx < PACKET_LENGTH; ++idx) {
        response.data[idx] = ibuf[(*ocnt + idx) & IBUF_MASK];
    }

    // Validate packet structure (header "tS" and tail "uP")
    response.packet_valid = (response.data[HEADER_OFFSET] == HEADER_BYTE1 &&
                            response.data[HEADER_OFFSET + 1] == HEADER_BYTE2 &&
                            response.data[TAIL_OFFSET] == TAIL_BYTE1 &&
                            response.data[TAIL_OFFSET + 1] == TAIL_BYTE2);

    // Parse basic header (bytes 0-12)
    response.hops = response.data[2];
    response.source_macid = ((uint32_t)response.data[3] << 24) |
                           ((uint32_t)response.data[4] << 16) |
                           ((uint32_t)response.data[5] << 8) |
                           (uint32_t)response.data[6];

    // Get command code for direction determination
    response.command_code = response.data[45];
    
    // Determine packet direction (using MAC address as primary indicator)
    response.direction = CommandProcessor::determine_direction(response.data, response.command_code);
    
    // Get command info from registry
    const CommandInfo* cmd_info = CommandProcessor::get_command_info(response.command_code);
    if (cmd_info != nullptr) {
        response.command_name = cmd_info->name;
        response.command_description = cmd_info->description;
    } else {
        response.command_name = "UNKNOWN";
        response.command_description = "Unknown command";
    }
    
    // Verify checksum based on packet type
    if (response.command_code == '3') {
        // DATA_UPLOAD packet - has special checksum (bytes 51-114 for SLOW, 5-124 for FAST)
        bool is_fast = (response.data[2] == 0x80);
        response.crc_valid = CommandReceiverSubs::verify_upload_checksum(response.data, is_fast);
    } else if (response.direction == BASE_TO_UNIT) {
        // BASE→UNIT commands don't have checksums (they're ASCII parameter strings)
        response.crc_valid = true;  // Mark as N/A (valid by default)
    } else {
        // Other UNIT→BASE responses - no checksum validation needed for most
        response.crc_valid = true;
    }
    
    // Determine if header info is present - ONLY for UNIT→BASE responses
    // BASE→UNIT commands have routing info in bytes 13-44, not sensor data
    response.has_header_info = (response.direction == UNIT_TO_BASE) && 
                               CommandProcessor::is_header_info_present(response.data);
    
    if (response.has_header_info) {
        // Parse header info (bytes 13-44 = 32 bytes) - UNIT→BASE only
        response.header_info.reserved1 = response.data[13];
        response.header_info.reserved2 = response.data[14];
        response.header_info.marker = response.data[15];
        
        // Delta is 3 bytes (24 bits)
        response.header_info.delta = ((uint32_t)response.data[16] << 16) |
                                     ((uint32_t)response.data[17] << 8) |
                                     (uint32_t)response.data[18];
        
        response.header_info.data_control_bits = response.data[19];
        
        response.header_info.macid = ((uint32_t)response.data[20] << 24) |
                                     ((uint32_t)response.data[21] << 16) |
                                     ((uint32_t)response.data[22] << 8) |
                                     (uint32_t)response.data[23];
        
        response.header_info.descriptor = ((uint16_t)response.data[24] << 8) |
                                         (uint16_t)response.data[25];
        
        response.header_info.dataset_pi_time.year = ((uint16_t)response.data[26] << 8) |
                                                     (uint16_t)response.data[27];
        response.header_info.dataset_pi_time.month = response.data[28];
        response.header_info.dataset_pi_time.day = response.data[29];
        response.header_info.dataset_pi_time.hour = response.data[30];
        response.header_info.dataset_pi_time.min = response.data[31];
        response.header_info.dataset_pi_time.sec = response.data[32];
        
        response.header_info.current_mistlx_time = ((uint32_t)response.data[33] << 24) |
                                                   ((uint32_t)response.data[34] << 16) |
                                                   ((uint32_t)response.data[35] << 8) |
                                                   (uint32_t)response.data[36];
        
        response.header_info.data_collection_time = ((uint32_t)response.data[37] << 24) |
                                                    ((uint32_t)response.data[38] << 16) |
                                                    ((uint32_t)response.data[39] << 8) |
                                                    (uint32_t)response.data[40];
        
        response.header_info.battery = response.data[41];
        
        response.header_info.temperature = ((uint16_t)response.data[42] << 8) |
                                          (uint16_t)response.data[43];
        
        response.header_info.rssi = response.data[44];

        // Decode descriptor fields
        CommandReceiverSubs::decode_descriptor(response);
        
        // Copy unit_id to top-level field for easier access
        response.unit_id = response.header_info.macid;
    }

    // Parse command parameters for BASE→UNIT commands
    CommandReceiverSubs::parse_command_params(response);

    // Command fields always start at byte 45
    int offset = 45;
    
    response.command_hops = response.data[offset + 1];
    response.command_macid = ((uint32_t)response.data[offset + 2] << 24) |
                            ((uint32_t)response.data[offset + 3] << 16) |
                            ((uint32_t)response.data[offset + 4] << 8) |
                            (uint32_t)response.data[offset + 5];
    
    response.command_count = response.data[offset + 10];

    // Parse age field for 'E' (erase) command (byte 46)
    if (response.command_code == 'E' || response.command_code == 'e') {
        uint8_t encoded_age = response.data[46];
        // Decode: remove top 2 bits (0xC0 mask) to get actual age
        response.erase_age = encoded_age & 0x3f;  // Extract bottom 6 bits
    } else {
        response.erase_age = 0;
    }

    // Parse upload data if this is a '3' command
    CommandReceiverSubs::parse_upload_data(response);

    // Parse upload partial request if this is a 'U' (0x55) command
    CommandReceiverSubs::parse_upload_partial_request(response);

    // Parse push config if this is a 'D' (broadcast) command
    CommandReceiverSubs::parse_push_config(response);

    // =========================================================================
    // ONLY parse UNIT→BASE response fields for UNIT→BASE packets
    // BASE→UNIT commands have ASCII parameters here, not sensor data!
    // =========================================================================
    
    if (response.direction == UNIT_TO_BASE) {
        // Parse version (10 bytes)
        for (int i = 0; i < 10; i++) {
            response.version[i] = response.data[offset + 11 + i];
        }
        response.version[10] = '\0';
        
        // Parse unit type and firmware version from version string
        CommandReceiverSubs::parse_version_string(response);

        // Parse RSSI values
        response.rssi_value = response.data[offset + 21];
        response.ambient_rssi = response.data[offset + 22];
        response.ram_corruption_reset_count = response.data[offset + 23];

        // Parse firmware and on_deck_crc
        response.firmware = response.data[offset + 24];
        response.on_deck_crc = ((uint32_t)response.data[offset + 25] << 24) |
                              ((uint32_t)response.data[offset + 26] << 16) |
                              ((uint32_t)response.data[offset + 27] << 8) |
                              (uint32_t)response.data[offset + 28];

        // Parse buf_data array (8 x 2 bytes = 16 bytes, bytes 74-89)
        for (int i = 0; i < 8; i++) {
            int idx = 74 + (i * 2);
            response.buf_data[i] = ((uint16_t)response.data[idx] << 8) |
                                   (uint16_t)response.data[idx + 1];
        }
        for (int i = 8; i < 16; i++) {
            response.buf_data[i] = 0;
        }

        // Parse buf_spread array (8 x 2 bytes = 16 bytes, bytes 90-105)
        for (int i = 0; i < 8; i++) {
            int idx = 90 + (i * 2);
            response.buf_spread[i] = ((uint16_t)response.data[idx] << 8) |
                                     (uint16_t)response.data[idx + 1];
        }
        for (int i = 8; i < 16; i++) {
            response.buf_spread[i] = 0;
        }

        // Parse buf_tach array (8 x 2 bytes = 16 bytes, bytes 106-121)
        for (int i = 0; i < 8; i++) {
            int idx = 106 + (i * 2);
            response.buf_tach[i] = ((uint16_t)response.data[idx] << 8) |
                                   (uint16_t)response.data[idx + 1];
        }
        for (int i = 8; i < 16; i++) {
            response.buf_tach[i] = 0;
        }
        // Decode buf_data fields
        response.datasets_processed = response.buf_data[0];
        response.packet_correction = response.buf_data[1];
        response.on_deck_dataset_count = response.buf_data[2];
        response.pi_time_year = response.buf_data[3];
        response.pi_time_month = (response.buf_data[4] >> 8) & 0xFF;
        response.pi_time_day = response.buf_data[4] & 0xFF;
        response.pi_time_hour = (response.buf_data[5] >> 8) & 0xFF;
        response.pi_time_min = response.buf_data[5] & 0xFF;
        response.pi_spi_restart_count = (response.buf_data[6] >> 8) & 0xFF;
        response.global_power_control = response.buf_data[6] & 0xFF;
        response.reboot_count = (response.buf_data[7] >> 8) & 0xFF;
        response.undervoltage_count = response.buf_data[7] & 0xFF;
        
        // Decode buf_spread fields
        response.header_debug = response.buf_spread[0];
        response.header_bleon = response.buf_spread[1];
        response.header_fpgaon = response.buf_spread[2];
        response.header_mincount = ((uint32_t)response.buf_spread[3] << 16) | 
                                   response.buf_spread[4];
        response.header_failcount = ((uint32_t)response.buf_spread[5] << 16) | 
                                    response.buf_spread[6];
        // Parse session and status
        response.session_id_command = ((uint16_t)response.data[123] << 8) |
                                     (uint16_t)response.data[124];
        response.fips_status = response.data[125];
    } else {
        // BASE→UNIT commands - initialize fields to safe defaults
        memset(response.version, 0, sizeof(response.version));
        response.unit_type = "";
        response.firmware_version = "";
        response.rssi_value = 0;
        response.ambient_rssi = 0;
        response.ram_corruption_reset_count = 0;
        response.firmware = 0;
        response.on_deck_crc = 0;
        
        for (int i = 0; i < 16; i++) {
            response.buf_data[i] = 0;
            response.buf_spread[i] = 0;
            response.buf_tach[i] = 0;
        }
        
        response.session_id_command = 0;
        response.fips_status = 0;
    }

    response.dest_macid = 0;

    return response;
}



// Corrected print_response function for CommandReceiver.cpp
// Replace lines 426-674 with this implementation

void CommandReceiver::print_response(const CommandResponse& response)
{
    // Basic validation
    LOG_INFO_CTX("cmd_receiver", "=== Response Packet ===");
    
    // Check if this is a bidirectional command and adjust name/description
    std::string cmd_name = response.command_name;
    std::string cmd_desc = response.command_description;

    if ((response.command_code == 'D' || response.command_code == 'd')) {
        if (response.source_macid == BROADCAST_MAC) {
            // BASE→UNIT direction
            cmd_name = "PUSH_CONFIG";
            cmd_desc = "Push configuration to units (broadcast)";
        } else {
            // UNIT→BASE direction (keep existing)
            cmd_name = "DATA_RSP";
            cmd_desc = "Data response with sensor readings";
        }
    }
   
    LOG_INFO_CTX("cmd_receiver", "RXParse: '%c' [%s] - %s", 
             response.command_code, 
             cmd_name.c_str(),
             cmd_desc.c_str());

    LOG_INFO_CTX("cmd_receiver", "Direction: %s", CommandProcessor::get_direction_string(response.direction).c_str());
    LOG_INFO_CTX("cmd_receiver", "Valid: %s, CRC: %s", 
                 response.packet_valid ? "YES" : "NO",
                 response.crc_valid ? "YES" : "NO");
    
    // Basic header info
    LOG_INFO_CTX("cmd_receiver", "Source MAC: 0x%08X%s, Hops: %d", 
                 response.source_macid,
                 response.source_macid == BROADCAST_MAC ? " [BROADCAST]" : "",
                 response.hops);
    
    // ========================================================================
    // BASE→UNIT Commands - Show ONLY command parameters, then exit
    // ========================================================================
    if (response.direction == BASE_TO_UNIT) {
        // Command parameters (for BASE→UNIT commands like 'R', 'A', 'U', 'D', etc.)
        if (response.has_command_params) {
            LOG_INFO_CTX("cmd_receiver", "--- Command Parameters (BASE→UNIT) ---");
            
            // For 'R' (SAMPLE_DATA) command, decode parameters
            if (response.command_code == 'R' || response.command_code == 'r') {
                LOG_INFO_CTX("cmd_receiver", "  Target MAC ID: 0x%08X", response.command_macid);
                LOG_INFO_CTX("cmd_receiver", "  Capture Segments: %u", response.sample_capture_segments);
                LOG_INFO_CTX("cmd_receiver", "  Sample Length: %u samples", response.sample_length);
                LOG_INFO_CTX("cmd_receiver", "  Sample Rate: %.2f Hz", response.sample_rate);
                LOG_INFO_CTX("cmd_receiver", "  Sample Channel: %u (%02x)", response.sample_channel,response.sample_channel);
                LOG_INFO_CTX("cmd_receiver", "  Decimation: %u (%02x)", response.sample_decimation,response.sample_decimation);
                LOG_INFO_CTX("cmd_receiver", "  Advanced Checksum: %s (%u)", response.advanced_checksum ? "ENABLED" : "DISABLED", response.advanced_checksum);
                LOG_INFO_CTX("cmd_receiver", "  Tach Delay: %u", response.sample_tach_delay);
                LOG_INFO_CTX("cmd_receiver", "  DC Control: 0x%08X", response.sample_dc_control);
                LOG_INFO_CTX("cmd_receiver", "  Wakeup Delay: %u", response.sample_wakeup_delay);
                LOG_INFO_CTX("cmd_receiver", "  Bluewave Interval: %u", response.sample_bluewave_interval);
            } else {
                // For other commands, just show raw params
                LOG_INFO_CTX("cmd_receiver", "  Raw Parameters:");
                for (int i = 0; i < 10; i++) {
                    LOG_INFO_CTX("cmd_receiver", "    param[%d]: 0x%08X (%u)", 
                                i, response.command_params[i], response.command_params[i]);
                }
            }
        }
        
        // For ERASE_CFG ('E') commands, show age parameter
        if (response.command_code == 'E' || response.command_code == 'e') {
            LOG_INFO_CTX("cmd_receiver", "--- Command Fields ---");
            LOG_INFO_CTX("cmd_receiver", "  Erase Age: %d (encoded byte: 0x%02X)", 
                         response.erase_age, (response.erase_age & 0x3f) | 0xc0);
        }
        
        // For UPLOAD_PARTIAL ('U'/0x55) commands, show requested segments
        if ((response.command_code == 'U' || response.command_code == 0x55) && 
            response.has_upload_partial_request) {
            LOG_INFO_CTX("cmd_receiver", "--- Upload Partial Request ---");
            LOG_INFO_CTX("cmd_receiver", "  Start Address: %d (0x%04X)", 
                        response.upload_partial_start_addr,
                        response.upload_partial_start_addr);
            LOG_INFO_CTX("cmd_receiver", "  Segments Requested: %zu", 
                        response.upload_partial_segments.size());
            
            // Always show the segment list (up to 32 segments per line)
            if (!response.upload_partial_segments.empty()) {
                std::string line = "  Segments: ";
                for (size_t i = 0; i < response.upload_partial_segments.size(); i++) {
                    if (i > 0 && i % 32 == 0) {
                        LOG_INFO_CTX("cmd_receiver", "%s", line.c_str());
                        line = "            ";
                    }
                    char seg_str[10];
                    snprintf(seg_str, sizeof(seg_str), "%d ", response.upload_partial_segments[i]);
                    line += seg_str;
                }
                if (!line.empty() && line != "            ") {
                    LOG_INFO_CTX("cmd_receiver", "%s", line.c_str());
                }
                
                // Show sample ranges for first few segments
                LOG_INFO_CTX("cmd_receiver", "  Sample Ranges:");
                size_t max_ranges = std::min(response.upload_partial_segments.size(), (size_t)20);
                for (size_t i = 0; i < max_ranges; i++) {
                    uint16_t seg = response.upload_partial_segments[i];
                    uint32_t start_sample = seg * 32;
                    uint32_t end_sample = start_sample + 31;
                    
                    if (i % 4 == 0) {
                        line = "    ";
                    }
                    char range_str[30];
                    snprintf(range_str, sizeof(range_str), "[%d:%u-%u] ", 
                            seg, start_sample, end_sample);
                    line += range_str;
                    
                    if ((i + 1) % 4 == 0 || i == max_ranges - 1) {
                        LOG_INFO_CTX("cmd_receiver", "%s", line.c_str());
                    }
                }
                
                if (response.upload_partial_segments.size() > 20) {
                    LOG_INFO_CTX("cmd_receiver", "    ... and %zu more segments",
                                response.upload_partial_segments.size() - 20);
                }
            }
        }
        
        // For PUSH_CONFIG ('D' from BASE) commands, show configuration
        if (response.command_code == 'D' && response.has_push_config) {
            LOG_INFO_CTX("cmd_receiver", "--- PUSH CONFIG (Broadcast) ---");
            LOG_INFO_CTX("cmd_receiver", "  Target MAC ID: 0x%08X", response.config_target_macid);
            LOG_INFO_CTX("cmd_receiver", "  Time Block: %u (shifted value: %u)", 
                        response.config_time_block, response.config_time_block);
            LOG_INFO_CTX("cmd_receiver", "  CRC32: 0x%08X [%s]",
                        response.config_crc32,
                        response.config_crc_valid ? "VALID" : "INVALID");
            
            LOG_INFO_CTX("cmd_receiver", "  RSSI Parameters:");
            LOG_INFO_CTX("cmd_receiver", "    Threshold: %d (0x%02X)", 
                (signed char)response.rssi_threshold, response.rssi_threshold);
            LOG_INFO_CTX("cmd_receiver", "    Delay: %u (0x%02X)", 
                        response.rssi_delay, response.rssi_delay);
            LOG_INFO_CTX("cmd_receiver", "    Increment: %u (0x%02X)", 
                        response.rssi_increment, response.rssi_increment);
            LOG_INFO_CTX("cmd_receiver", "    Power Adjust: %d (0x%02X)", 
                        (int8_t)response.power_adjust, response.power_adjust);
            
            if (print_upload_data_samples) {
                // Print config packet as hex string
                LOG_INFO_CTX("cmd_receiver", "  Config Packet (38 bytes):");
                std::string hex_line = "    ";
                for (int i = 0; i < 38; i++) {
                    if (i > 0 && i % 16 == 0) {
                        LOG_INFO_CTX("cmd_receiver", "%s", hex_line.c_str());
                        hex_line = "    ";
                    }
                    char hex_byte[4];
                    snprintf(hex_byte, sizeof(hex_byte), "%02X ", response.config_packet[i]);
                    hex_line += hex_byte;
                }
                if (!hex_line.empty() && hex_line != "    ") {
                    LOG_INFO_CTX("cmd_receiver", "%s", hex_line.c_str());
                }
            }
        }
        
        LOG_INFO_CTX("cmd_receiver", "======================");
        return;  // EXIT - Do not parse UNIT→BASE fields for BASE→UNIT commands
    }
    
    // ========================================================================
    // UNIT→BASE Responses - Show header info, sensor data, version, etc.
    // ========================================================================
    
    // Header information (sensor data from unit)
    if (response.has_header_info) {
        LOG_INFO_CTX("cmd_receiver", "--- Header Info (Sensor Data) ---");
        LOG_INFO_CTX("cmd_receiver", "  Marker: 0x%02X, Delta: %u", 
                     response.header_info.marker, response.header_info.delta);
        LOG_INFO_CTX("cmd_receiver", "  Unit ID: 0x%08X", response.unit_id);
        
        // Check if data is ready for upload
        bool data_ready = (response.header_info.data_control_bits != 0x00);
        
        LOG_INFO_CTX("cmd_receiver", "  Data Control Bits: 0x%02X", 
                     response.header_info.data_control_bits);
        LOG_INFO_CTX("cmd_receiver", "  Data Ready for Upload: %s", 
                     data_ready ? "YES" : "NO");
        LOG_INFO_CTX("cmd_receiver", "  Dataset Time: %04d-%02d-%02d %02d:%02d:%02d",
                     response.header_info.dataset_pi_time.year,
                     response.header_info.dataset_pi_time.month,
                     response.header_info.dataset_pi_time.day,
                     response.header_info.dataset_pi_time.hour,
                     response.header_info.dataset_pi_time.min,
                     response.header_info.dataset_pi_time.sec);
        LOG_INFO_CTX("cmd_receiver", "  Mistlx Time: 0x%08X, Collection Time: 0x%08X",
                     response.header_info.current_mistlx_time,
                     response.header_info.data_collection_time);
                     
        int8_t rssi_signed = (int8_t)response.header_info.rssi;
        // Calculate temperature (Fahrenheit) and battery voltage
        float battery_voltage = SensorConversions::battery_to_voltage(response.header_info.battery);
        double temperature_f = SensorConversions::temperature_to_fahrenheit(response.header_info.temperature);

        
        LOG_INFO_CTX("cmd_receiver", "  Battery: %.1fV, Temperature: %.1fF, RSSI: %ddBm",
                     battery_voltage,
                     temperature_f,
                     rssi_signed);

        // Decoded descriptor fields
        if (response.descriptor_channel_mask != 0) {  // 0x0 means disabled
            LOG_INFO_CTX("cmd_receiver", "--- Descriptor Details (0x%04X) ---",
                         response.header_info.descriptor);
            LOG_INFO_CTX("cmd_receiver", "  Data Length: %d samples (code=0x%02X)",
                         response.descriptor_sample_length, 
                         response.descriptor_length_code);
            LOG_INFO_CTX("cmd_receiver", "  Sample Rate: %s (code=%d)",
                         response.descriptor_sample_rate_str.c_str(),
                         response.descriptor_sample_rate);
            
            // Build channel string
            std::string channels = "";
            if (response.descriptor_channel_mask & 0x01) channels += "Ultrasonic ";
            if (response.descriptor_channel_mask & 0x02) channels += "X ";
            if (response.descriptor_channel_mask & 0x04) channels += "Y ";
            if (response.descriptor_channel_mask & 0x08) channels += "Z ";
            if (channels.empty()) channels = "None";
            
            LOG_INFO_CTX("cmd_receiver", "  Channels: %s (mask=0x%X)",
                         channels.c_str(), response.descriptor_channel_mask);
            LOG_INFO_CTX("cmd_receiver", "  Mode: %s",
                         response.descriptor_rms_only ? "RMS Only" : "Raw Data");
        } else {
            LOG_INFO_CTX("cmd_receiver", "--- Descriptor (0x%04X): DISABLED ---",
                         response.header_info.descriptor);
        }
    }
    
    // Command fields (for UNIT→BASE responses)
    LOG_INFO_CTX("cmd_receiver", "--- Command Fields ---");
    LOG_INFO_CTX("cmd_receiver", "  Hops: %d, MAC: 0x%08X, Count: %d",
                 response.command_hops, response.command_macid, response.command_count);
    
    // For DATA_UPLOAD ('3') commands, show segment info and optionally data
    if (response.command_code == '3' && response.has_upload_data) {
        LOG_INFO_CTX("cmd_receiver", "  Upload Segment Address: %d (0x%04X) [%s mode]", 
                     response.upload_segment_addr, 
                     response.upload_segment_addr,
                     response.is_fast_mode ? "FAST" : "SLOW");
        
        if (print_upload_data_samples) {
            // Print data in rows of 8 samples
            for (int row = 0; row < 4; row++) {
                std::string line = "    Data[" + std::to_string(row * 8) + "-" + 
                                   std::to_string(row * 8 + 7) + "]: ";
                for (int col = 0; col < 8; col++) {
                    int idx = row * 8 + col;
                    char hex_str[10];
                    snprintf(hex_str, sizeof(hex_str), "0x%04X ", 
                            (uint16_t)response.upload_data[idx]);
                    line += hex_str;
                }
                LOG_INFO_CTX("cmd_receiver", "%s", line.c_str());
            }
        }
    }
    
    // Sanitize version string for display
    std::string clean_version = CommandProcessor::sanitize_string(response.version);
    
    // For ACK_INIT ('1') responses, highlight unit info
    if (response.command_code == '1') {
        LOG_INFO_CTX("cmd_receiver", "  Unit ID: 0x%08X", response.command_macid);
        LOG_INFO_CTX("cmd_receiver", "  Unit Type: %s", 
                     response.unit_type.empty() ? "N/A" : response.unit_type.c_str());
        LOG_INFO_CTX("cmd_receiver", "  Firmware Version: %s", 
                     response.firmware_version.empty() ? "N/A" : response.firmware_version.c_str());
        LOG_INFO_CTX("cmd_receiver", "  Full Version String: %s", clean_version.c_str());
    } else {
        LOG_INFO_CTX("cmd_receiver", "  Version: %s", clean_version.c_str());
    }
    
    LOG_INFO_CTX("cmd_receiver", "  RSSI: %d, Ambient RSSI: %d, Reset Count: %d",
                 response.rssi_value, response.ambient_rssi, response.ram_corruption_reset_count);
    LOG_INFO_CTX("cmd_receiver", "  Firmware: 0x%02X, On-Deck CRC: 0x%08X %s",
                 response.firmware, response.on_deck_crc,
                 response.on_deck_crc ? "[DATA READY]" : "");
    LOG_INFO_CTX("cmd_receiver", "  Session ID: 0x%04X, FIPS Status: 0x%02X",
                 response.session_id_command, response.fips_status);
    
    // Decoded system status fields
    LOG_INFO_CTX("cmd_receiver", "--- System Status ---");
    LOG_INFO_CTX("cmd_receiver", "  Datasets: processed=%d, correction=%d, on_deck=%d",
                 response.datasets_processed, response.packet_correction, 
                 response.on_deck_dataset_count);
    LOG_INFO_CTX("cmd_receiver", "  Pi Time: %04d-%02d-%02d %02d:%02d",
                 response.pi_time_year, response.pi_time_month, response.pi_time_day,
                 response.pi_time_hour, response.pi_time_min);
    LOG_INFO_CTX("cmd_receiver", "  Restarts: SPI=%d, Power=%d, Reboot=%d, Undervoltage=%d",
                 response.pi_spi_restart_count, response.global_power_control,
                 response.reboot_count, response.undervoltage_count);
    LOG_INFO_CTX("cmd_receiver", "  Hardware: debug=0x%04X, BLE=%d, FPGA=%d",
                 response.header_debug, response.header_bleon, response.header_fpgaon);
    LOG_INFO_CTX("cmd_receiver", "  Counts: mincount=%u, failcount=%u",
                 response.header_mincount, response.header_failcount);
    
    LOG_INFO_CTX("cmd_receiver", "======================");
}




