#ifndef COMMAND_TRANSMITTER_H
#define COMMAND_TRANSMITTER_H

#include "CommandProcessor.h"
#include "TS1X.h"

// Forward declaration
struct Sampleset;

// Handles building and transmitting command packets
class CommandTransmitter
{
public:
    CommandTransmitter(CTS1X* core);
    
    // Build a command packet
    // If sampleset is provided, encodes sampleset parameters into R command body
    bool make_command(unsigned char* output, int command, uint32_t macid, 
                     const unsigned char* body_data = nullptr,
                     const Sampleset* sampleset = nullptr);
    
    // Send a command packet
    void send_command(const unsigned char* cmd_buffer, int length);
    
    // Print a command packet being transmitted
    void print_tx_command(const unsigned char* cmd_buffer, int length);

    bool make_erase_command(unsigned char* output, uint8_t age);

private:
    CTS1X* ts1x_core;
    
    // Helper function to write ASCII hex characters
    static void write_hex_ascii(unsigned char* buffer, int offset, uint32_t value, int num_chars);
};

#endif // COMMAND_TRANSMITTER_H
