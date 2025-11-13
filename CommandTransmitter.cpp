#include "CommandTransmitter.h"
#include "logger.h"
#include "buffer_constants.h"
#include "command_definitions.h"
#include <cstring>
#include <ctime>
#include <cctype>
#include <cmath>
#include "SamplesetGenerator.h"

CommandTransmitter::CommandTransmitter(CTS1X* core)
    : ts1x_core(core)
{
}

void CommandTransmitter::write_hex_ascii(unsigned char* buffer, int offset, uint32_t value, int num_chars)
{
    char hex_str[16];
    snprintf(hex_str, sizeof(hex_str), "%0*x", num_chars, value);
    
    for (int i = 0; i < num_chars; i++) {
        buffer[offset + i] = hex_str[i];
    }
}

bool CommandTransmitter::make_command(unsigned char* output, int command, uint32_t macid, 
                                     const unsigned char* body_data, const Sampleset* sampleset)
{
    if (output == nullptr) {
        LOG_INFO_CTX("cmd_transmitter", "Error: output buffer is null");
        return false;
    }

    // Initialize entire buffer with 0x30 ('0' padding)
    memset(output, 0x30, PACKET_LENGTH);

    int idx = 0;
    
    // Header: "tS" (0x74 0x53)
    output[idx++] = HEADER_BYTE1;  // 0: 0x74
    output[idx++] = HEADER_BYTE2;  // 1: 0x53
    
    // Fixed header fields
    output[idx++] = 0x01;          // 2: 0x01
    
    // Bytes 3-6: MAC address (always BROADCAST_MAC for BASE→UNIT)
    output[idx++] = 0xff;          // 3: 0xff
    output[idx++] = 0xff;          // 4: 0xff
    output[idx++] = 0xff;          // 5: 0xff
    output[idx++] = 0xff;          // 6: 0xff
    output[idx++] = 0x01;          // 7: 0x01
    
    // Padding (bytes 8-12)
    for (int i = 0; i < 5; i++) {
        output[idx++] = 0x30;      // 8-12: 0x30
    }
    
    // MAC ID - first occurrence (bytes 13-16) - BIG ENDIAN
    output[idx++] = (macid >> 24) & 0xff;  // 13: MSB
    output[idx++] = (macid >> 16) & 0xff;  // 14
    output[idx++] = (macid >> 8) & 0xff;   // 15
    output[idx++] = macid & 0xff;          // 16: LSB
    
    // MAC ID - second occurrence (bytes 17-20) - BIG ENDIAN
    output[idx++] = (macid >> 24) & 0xff;  // 17: MSB
    output[idx++] = (macid >> 16) & 0xff;  // 18
    output[idx++] = (macid >> 8) & 0xff;   // 19
    output[idx++] = macid & 0xff;          // 20: LSB
    
    // Padding (bytes 21-44) - No header info for BASE→UNIT commands
    for (int i = 0; i < 24; i++) {
        output[idx++] = 0x30;      // 21-44: 0x30
    }
    
    // Command character (byte 45)
    output[idx++] = command & 0xff;        // 45: command
    
    // Body data or padding (bytes 46-123)
    // Priority: sampleset encoding > explicit body_data > default padding
    if (sampleset != nullptr && (command == CMD_SAMPLE_DATA || command == CMD_SAMPLE_DATA_LC)) {
        // Encode sampleset parameters into 'R' command body
        unsigned char encoded_body[78];
        memset(encoded_body, '0', sizeof(encoded_body));
        
        // Calculate parameters from sampleset
        uint32_t capture_segments;
        uint32_t channel_mask;
        uint32_t decimation;
        uint32_t dc_control;
        
        // Channel mask is directly from sampling_mask
        channel_mask = sampleset->sampling_mask;
        
        if (sampleset->ac_dc_flag == 0) {
            // DC mode: Special encoding
            decimation = 1;  // DC uses decimation=1
            dc_control = 0x00000001;  // Enable DC mode
            capture_segments = 1;  // DC captures 1 segment (16 samples)
        } else {
            // AC mode: Calculate decimation from max_freq
            // sample_rate = 2 * max_freq (Nyquist)
            // decimation code: sample_rate = 20000 / (2^(decimation-1))
            // Therefore: decimation = log2(20000 / sample_rate) + 1
            
            double sample_rate = 2.0 * sampleset->max_freq;
            
            // Calculate decimation (clamp to 1-15 range)
            double decimation_float = log2(20000.0 / sample_rate) + 1.0;
            decimation = (uint32_t)(decimation_float + 0.5);  // Round to nearest
            if (decimation < 1) decimation = 1;
            if (decimation > 15) decimation = 15;
            
            // Calculate capture_segments based on resolution
            // resolution is the total number of samples desired
            // Each segment contains 16 samples
            if (sampleset->resolution > 0) {
                capture_segments = (sampleset->resolution + 15) / 16;  // Round up
            } else {
                capture_segments = 100;  // Default: 1600 samples
            }
            
            dc_control = 0;  // AC mode
        }
        
        // Build the 10 parameters as 8-character ASCII hex strings
        // params[0]: capture_segments
        snprintf((char*)&encoded_body[0], 9, "%08x", capture_segments);
        
        // params[1]: Combined field
        // Bits 0-7: channel_mask
        // Bits 8-11: decimation (4 bits)
        // Bit 12: advanced_checksum (always 1)
        // Bits 16-31: tach_delay (0 for normal operation)
        uint32_t combined = channel_mask | (decimation << 8) | (1 << 12);  // Set advanced checksum
        snprintf((char*)&encoded_body[8], 9, "%08x", combined);
        
        // params[2]: dc_control
        snprintf((char*)&encoded_body[16], 9, "%08x", dc_control);
        
        // params[3]: wakeup_delay (typically 0)
        snprintf((char*)&encoded_body[24], 9, "%08x", 0);
        
        // params[4]: bluewave_interval (typically 0)
        snprintf((char*)&encoded_body[32], 9, "%08x", 0);
        
        // params[5-9]: unused (already padded with '0')
        
        LOG_INFO_CTX("cmd_transmitter", "Encoded sampleset: mask=0x%02x, dec=%u, segs=%u, dc_ctl=0x%08x",
                     channel_mask, decimation, capture_segments, dc_control);
        
        // Copy encoded body to output
        for (int i = 0; i < 78; i++) {
            output[idx++] = encoded_body[i];  // 46-123: encoded sampleset data
        }
    }
    else if (body_data != nullptr) {
        for (int i = 0; i < 78; i++) {
            output[idx++] = body_data[i];  // 46-123: body data
        }
    } else {
        // Default padding for bytes 46-85
        for (int i = 0; i < 40; i++) {
            output[idx++] = 0x30;          // 46-85: 0x30
        }
        
        // Special handling for 'R'/'r' command - add time encoding
        if (command == CMD_SAMPLE_DATA || command == CMD_SAMPLE_DATA_LC) {
            // Set specific bytes for 'r' command
            output[52] = 0x34;  // '4'
            output[58] = 0x31;  // '1'
            output[59] = 0x31;  // '1'
            output[61] = 0x31;  // '1'
            output[84] = 0x31;  // '1'
            output[85] = 0x65;  // 'e'
            
            // Encode current time (bytes 86-99)
            time_t now = time(nullptr);
            struct tm* timeinfo = localtime(&now);
            
            if (timeinfo != nullptr) {
                // Month (86-87): 2 hex characters
                write_hex_ascii(output, 86, timeinfo->tm_mon + 1, 2);
                
                // Day (88-89): 2 hex characters
                write_hex_ascii(output, 88, timeinfo->tm_mday, 2);
                
                // Year (90-93): 4 hex characters
                write_hex_ascii(output, 90, timeinfo->tm_year + 1900, 4);
                
                // Hour (94-95): 2 hex characters
                write_hex_ascii(output, 94, timeinfo->tm_hour, 2);
                
                // Minute (96-97): 2 hex characters
                write_hex_ascii(output, 96, timeinfo->tm_min, 2);
                
                // Second (98-99): 2 hex characters
                write_hex_ascii(output, 98, timeinfo->tm_sec, 2);
            }
            
            // Padding (bytes 100-123)
            idx = 100;
            for (int i = 0; i < 24; i++) {
                output[idx++] = 0x30;      // 100-123: 0x30
            }
        } else {
            // For non-'r' commands, just pad the rest (bytes 86-123)
            for (int i = 0; i < 38; i++) {
                output[idx++] = 0x30;      // 86-123: 0x30
            }
        }
    }
    
    // For 'A' and 'R' commands, no checksum - just padding at 124-125
    output[124] = 0x30;  // 124: padding
    output[125] = 0x30;  // 125: padding
    
    // Tail: "uP" (0x75 0x50)
    output[126] = TAIL_BYTE1;              // 126: 0x75
    output[127] = TAIL_BYTE2;              // 127: 0x50

    return true;
}

