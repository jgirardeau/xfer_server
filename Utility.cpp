
#include "Utility.h"
#include "logger.h"
#include <fstream>
#include <iostream>
#include "buffer_constants.h"
#include <string>
#include <cstdio>
#include <cctype>
Utility::Utility(char* buffer, int* icnt, int* ocnt, pi_buffer* cmd_buffer)
    : ibuf(buffer), icnt(icnt), ocnt(ocnt), command_buffer(cmd_buffer) {}

void Utility::init_rf_channel() {
    // Resolve RF channel file path from config (fallback to default)
    const std::string rf_channel_file =
        ConfigManager::instance().get("system.rf_channel_file",
                                      std::string("/home/pi/channel.txt"));

    std::ifstream file(rf_channel_file);
    if (!file.is_open()) {
        LOG_INFO_CTX("utility", "Failed to open RF channel file: %s", rf_channel_file.c_str());
        return;
    }

    int channel = -1;
    if (!(file >> channel)) {
        LOG_INFO_CTX("utility", "Failed to read channel from %s", rf_channel_file.c_str());
        return;
    }

    // Validate channel (0–5 matches your existing logic)
    if (channel >= 0 && channel <= 5) {
        command_buffer->add_char(static_cast<char>(0x80 | (channel & 0x7)));
        LOG_INFO_CTX("utility", "Set RF channel to %d from %s", channel, rf_channel_file.c_str());
    } else {
        LOG_INFO_CTX("utility", "Invalid channel %d in %s", channel, rf_channel_file.c_str());
    }
}

void Utility::rx_char(char ch) {
    ibuf[(*icnt) & IBUF_MASK] = ch;
    (*icnt)++;
    *icnt=(*icnt) & IBUF_MASK;
}

int Utility::make_pointer(int i1, int i2) {
    return (i1 + i2) & IBUF_MASK;
}

void Utility::move_buffer(int loc) {
    (*ocnt) += loc;
    *ocnt=(*ocnt) & IBUF_MASK;
}

bool Utility::is_valid_command_header()
{
    int start = *ocnt;
    // Check for "tS" at start
    if (ibuf[start&IBUF_MASK] != 't') return false;
    if (ibuf[make_pointer(start, 1)] != 'S') return false;

    // Check for "uP" at end (positions CLENG-2 and CLENG-1)
    if (ibuf[make_pointer(start, CLENG - 2)] != 'u') return false;
    if (ibuf[make_pointer(start, CLENG - 1)] != 'P') return false;
    return true;
}

