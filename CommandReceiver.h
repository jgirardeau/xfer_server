#ifndef COMMAND_RECEIVER_H
#define COMMAND_RECEIVER_H

#include "CommandProcessor.h"

// Handles parsing and displaying received command packets
class CommandReceiver
{
public:
    CommandReceiver(char* buffer, int* input_cnt, int* output_cnt);
    
    // Control printing of upload data samples
    void set_print_upload_data(bool enable) { print_upload_data_samples = enable; }

    // Print a received command from the circular buffer
    void print_command();
    
    // Parse a complete response packet from the circular buffer
    CommandResponse parse_response();
    
    // Print a parsed response packet
    void print_response(const CommandResponse& response);
    
    // Verify checksum of packet in circular buffer
    bool verify_checksum();

private:
    char* ibuf;
    int* icnt;
    int* ocnt;
    bool print_upload_data_samples;  // Flag to control data printing
};

#endif // COMMAND_RECEIVER_H