void CommandTransmitter::send_command(const unsigned char* cmd_buffer, int len)
{
    //LOG_INFO_CTX("cmd_transmitter", "Sending command (%d bytes)", len);
    for (int i = 0; i < len; ++i) {
        ts1x_core->scia_xmit(cmd_buffer[i]);
    }
}

void CommandTransmitter::print_tx_command(const unsigned char* cmd_buffer, int length)
{
    size_t idx = COMMAND_START;
    if (cmd_buffer == nullptr || length == 0 || idx >= length) {
        LOG_ERROR_CTX("cmd_transmitter", "Invalid parameters: buffer=%p, length=%d", 
                      cmd_buffer, length);
        return;
    }
    
    char cmd = cmd_buffer[idx];
    cmd = tolower(cmd);
    
    // Determine direction for logging
    PacketDirection dir = CommandProcessor::determine_direction(cmd_buffer, cmd);
    
    std::string data = CommandProcessor::hex_dump_buffer(cmd_buffer, length);
    
    const CommandInfo* info = CommandProcessor::get_command_info(cmd);
    if (info != nullptr) {
        LOG_INFO_CTX("cmd_transmitter", "TX Command: %c [%s] %s, Data: %s", 
                     cmd, info->name.c_str(),
                     CommandProcessor::get_direction_string(dir).c_str(),
                     data.c_str());
    } else {
        LOG_INFO_CTX("cmd_transmitter", "TX Command: %c [UNKNOWN] %s, Data: %s", 
                     cmd, CommandProcessor::get_direction_string(dir).c_str(), data.c_str());
    }
}

