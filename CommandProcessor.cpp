#include "CommandProcessor.h"
#include "CommandTransmitter.h"
#include "CommandReceiver.h"
#include "logger.h"
#include <cctype>
#include <sstream>
#include <iomanip>
#include "command_definitions.h"

// Initialize command registry
std::map<char, CommandInfo> CommandProcessor::init_command_registry() {
    std::map<char, CommandInfo> registry;
    
    // BASE â†’ UNIT Commands (MAC = 0xFFFFFFFF)
    registry[CMD_WAKEUP] = {CMD_WAKEUP, "WAKE", "Wake/Activate command", PacketDirection::BASE_TO_UNIT};
    registry[CMD_WAKEUP_LC] = {CMD_WAKEUP_LC, "WAKE", "Wake/Activate command (lowercase)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_SAMPLE_DATA] = {CMD_SAMPLE_DATA, "SAMPLE_DATA", "Sample data command", PacketDirection::BASE_TO_UNIT};
    registry[CMD_SAMPLE_DATA_LC] = {CMD_SAMPLE_DATA_LC, "SAMPLE_DATA", "Sample data command (lowercase)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_SLEEP] = {CMD_SLEEP, "SLEEP", "Sleep command", PacketDirection::BASE_TO_UNIT};
    registry[CMD_SLEEP_LC] = {CMD_SLEEP_LC, "SLEEP", "Sleep command (lowercase)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_RESET] = {CMD_RESET, "RESET", "Reset command", PacketDirection::BASE_TO_UNIT};
    registry[CMD_RESET_LC] = {CMD_RESET_LC, "RESET", "Reset command (lowercase)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_ERASE_CFG] = {CMD_ERASE_CFG, "ERASE_CFG", "Erase old config files", PacketDirection::BASE_TO_UNIT};
    registry[CMD_ERASE_CFG_LC] = {CMD_ERASE_CFG_LC, "ERASE_CFG", "Erase old config files (lowercase)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_INITIALIZE] = {CMD_INITIALIZE, "INIT", "Initialize/Probe command", PacketDirection::BASE_TO_UNIT};
    registry[CMD_INITIALIZE_LC] = {CMD_INITIALIZE_LC, "INIT", "Initialize/Probe command (lowercase)", PacketDirection::BASE_TO_UNIT};
    
    // UNIT â†’ BASE Responses (specific MAC address)
    registry[CMD_ACK_INIT] = {CMD_ACK_INIT, "ACK_INIT", "ACK response to Initialize command with unit info", PacketDirection::UNIT_TO_BASE};
    registry[CMD_DATA_UPLOAD] = {CMD_DATA_UPLOAD, "DATA_UPLOAD", "Data upload segment", PacketDirection::UNIT_TO_BASE};
    registry[CMD_DATA_RESPONSE] = {CMD_DATA_RESPONSE, "DATA_RSP", "Data response with sensor readings", PacketDirection::UNIT_TO_BASE};
    registry[CMD_DATA_RESPONSE_LC] = {CMD_DATA_RESPONSE_LC, "DATA_RSP", "Data response with sensor readings (lowercase)", PacketDirection::UNIT_TO_BASE};
    registry[CMD_ACK] = {CMD_ACK, "ACK", "Acknowledgment response", PacketDirection::UNIT_TO_BASE};
    registry[CMD_ACK_LC] = {CMD_ACK_LC, "ACK", "Acknowledgment response (lowercase)", PacketDirection::UNIT_TO_BASE};
    //  other BASE â†’ UNIT commands:
    registry[CMD_UPLOAD_INIT] = {CMD_UPLOAD_INIT, "UPLOAD_INIT", "Upload initialization request (0x51)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_UPLOAD_INIT_LC] = {CMD_UPLOAD_INIT_LC, "UPLOAD_INIT", "Upload initialization request (0x51, lowercase)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_UPLOAD_PARTIAL] = {CMD_UPLOAD_PARTIAL, "UPLOAD_PARTIAL", "Upload partial data request (0x55)", PacketDirection::BASE_TO_UNIT};
    registry[CMD_UPLOAD_PARTIAL_LC] = {CMD_UPLOAD_PARTIAL_LC, "UPLOAD_PARTIAL", "Upload partial data request (0x55)", PacketDirection::BASE_TO_UNIT};
    
    return registry;
}

const std::map<char, CommandInfo> CommandProcessor::command_registry = init_command_registry();

CommandProcessor::CommandProcessor(CTS1X* core, char* buffer, int* input_cnt, int* output_cnt)
    : ts1x_core(core), ibuf(buffer), icnt(input_cnt), ocnt(output_cnt)
{
    transmitter = new CommandTransmitter(core);
    receiver = new CommandReceiver(buffer, input_cnt, output_cnt);
}

CommandProcessor::~CommandProcessor()
{
    delete transmitter;
    delete receiver;
}

// Command registry access
const CommandInfo* CommandProcessor::get_command_info(char command_code) {
    auto it = command_registry.find(command_code);
    if (it != command_registry.end()) {
        return &(it->second);
    }
    return nullptr;
}

std::string CommandProcessor::get_direction_string(PacketDirection dir) {
    switch (dir) {
        case PacketDirection::BASE_TO_UNIT: return "BASEâ†’UNIT";
        case PacketDirection::UNIT_TO_BASE: return "UNITâ†’BASE";
        default: return "UNKNOWN";
    }
}

bool CommandProcessor::is_header_info_present(const unsigned char* data) {
    // Header info is in bytes 13-44 (32 bytes)
    // It's NOT present if:
    // 1. Bytes 13-16 are all 0xFF (explicit marker for no header)
    // 2. Bytes 13-44 are all padding (0x30 = ASCII '0')
    
    // Check for 0xFF marker
    if (data[13] == 0xFF && data[14] == 0xFF && 
        data[15] == 0xFF && data[16] == 0xFF) {
        return false;
    }
    
    // Check if it's all padding (0x30)
    bool all_padding = true;
    for (int i = 13; i < 45; i++) {
        if (data[i] != 0x30) {
            all_padding = false;
            break;
        }
    }
    if (all_padding) {
        return false;
    }
    
    // Check for realistic values in key fields
    uint8_t marker = data[15];
    if (marker == 0x30 || marker == 0xFF) {
        return false;
    }
    
    return true;
}

PacketDirection CommandProcessor::determine_direction(const unsigned char* data, char command_code) {
    // Primary check: MAC address
    // BASEâ†’UNIT always uses broadcast MAC (0xFFFFFFFF) in bytes 3-6
    uint32_t source_mac = ((uint32_t)data[3] << 24) |
                          ((uint32_t)data[4] << 16) |
                          ((uint32_t)data[5] << 8) |
                          (uint32_t)data[6];
    
    if (source_mac == BROADCAST_MAC) {
        return PacketDirection::BASE_TO_UNIT;
    }
    
    // Secondary check: command registry
    const CommandInfo* info = get_command_info(command_code);
    if (info != nullptr) {
        return info->typical_direction;
    }
    
    // Tertiary check: header info presence
    if (is_header_info_present(data)) {
        return PacketDirection::UNIT_TO_BASE;
    }
    
    return PacketDirection::UNKNOWN;
}

// Utility functions
uint16_t CommandProcessor::calculate_checksum(const unsigned char* data, int length)
{
    uint16_t sum = 0;
    for (int i = 0; i < length; i++) {
        sum += data[i];
    }
    return sum;
}

std::string CommandProcessor::sanitize_string(const char* str, size_t max_len) {
    std::string result;
    for (size_t i = 0; i < max_len && str[i] != '\0'; i++) {
        if (std::isprint(static_cast<unsigned char>(str[i]))) {
            result += str[i];
        } else {
            result += '.';
        }
    }
    return result;
}

std::string CommandProcessor::hex_dump_buffer(const unsigned char* cmd_buffer, int len) {
    std::string hex;
    hex.reserve(len * 3);
    std::string ascii;
    ascii.reserve(len);

    if (cmd_buffer == nullptr || len <= 0) {
        return "";
    }

    for (int i = 0; i < len; ++i) {
        unsigned char b = static_cast<unsigned char>(cmd_buffer[i]);

        char tmp[4];
        std::snprintf(tmp, sizeof(tmp), "%02X", b);
        if (!hex.empty()) hex.push_back(' ');
        hex += tmp;

        ascii.push_back(std::isprint(b) ? static_cast<char>(b) : '.');
    }
    return hex + "  |" + ascii + "|";
}

// Transmit operations - delegate to CommandTransmitter
void CommandProcessor::send_command(const unsigned char* cmd_buffer, int length)
{
    transmitter->send_command(cmd_buffer, length);
}

bool CommandProcessor::make_command(unsigned char* output, int command, uint32_t macid, const unsigned char* body_data, const Sampleset* sampleset)
{
    return transmitter->make_command(output, command, macid, body_data, sampleset);
}

void CommandProcessor::print_tx_command(const unsigned char* data, int length)
{
    transmitter->print_tx_command(data, length);
}

// Receive operations - delegate to CommandReceiver
void CommandProcessor::print_command()
{
    receiver->print_command();
}

CommandResponse CommandProcessor::parse_response()
{
    return receiver->parse_response();
}

void CommandProcessor::print_response(const CommandResponse& response)
{
    receiver->print_response(response);
}

bool CommandProcessor::make_erase_command(unsigned char* output, uint8_t age)
{
    return transmitter->make_erase_command(output, age);
}

void CommandProcessor::set_print_upload_data(bool enable)
{
    receiver->set_print_upload_data(enable);
}