#include "WriteOutputFiles.h"
#include "HeaderWriter.h"
#include "DataFileWriter.h"
#include "logger.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include "SensorConversions.h"

// Helper function to create directory recursively
static bool create_directory_recursive(const std::string& path) {
    // Check if directory already exists
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;  // Directory exists
        }
        LOG_ERROR_CTX("file_writer", "Path exists but is not a directory: %s", path.c_str());
        return false;
    }
    
    // Find parent directory
    size_t slash_pos = path.find_last_of('/');
    if (slash_pos != std::string::npos && slash_pos > 0) {
        std::string parent = path.substr(0, slash_pos);
        // Recursively create parent
        if (!create_directory_recursive(parent)) {
            return false;
        }
    }
    
    // Create this directory
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno != EEXIST) {  // Ignore if it was just created by another thread
            LOG_ERROR_CTX("file_writer", "Failed to create directory %s: %s", 
                         path.c_str(), strerror(errno));
            return false;
        }
    }
    
    LOG_INFO_CTX("file_writer", "Created directory: %s", path.c_str());
    return true;
}

OutputFileInfo write_output_files(
    const std::string& root_filehandler,
    const std::string& config_files_directory,
    const std::string& ts1_data_files,
    const std::vector<int16_t>& data,
    const CommandResponse* triggering_response
)
{
    OutputFileInfo result;
    
    LOG_INFO_CTX("file_writer", "=== Write Output Files ===");
    
    // Log config variables
    LOG_INFO_CTX("file_writer", "Config: root_filehandler = %s", root_filehandler.c_str());
    LOG_INFO_CTX("file_writer", "Config: config_files_directory = %s", config_files_directory.c_str());
    LOG_INFO_CTX("file_writer", "Config: ts1_data_files = %s", ts1_data_files.c_str());
    
    // Check if we have a valid triggering response
    if (!triggering_response) {
        LOG_ERROR_CTX("file_writer", "No triggering response available!");
        return result;
    }
    
    // Check if response has header info (descriptor is in header_info)
    if (!triggering_response->has_header_info) {
        LOG_ERROR_CTX("file_writer", "Triggering response has no header info!");
        return result;
    }
    
    // Log descriptor information
    uint16_t descriptor = triggering_response->header_info.descriptor;
    uint32_t sample_length = triggering_response->descriptor_sample_length;
    uint8_t channel_mask = triggering_response->descriptor_channel_mask;
    uint8_t sample_rate_code = triggering_response->descriptor_sample_rate;
    bool rms_only = triggering_response->descriptor_rms_only;
    
    LOG_INFO_CTX("file_writer", "Descriptor: 0x%04X", descriptor);
    LOG_INFO_CTX("file_writer", "  Sample Length: %d samples (expected)", sample_length);
    LOG_INFO_CTX("file_writer", "  Actual Data Received: %zu samples", data.size());
    LOG_INFO_CTX("file_writer", "  Channel Mask: 0x%02X", channel_mask);
    
    // Decode channel mask into readable format
    std::string channels = "";
    if (channel_mask & 0x01) channels += "Ultrasonic ";
    if (channel_mask & 0x02) channels += "X ";
    if (channel_mask & 0x04) channels += "Y ";
    if (channel_mask & 0x08) channels += "Z ";
    if (channels.empty()) channels = "None";
    
    LOG_INFO_CTX("file_writer", "  Active Channels: %s", channels.c_str());
    LOG_INFO_CTX("file_writer", "  Sample Rate: %s (code=%d)", 
                 triggering_response->descriptor_sample_rate_str.c_str(),
                 sample_rate_code);
    LOG_INFO_CTX("file_writer", "  Mode: %s", rms_only ? "RMS Only" : "Raw Data");
    
    // Log node information
    LOG_INFO_CTX("file_writer", "Node MAC: 0x%08X", triggering_response->source_macid);
    LOG_INFO_CTX("file_writer", "Data Control Bits: 0x%02X", 
                 triggering_response->header_info.data_control_bits);
    LOG_INFO_CTX("file_writer", "On-Deck CRC: 0x%08X", triggering_response->on_deck_crc);
    
    // Log dataset time information
    LOG_INFO_CTX("file_writer", "Dataset Time: %04d-%02d-%02d %02d:%02d:%02d",
                 triggering_response->header_info.dataset_pi_time.year,
                 triggering_response->header_info.dataset_pi_time.month,
                 triggering_response->header_info.dataset_pi_time.day,
                 triggering_response->header_info.dataset_pi_time.hour,
                 triggering_response->header_info.dataset_pi_time.min,
                 triggering_response->header_info.dataset_pi_time.sec);
    
    // Write header log entry
    write_header_log_entry(triggering_response, data.size());
    
    // Write DC file to ts1_data_files/dcvals directory
    result.dc_filename = write_dc_file(ts1_data_files, triggering_response);
    
    // Write data file to ts1_data_files/<unit_id>_ch<1/2>/ directory
    result.data_filename = write_data_file(ts1_data_files, data, triggering_response);
    
    // Set success flag if both files were written
    result.success = (!result.dc_filename.empty() && !result.data_filename.empty());
    
    if (result.success) {
        LOG_INFO_CTX("file_writer", "=== File Writing Complete ===");
    } else {
        LOG_ERROR_CTX("file_writer", "=== File Writing Failed ===");
    }
    
    return result;
    
    // TODO: Future enhancements:
    // 1. Optionally copy relevant config files from config_files_directory to output
}