bool CommandTransmitter::make_erase_command(unsigned char* output, uint8_t age)
{
    if (output == nullptr) {
        LOG_INFO_CTX("cmd_transmitter", "Error: output buffer is null");
        return false;
    }

    // Initialize entire buffer with zeros (not 0x30 like other commands)
    memset(output, 0x00, PACKET_LENGTH);

    int idx = 0;
    
    // Header: "tS" (0x74 0x53)
    output[idx++] = HEADER_BYTE1;  // 0: 0x74
    output[idx++] = HEADER_BYTE2;  // 1: 0x53
    
    // Fixed header fields
    output[idx++] = 0x01;          // 2: 0x01
    
    // Bytes 3-6: MAC address (always BROADCAST_MAC for BASEâ†’UNIT)
    output[idx++] = 0xff;          // 3: 0xff
    output[idx++] = 0xff;          // 4: 0xff
    output[idx++] = 0xff;          // 5: 0xff
    output[idx++] = 0xff;          // 6: 0xff
    output[idx++] = 0x01;          // 7: 0x01
    
    // Padding (bytes 8-12) - ASCII '0'
    for (int i = 0; i < 5; i++) {
        output[idx++] = 0x30;      // 8-12: 0x30
    }

    // MAC ID fields (bytes 13-20) - ASCII '0'
    for (int i = 0; i < 8; i++) {
        output[idx++] = 0x30;
    }
    
    // Padding (bytes 21-44) - ASCII '0'
    for (int i = 0; i < 24; i++) {
        output[idx++] = 0x30;      // 21-44: 0x30
    }
    
    // Command character (byte 45)
    output[idx++] = CMD_ERASE_CFG;                   // 45: 'E' command
    
    // Age byte (byte 46) - encoded as (age & 0x3f) | 0xc0
    output[idx++] = (age & 0x3f) | 0xc0;   // 46: age with top 2 bits set
    
    // Rest is zeros (bytes 47-125) - already zeroed by memset
    
    // Tail: "uP" (0x75 0x50)
    output[126] = TAIL_BYTE1;              // 126: 0x75
    output[127] = TAIL_BYTE2;              // 127: 0x50

    return true;
}