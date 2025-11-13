#include "HeaderWriter.h"
#include "logger.h"
#include <sstream>
#include <iomanip>
#include <sys/time.h>

void write_header_log_entry(const CommandResponse* triggering_response, size_t data_size) {
    // Get the header logger
    SimpleLogger* header_logger = get_header_logger();
    if (!header_logger) {
        LOG_ERROR_CTX("header_writer", "Header logger not initialized!");
        return;
    }
    
    // Check if we have valid response
    if (!triggering_response || !triggering_response->has_header_info) {
        LOG_ERROR_CTX("header_writer", "Invalid triggering response or missing header info");
        return;
    }
    
    // Build the header string similar to the example format
    std::ostringstream oss;
    
    // Add timestamp (current time when log entry is written)
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm* tm_info = localtime(&tv.tv_sec);
    
    char timestamp[64];
    snprintf(timestamp, sizeof(timestamp), "%04d-%02d-%02d %02d:%02d:%02d,%03ld",
             tm_info->tm_year + 1900,
             tm_info->tm_mon + 1,
             tm_info->tm_mday,
             tm_info->tm_hour,
             tm_info->tm_min,
             tm_info->tm_sec,
             tv.tv_usec / 1000);
    
    oss << timestamp << " - ";
    
    // Start with Push_header prefix
    oss << "Push_header echo";
    
    // MAC addresses (ECHO=source MAC, MIST=unit_id)
    oss << " ECHO=" << std::hex << std::setfill('0') << std::setw(8) << triggering_response->source_macid;
    oss << " MIST=" << std::hex << std::setfill('0') << std::setw(8) << triggering_response->unit_id;
    
    // Descriptor info: Desc (rssi descriptor_hex L=length)
    oss << std::dec << " Desc (";
    
    // Add RSSI if available (from header_info, not rssi_value which is different)
    if (triggering_response->header_info.rssi != 0 && triggering_response->header_info.rssi != 255) {
        oss << (int)triggering_response->header_info.rssi << " ";
    }
    
    oss << std::hex << std::setw(4) << std::setfill('0') << triggering_response->header_info.descriptor;
    oss << std::dec << " L=" << triggering_response->descriptor_sample_length << ")";
    
    // Dataset timestamp
    const auto& dt = triggering_response->header_info.dataset_pi_time;
    oss << "  " << std::setfill('0')
        << std::setw(4) << dt.year << "/"
        << std::setw(2) << (int)dt.month << "/"
        << std::setw(2) << (int)dt.day << "-"
        << std::setw(2) << (int)dt.hour << ":"
        << std::setw(2) << (int)dt.min << ":"
        << std::setw(2) << (int)dt.sec;
    
    // Data control bits
    oss << " DCB=" << std::hex << std::setw(2) << std::setfill('0') 
        << (int)triggering_response->header_info.data_control_bits;
    
    // CRC
    oss << " CRC=" << std::hex << std::setw(8) << std::setfill('0') 
        << triggering_response->on_deck_crc;
    
    // Actual data size received
    oss << std::dec << " DataSize=" << data_size;
    
    // Channel mask info
    oss << " ChMask=" << std::hex << std::setw(2) << std::setfill('0')
        << (int)triggering_response->descriptor_channel_mask;
    
    // Decode channels
    oss << std::dec << " Ch=[";
    bool first = true;
    if (triggering_response->descriptor_channel_mask & 0x01) {
        if (!first) oss << ",";
        oss << "US";
        first = false;
    }
    if (triggering_response->descriptor_channel_mask & 0x02) {
        if (!first) oss << ",";
        oss << "X";
        first = false;
    }
    if (triggering_response->descriptor_channel_mask & 0x04) {
        if (!first) oss << ",";
        oss << "Y";
        first = false;
    }
    if (triggering_response->descriptor_channel_mask & 0x08) {
        if (!first) oss << ",";
        oss << "Z";
        first = false;
    }
    if (first) oss << "None";
    oss << "]";
    
    // Sample rate
    oss << " SR=" << triggering_response->descriptor_sample_rate_str;
    
    // Mode - changed from RAW to DATA
    oss << " Mode=" << (triggering_response->descriptor_rms_only ? "RMS" : "DATA");
    
    // Additional diagnostic fields
    // RSSI and Ambient RSSI (from response, not header_info)
    if (triggering_response->rssi_value != 0 && triggering_response->rssi_value != 255) {
        oss << " Rssi=" << std::dec << (int)triggering_response->rssi_value;
    }
    if (triggering_response->ambient_rssi != 0 && triggering_response->ambient_rssi != 255) {
        oss << " ARssi=" << std::dec << (int)triggering_response->ambient_rssi;
    }
    
    // Reset count
    if (triggering_response->ram_corruption_reset_count != 0) {
        oss << " RCnt=" << std::dec << (int)triggering_response->ram_corruption_reset_count;
    }
    
    // Firmware version (hex)
    if (triggering_response->firmware != 0) {
        oss << " FW=" << std::hex << std::setw(2) << std::setfill('0') 
            << (int)triggering_response->firmware;
    }
    
    // Datasets processed and correction
    if (triggering_response->datasets_processed != 0) {
        oss << std::dec << " DSETS=" << triggering_response->datasets_processed;
    }
    if (triggering_response->packet_correction != 0) {
        oss << std::dec << " PCORR=" << triggering_response->packet_correction;
    }
    
    // Pi time (simplified format: YYYY-MM-DD HH:MM)
    if (triggering_response->pi_time_year != 0) {
        oss << " PI=" << std::setfill('0')
            << std::setw(4) << triggering_response->pi_time_year << "-"
            << std::setw(2) << (int)triggering_response->pi_time_month << "-"
            << std::setw(2) << (int)triggering_response->pi_time_day << " "
            << std::setw(2) << (int)triggering_response->pi_time_hour << ":"
            << std::setw(2) << (int)triggering_response->pi_time_min;
    }
    
    // Restart counters
    if (triggering_response->pi_spi_restart_count != 0) {
        oss << std::dec << " RSPI=" << (int)triggering_response->pi_spi_restart_count;
    }
    if (triggering_response->global_power_control != 0) {
        oss << std::dec << " RPA=" << (int)triggering_response->global_power_control;
    }
    if (triggering_response->reboot_count != 0) {
        oss << std::dec << " RBT=" << (int)triggering_response->reboot_count;
    }
    if (triggering_response->undervoltage_count != 0) {
        oss << std::dec << " RUVOLT=" << (int)triggering_response->undervoltage_count;
    }
    
    // Hardware diagnostics
    if (triggering_response->header_debug != 0) {
        oss << " DBG=" << std::hex << std::setw(4) << std::setfill('0') 
            << triggering_response->header_debug;
    }
    if (triggering_response->header_bleon != 0) {
        oss << std::dec << " BLE=" << triggering_response->header_bleon;
    }
    if (triggering_response->header_fpgaon != 0) {
        oss << std::dec << " FPGA=" << triggering_response->header_fpgaon;
    }
    
    // Counts
    if (triggering_response->header_mincount != 0) {
        oss << std::dec << " MICNT=" << triggering_response->header_mincount;
    }
    if (triggering_response->header_failcount != 0) {
        oss << std::dec << " FAIL=" << triggering_response->header_failcount;
    }
    
    // Write to header log
    std::string header_line = oss.str();
    header_logger->write_raw(header_line.c_str());
    
    // Also log to main logger for debugging
    LOG_INFO_CTX("header_writer", "Header entry written: %s", header_line.c_str());
}