#include "CommandReceiverSubs.h"
#include "CommandReceiver.h"
#include "logger.h"
#include "buffer_constants.h"
#include <cstring>
#include <cmath>

namespace CommandReceiverSubs {

void parse_version_string(CommandResponse& response)
{
    // Parse unit type and firmware version from version string
    // Format is typically: "TSX_7CHv85" or similar
    // Unit type: everything before 'v', Firmware: 'v' and after
    std::string version_str(response.version);
    size_t v_pos = version_str.find('v');
    if (v_pos != std::string::npos) {
        response.unit_type = version_str.substr(0, v_pos);
        response.firmware_version = version_str.substr(v_pos);
    } else {
        response.unit_type = version_str;
        response.firmware_version = "";
    }
}

void parse_command_params(CommandResponse& response)
{
    // Parse command parameters for BASE→UNIT commands
    // Parameters are 10x 8-character ASCII hex strings starting at byte 46
    // Each param is stored as 4 bytes (32-bit integer)
    
    if (response.direction != BASE_TO_UNIT) {
        return;  // Only parse params for BASE→UNIT commands
    }
    
    response.has_command_params = true;
    
    for (int i = 0; i < 10; i++) {
        int offset = 46 + (i * 8);
        char hex_str[9];
        
        // Extract 8 ASCII hex characters
        for (int j = 0; j < 8; j++) {
            hex_str[j] = response.data[offset + j];
        }
        hex_str[8] = '\0';
        
        // Convert ASCII hex to integer
        uint32_t value = 0;
        sscanf(hex_str, "%x", &value);
        response.command_params[i] = value;
    }
    
    // Decode specific parameters for 'R' (SAMPLE_DATA) command
    if (response.command_code == 'R' || response.command_code == 'r') {
        // params[0] = capture_segments
        response.sample_capture_segments = response.command_params[0];
        
        // params[1] = sample_channel + (1<<12) + (task_sample_decimation<<8) + (analyzer_server_tach_delay<<16)
        // Bit layout:
        //   Bits 0-7:   sample_channel (8 bits)
        //   Bits 8-11:  decimation (4 bits, range 0-15)
        //   Bit 12:     advanced_checksum flag
        //   Bits 13-15: unused
        //   Bits 16-31: tach_delay (16 bits)
        uint32_t combined = response.command_params[1];
        response.sample_channel = combined & 0xFF;
        response.sample_decimation = (combined >> 8) & 0x0F;      // Only 4 bits for decimation!
        response.advanced_checksum = (combined >> 12) & 0x01;     // Bit 12 is advanced checksum flag
        response.sample_tach_delay = (combined >> 16) & 0xFFFF;
        
        // params[2] = dc_control
        response.sample_dc_control = response.command_params[2];
        
        // params[3] = wakeup_delay<<16
        response.sample_wakeup_delay = (response.command_params[3] >> 16) & 0xFFFF;
        
        // params[4] = bluewave_interval
        response.sample_bluewave_interval = response.command_params[4];
        
        // Calculate derived parameters
        // sample_length = sample_capture_segments * 16
        response.sample_length = response.sample_capture_segments * 16;
        
        // sample_rate = 20000.0 / (2^(sample_decimation - 1))
        if (response.sample_decimation > 0) {
            response.sample_rate = 20000.0 / pow(2.0, (double)(response.sample_decimation - 1));
        } else {
            // Handle edge case where decimation is 0
            response.sample_rate = 20000.0;
        }
    }
}

void parse_upload_data(CommandResponse& response)
{
    // Check if this is a data upload packet (command '3')
    if (response.command_code != '3') {
        response.has_upload_data = false;
        return;
    }
    
    // Detect FAST vs SLOW mode
    response.is_fast_mode = (response.data[2] == 0x80);
    
    // Verify checksum FIRST
    if (!verify_upload_checksum(response.data, response.is_fast_mode)) {
        LOG_WARN_CTX("cmd_receiver", "Upload data checksum verification failed");
        response.has_upload_data = false;
        response.crc_valid = false;  // Mark as invalid
        return;
    }
    
    if (response.is_fast_mode) {
        // FAST format:
        // Bytes 3-4: segment address (big endian)
        // Bytes 5-124: 120 bytes of packed data (64x 15-bit samples)
        
        response.upload_segment_addr = ((uint16_t)response.data[3] << 8) | response.data[4];
        
        // Decode using the FAST algorithm
        int16_t samples[64];
        int sample_idx = 0;
        int save_first = 0;
        int lcnt = 0;
        
        for (int i = 0; i < 64; i++) {
            if ((i & 0xf) == 0) {
                save_first = 0;
                sample_idx++;  // Skip first sample of each group of 16
            } else {
                int ret = ((response.data[5 + (lcnt * 2)] << 8) & 0xff00) |
                          (response.data[5 + (lcnt * 2) + 1] & 0xff);
                
                if (ret & 1) {
                    save_first += 0x8000;  // Transfer bit from saved sample
                }
                
                ret &= 0xfffe;  // Clear lower bit - it's trash in this mode
                
                if (ret & 2) {
                    ret++;  // Dithering function
                }
                
                samples[sample_idx] = ret - 32768;  // Convert to signed
                
                if ((i & 0xf) == 0xf) {
                    samples[sample_idx - 15] = save_first - 32768;
                }
                
                save_first >>= 1;
                lcnt++;
                sample_idx++;
            }
        }
        
        // Store first 32 samples (FAST mode returns 64, we only store first segment)
        for (int i = 0; i < 32; i++) {
            response.upload_data[i] = samples[i];
        }
        
    } else {
        // SLOW format:
        // Byte 45: 0x33 (command)
        // Bytes 47-48: segment address (big endian)
        // Bytes 51-114: 64 bytes (32x 16-bit samples, big endian)
        
        if (response.data[45] != 0x33) {
            LOG_WARN_CTX("cmd_receiver_sub", "Invalid SLOW upload command byte: 0x%02X", response.data[45]);
            response.has_upload_data = false;
            return;
        }
        
        response.upload_segment_addr = ((uint16_t)response.data[47] << 8) | response.data[48];
        
        // Parse 32 samples (64 bytes)
        for (int i = 0; i < 32; i++) {
            int offset = 51 + (i * 2);
            response.upload_data[i] = ((int16_t)response.data[offset] << 8) | response.data[offset + 1];
        }
    }
    
    response.has_upload_data = true;
}

bool verify_upload_checksum(const unsigned char* data, bool is_fast)
{
    uint16_t basic_checksum = 0;
    
    if (is_fast) {
        // FAST mode: sum bytes 5-124 (120 bytes of data)
        for (int i = 5; i < 125; i++) {
            basic_checksum += data[i];
        }
    } else {
        // SLOW mode: Check if checksum is enabled (version byte at 49 must be 0xBB)
        uint8_t version = data[49] & 0xFF;
        if (version != 0xBB) {
            // Checksum not enabled, consider it valid
            return true;
        }
        
        // SLOW mode: sum data bytes 51-114 (32 pairs = 64 bytes)
        for (int i = 51; i < 115; i++) {
            basic_checksum += data[i];
        }
    }
    
    // Extract MAC address from bytes 3-6
    uint32_t mac = ((uint32_t)data[3] << 24) |
                   ((uint32_t)data[4] << 16) |
                   ((uint32_t)data[5] << 8) |
                   ((uint32_t)data[6]);
    
    // Calculate advanced checksum (basic + MAC bytes)
    uint16_t advanced_checksum = basic_checksum;
    advanced_checksum += (mac >> 24) & 0xFF;
    advanced_checksum += (mac >> 16) & 0xFF;
    advanced_checksum += (mac >> 8) & 0xFF;
    advanced_checksum += mac & 0xFF;
    
    // Get stored checksum from byte 125
    uint8_t stored_checksum = data[125] & 0xFF;
    
    // XOR checksums with 0xAA for comparison (obfuscation)
    uint8_t basic_check = (basic_checksum ^ 0xAA) & 0xFF;
    uint8_t advanced_check = (advanced_checksum ^ 0xAA) & 0xFF;
    
    // Validation passes if EITHER checksum matches
    bool basic_match = (basic_check == stored_checksum);
    bool advanced_match = (advanced_check == stored_checksum);
    
    return (basic_match || advanced_match);
}

void decode_descriptor(CommandResponse& response)
{
    if (!response.has_header_info) {
        return;  // No descriptor to decode
    }
    
    uint16_t descriptor = response.header_info.descriptor;
    
    // Bit 15: RMS Only flag
    response.descriptor_rms_only = (descriptor & 0x8000) != 0;
    
    // Bits 14-12: Sample Rate (3 bits)
    response.descriptor_sample_rate = (descriptor >> 12) & 0x07;
    
    // Bits 11-8: Channel mask (4 bits)
    response.descriptor_channel_mask = (descriptor >> 8) & 0x0F;
    
    // Bits 7-0: Length code
    response.descriptor_length_code = descriptor & 0xFF;
    
    // Calculate actual sample length: (L+1)*256
    response.descriptor_sample_length = (response.descriptor_length_code + 1) * 256;
    
    // Convert sample rate code to human-readable string
    switch (response.descriptor_sample_rate) {
        case 0: response.descriptor_sample_rate_str = "20.0 kHz"; break;
        case 1: response.descriptor_sample_rate_str = "10.0 kHz"; break;
        case 2: response.descriptor_sample_rate_str = "5.0 kHz"; break;
        case 3: response.descriptor_sample_rate_str = "2.5 kHz"; break;
        case 4: response.descriptor_sample_rate_str = "1.25 kHz"; break;
        case 5: response.descriptor_sample_rate_str = "625 Hz"; break;
        case 6: response.descriptor_sample_rate_str = "312.5 Hz"; break;
        case 7: response.descriptor_sample_rate_str = "156.25 Hz"; break;
        default: response.descriptor_sample_rate_str = "Unknown"; break;
    }
}

void parse_upload_partial_request(CommandResponse& response)
{
    // Check if this is upload partial request (command 'U' = 0x55)
    if (response.command_code != 'U' && response.command_code != 0x55) {
        response.has_upload_partial_request = false;
        return;
    }
    
    // Parse Sample Start address (4 ASCII hex chars at bytes 47-50)
    // This is the starting address divided by 32
    char addr_str[5];
    for (int i = 0; i < 4; i++) {
        addr_str[i] = response.data[46 + i];
    }
    addr_str[4] = '\0';
    
    // Convert ASCII hex to integer (this is address/32, so segment number)
    uint16_t start_segment = 0;
    sscanf(addr_str, "%hx", &start_segment);
    response.upload_partial_start_addr = start_segment * 32;  // Convert back to byte address
    
    // Parse bitmask (76 bytes at bytes 51-126)
    // Each byte has format: MMMMMMM1 where M are mask bits and bit 0 (LSB) is always 1
    // Each byte represents 7 segments (bits 7-1, bit 0 ignored)
    // Bit 7 (MSB) = first segment of the group
    // Total possible segments: 76 * 7 = 532 segments
    response.upload_partial_segments.clear();
    //LOG_INFO_CTX("cmd_receiver_sub", "Partial upload start address: %04x",start_segment*32);
    for (int byte_idx = 0; byte_idx < 76; byte_idx++) {
        uint8_t mask = response.data[50 + byte_idx];
        //LOG_INFO_CTX("cmd_receiver_sub", "Check byte %d: %02x",byte_idx,mask);
        
        // Check bits 7 down to 1 (bit 0/LSB is always 1 and ignored)
        // Bit 7 (MSB) corresponds to segment (byte_idx * 7 + 0)
        // Bit 6 corresponds to segment (byte_idx * 7 + 1), etc.
        for (int bit_pos = 7; bit_pos >= 1; bit_pos--) {
            if (mask & (1 << bit_pos)) {
                // Calculate absolute segment number
                uint16_t segment_num = (byte_idx * 7) + (7 - bit_pos);
                //LOG_INFO_CTX("cmd_receiver_sub", "found %d",segment_num);
                response.upload_partial_segments.push_back(segment_num);
            }
        }
    }
    
    response.has_upload_partial_request = true;
}

uint32_t calculate_crc32(const unsigned char* data, int length)
{
    uint32_t crc = 0xFFFFFFFF;
    
    for (int i = 0; i < length; i++) {
        uint32_t byte = data[i];
        crc = crc ^ byte;
        
        for (int j = 7; j >= 0; j--) {
            uint32_t mask = -(crc & 1);
            crc = (crc >> 1) ^ (0xEDB88320 & mask);
        }
    }
    
    return ~crc;
}

void parse_push_config(CommandResponse& response)
{
    // Check if this is a 'D' command going BASE→UNIT (broadcast MAC)
    if (response.command_code != 'D' && response.command_code != 'd') {
        response.has_push_config = false;
        return;
    }
    
    // Check if this is BASE→UNIT direction (source MAC = BROADCAST)
    if (response.source_macid != BROADCAST_MAC) {
        response.has_push_config = false;
        return;
    }
    
    // This is a PUSH_CONFIG command
    // Body starts at byte 46, 80 bytes total
    // Format:
    // - new_config_packet[38] (bytes 46-83)
    // - macid (4 bytes, bytes 84-87)
    // - time_block>>7 (2 bytes, bytes 88-89)
    // - CRC32 (4 bytes big-endian, bytes 90-93)
    // - 0xfa 0xde marker (2 bytes, bytes 94-95)
    // - rssi_threshold (1 byte, byte 96)
    // - rssi_delay (1 byte, byte 97)
    // - rssi_increment (1 byte, byte 98)
    // - power_adjust (1 byte, byte 99)
    // - padding with 0x00 to byte 125
    
    int idx = 46;
    
    // Extract config packet (38 bytes)
    for (int i = 0; i < 38; i++) {
        response.config_packet[i] = response.data[idx++];
    }
    
    // Extract target MAC ID (4 bytes, big-endian)
    response.config_target_macid = ((uint32_t)response.data[idx] << 24) |
                                   ((uint32_t)response.data[idx + 1] << 16) |
                                   ((uint32_t)response.data[idx + 2] << 8) |
                                   (uint32_t)response.data[idx + 3];
    idx += 4;
    
    // Extract time_block (2 bytes, should be same value)
    uint8_t time_block_byte = response.data[idx];
    response.config_time_block = time_block_byte;  // Original value before >>7 shift
    idx += 2;
    
    // Extract stored CRC32 (4 bytes, big-endian)
    response.config_crc32 = ((uint32_t)response.data[idx] << 24) |
                           ((uint32_t)response.data[idx + 1] << 16) |
                           ((uint32_t)response.data[idx + 2] << 8) |
                           (uint32_t)response.data[idx + 3];
    idx += 4;
    
    // Calculate CRC32 over config data (38 bytes + 4 bytes macid + 2 bytes timeblock = 44 bytes)
    uint32_t calculated_crc = calculate_crc32(&response.data[46], 44);
    response.config_crc_valid = (calculated_crc == response.config_crc32);
    
    // Check for RSSI marker (0xfa 0xde)
    if (response.data[idx] == 0xfa && response.data[idx + 1] == 0xde) {
        idx += 2;
        
        // Extract RSSI parameters
        response.rssi_threshold = response.data[idx++];
        response.rssi_delay = response.data[idx++];
        response.rssi_increment = response.data[idx++];
        response.power_adjust = response.data[idx++];
    } else {
        LOG_WARN_CTX("cmd_receiver_sub", "Missing RSSI marker (0xfa 0xde) at position %d", idx);
        response.rssi_threshold = 0;
        response.rssi_delay = 0;
        response.rssi_increment = 0;
        response.power_adjust = 0;
    }
    
    response.has_push_config = true;
}

} // namespace CommandReceiverSubs