std::string write_dc_file(
    const std::string& ts1_data_files,
    const CommandResponse* response
)
{
    if (!response || !response->has_header_info) {
        LOG_ERROR_CTX("file_writer", "Cannot write DC file: no valid response or header info");
        return "";
    }
    
    // Construct full directory path: ts1_data_files/dcvals
    std::string dc_directory = ts1_data_files + "/dcvals";
    
    // Create directory structure if it doesn't exist
    if (!create_directory_recursive(dc_directory)) {
        LOG_ERROR_CTX("file_writer", "Failed to create DC directory: %s", dc_directory.c_str());
        return "";
    }
    
    // Extract unit_id as hex string (8 characters, lowercase)
    char unit_id_hex[9];
    snprintf(unit_id_hex, sizeof(unit_id_hex), "%08x", response->unit_id);
    
    // Extract date/time from dataset_pi_time (when data was collected on remote unit)
    uint16_t year = response->header_info.dataset_pi_time.year;
    uint8_t month = response->header_info.dataset_pi_time.month;
    uint8_t day = response->header_info.dataset_pi_time.day;
    uint8_t hour = response->header_info.dataset_pi_time.hour;
    uint8_t min = response->header_info.dataset_pi_time.min;
    uint8_t sec = response->header_info.dataset_pi_time.sec;
    
    // Format filename: DC_<unit_id>_YYYY_MM_DD__HH_M_SS.txt
    char filename[128];
    snprintf(filename, sizeof(filename), "DC_%s_%04d_%02d_%02d__%02d_%02d_%02d.txt",
             unit_id_hex, year, month, day, hour, min, sec);
    
    // Create full path in dcvals directory
    std::string filepath = dc_directory + "/" + filename;
    
    // Calculate temperature (Fahrenheit) and battery voltage
    float battery_voltage = SensorConversions::battery_to_voltage(response->header_info.battery);
    double temperature_f = SensorConversions::temperature_to_fahrenheit(response->header_info.temperature);

    
    // Format date string for file content
    char date_str[32];
    snprintf(date_str, sizeof(date_str), "%04d_%02d_%02d__%02d_%02d_%02d",
             year, month, day, hour, min, sec);
    
    // Open file for writing
    FILE* fp = fopen(filepath.c_str(), "w");
    if (!fp) {
        LOG_ERROR_CTX("file_writer", "Failed to open DC file: %s", filepath.c_str());
        return "";
    }
    
    // Write content line
    // Format: <date> <unit_id> <fixed:0003> <temp> <battery> <16 floats> <16 ints> <0> ;
    fprintf(fp, "%s %s 0003 %.5f %.5f",
            date_str, unit_id_hex, 
            temperature_f, battery_voltage);
    
    // Write 16 -1.00000 values (placeholder float data)
    for (int i = 0; i < 16; i++) {
        fprintf(fp, " -1.00000");
    }
    
    // Write 16 integer values: 2x -2, then 14x -1 (placeholder int data)
    fprintf(fp, " -2 -2");
    for (int i = 0; i < 14; i++) {
        fprintf(fp, " -1");
    }
    
    // Write final 0 and semicolon
    fprintf(fp, " 0 ;\n");
    
    fclose(fp);
    
    LOG_INFO_CTX("file_writer", "Wrote DC file: %s", filepath.c_str());
    
    return filepath;
}
