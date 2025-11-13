#ifndef UTILITY_H
#define UTILITY_H

#include "pi_buffer.h"
#include "ConfigManager.h" // For ConfigManager access
#include <cstdint>

class Utility
{
public:
    Utility(char* buffer, int* icnt, int* ocnt, pi_buffer* cmd_buffer);
    void rx_char(char ch);
    void init_rf_channel();
    int make_pointer(int i1, int i2);
    bool is_valid_command_header();
    void move_buffer(int loc);
    

private:
    char* ibuf;
    int* icnt;
    int* ocnt;
    pi_buffer* command_buffer;
};

#endif // UTILITY_H