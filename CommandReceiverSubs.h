#ifndef COMMANDRECEIVERSUBS_H
#define COMMANDRECEIVERSUBS_H

#include <cstdint>

// Forward declaration
struct CommandResponse;

namespace CommandReceiverSubs {
    
    /**
     * Parse version string into unit type and firmware version
     * @param response CommandResponse to populate with parsed data
     */
    void parse_version_string(CommandResponse& response);
    
    /**
     * Parse command parameters for BASE→UNIT commands
     * @param response CommandResponse to populate with parsed parameters
     */
    void parse_command_params(CommandResponse& response);
    
    /**
     * Parse upload data from command '3' packet
     * @param response CommandResponse to populate with upload data
     */
    void parse_upload_data(CommandResponse& response);
    
    /**
     * Verify checksum for upload data packets
     * @param data Raw packet data
     * @param is_fast True for FAST mode, false for SLOW mode
     * @return True if checksum is valid
     */
    bool verify_upload_checksum(const unsigned char* data, bool is_fast);
    
    /**
     * Decode descriptor field from header info
     * @param response CommandResponse to populate with decoded descriptor data
     */
    void decode_descriptor(CommandResponse& response);
    
    /**
     * Parse upload partial request from command 'U'
     * @param response CommandResponse to populate with partial request data
     */
    void parse_upload_partial_request(CommandResponse& response);
    
    /**
     * Calculate CRC32 checksum using standard algorithm
     * @param data Data to calculate CRC over
     * @param length Length of data in bytes
     * @return Calculated CRC32 value
     */
    uint32_t calculate_crc32(const unsigned char* data, int length);
    
    /**
     * Parse push config command 'D' from BASE→UNIT
     * @param response CommandResponse to populate with config data
     */
    void parse_push_config(CommandResponse& response);
}

#endif // COMMANDRECEIVERSUBS_H
