#include "DataFileWriter.h"
#include "logger.h"
#include <sys/stat.h>
#include <sys/types.h>
#include <errno.h>
#include <cmath>
#include <cstdio>

// Data scaling constant
#define DATA_SCALE (1.0 / 20971.52)

// Sample rate mapping (code -> Hz)
static const double SAMPLE_RATE_MAP[] = {
    20000.0,  // 0
    10000.0,  // 1
    5000.0,   // 2
    2500.0,   // 3
    1250.0,   // 4
    625.0,    // 5
    312.0,    // 6
    156.0     // 7
};

// Helper function to create directory recursively
static bool create_directory_recursive(const std::string& path) {
    struct stat st;
    if (stat(path.c_str(), &st) == 0) {
        if (S_ISDIR(st.st_mode)) {
            return true;
        }
        LOG_ERROR_CTX("data_writer", "Path exists but is not a directory: %s", path.c_str());
        return false;
    }
    
    size_t slash_pos = path.find_last_of('/');
    if (slash_pos != std::string::npos && slash_pos > 0) {
        std::string parent = path.substr(0, slash_pos);
        if (!create_directory_recursive(parent)) {
            return false;
        }
    }
    
    if (mkdir(path.c_str(), 0755) != 0) {
        if (errno != EEXIST) {
            LOG_ERROR_CTX("data_writer", "Failed to create directory %s: %s", 
                         path.c_str(), strerror(errno));
            return false;
        }
    }
    
    return true;
}

// Calculate mean from raw data
static double calculate_mean(const std::vector<int16_t>& data) {
    if (data.empty()) {
        return 0.0;
    }
    
    double mean = 0.0;
    for (int16_t sample : data) {
        mean+=sample;
    }
    
    return mean / ((double)data.size());
}

// Calculate RMS from scaled data

static double calculate_rms(const std::vector<int16_t>& data,int meani) {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum_squares = 0.0;
    for (int16_t sample : data) {
        double scaled_sample = (sample-meani) * DATA_SCALE;
        sum_squares += scaled_sample * scaled_sample;
    }
    
    return std::sqrt(sum_squares / data.size());
}

static double calculate_rms_debug(const std::vector<int16_t>& data,int meani) {
    if (data.empty()) {
        return 0.0;
    }
    
    double sum_squares = 0.0;
    LOG_INFO_CTX("data_writer", "RMS Debug: data.size()=%zu", data.size());
    LOG_INFO_CTX("data_writer", "RMS Debug: DATA_SCALE=%.10e", DATA_SCALE);
    
    // Log first few samples
    for (size_t i = 0; i < std::min(size_t(5), data.size()); i++) {
        int dataval=data[i]-meani;
        double scaled = dataval * DATA_SCALE;
        LOG_INFO_CTX("data_writer", "RMS Sample[%zu]: raw=%d, scaled=%.10e, squared=%.10e",
                     i, dataval, scaled, scaled * scaled);
    }
    
    for (int16_t sample : data) {
        double scaled_sample = (sample-meani) * DATA_SCALE;
        sum_squares += scaled_sample * scaled_sample;
    }
    
    double rms = std::sqrt(sum_squares / data.size());
    LOG_INFO_CTX("data_writer", "RMS Debug: sum_squares=%.10e, count=%zu, rms=%.10e",
                 sum_squares, data.size(), rms);
    
    return rms;
}

void fprintf_3digit_exp(FILE* fp, double value) {
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.6e", value);
    
    // Find the 'e' in scientific notation
    char* e_pos = strchr(buffer, 'e');
    if (!e_pos) e_pos = strchr(buffer, 'E');
    
    // If exponent is only 2 digits (e.g., "e-04"), expand to 3 (e.g., "e-004")
    if (e_pos && strlen(e_pos) == 4) {  // "e-04" is 4 chars
        char sign = e_pos[1];           // '+' or '-'
        int exp_val = abs(atoi(e_pos + 2));
        sprintf(e_pos, "e%c%03d", sign, exp_val);
    }
    
    fprintf(fp, "%s\n", buffer);
}

std::string write_data_file(
    const std::string& ts1_data_files,
    const std::vector<int16_t>& data,
    const CommandResponse* response
)
{
    if (!response || !response->has_header_info) {
        LOG_ERROR_CTX("data_writer", "Cannot write data file: no valid response or header info");
        return "";
    }
    
    if (data.empty()) {
        LOG_ERROR_CTX("data_writer", "Cannot write data file: no data samples");
        return "";
    }
    
    // Validate channel mask (must be 0x01 or 0x02)
    uint8_t channel_mask = response->descriptor_channel_mask;
    if (channel_mask != 0x01 && channel_mask != 0x02) {
        LOG_ERROR_CTX("data_writer", "Invalid channel mask: 0x%02X (must be 0x01 or 0x02)", 
                     channel_mask);
        return "";
    }
    
    // Determine channel string and start channel number
    const char* channel_str = (channel_mask == 0x01) ? "ch1" : "ch2";
    int start_channel = (channel_mask == 0x01) ? 1 : 2;
    
    // Extract unit_id as hex string (8 characters, lowercase)
    char unit_id_hex[9];
    snprintf(unit_id_hex, sizeof(unit_id_hex), "%08x", response->unit_id);
    
    // Extract echobase (source_macid) as hex string
    char echobase_hex[9];
    snprintf(echobase_hex, sizeof(echobase_hex), "%08x", response->source_macid);
    
    // Extract date/time from dataset_pi_time (when data was collected on remote unit)
    uint16_t year = response->header_info.dataset_pi_time.year;
    uint8_t month = response->header_info.dataset_pi_time.month;
    uint8_t day = response->header_info.dataset_pi_time.day;
    uint8_t hour = response->header_info.dataset_pi_time.hour;
    uint8_t min = response->header_info.dataset_pi_time.min;
    uint8_t sec = response->header_info.dataset_pi_time.sec;
    
    // Format filename: YYYY_MM_DD__HH_M_SS.txt
    char filename[64];
    snprintf(filename, sizeof(filename), "%04d_%02d_%02d__%02d_%02d_%02d.txt",
             year, month, day, hour, min, sec);
    
    // Construct directory path: ts1_data_files/<unit_id>_ch<1/2>/
    std::string data_directory = ts1_data_files + "/" + unit_id_hex + "_" + channel_str;
    
    // Create directory structure if it doesn't exist
    if (!create_directory_recursive(data_directory)) {
        LOG_ERROR_CTX("data_writer", "Failed to create data directory: %s", data_directory.c_str());
        return "";
    }
    
    // Create full file path
    std::string filepath = data_directory + "/" + filename;
    
    // Get sample rate
    double sample_rate = 0.0;
    uint8_t rate_code = response->descriptor_sample_rate;
    if (rate_code < 8) {
        sample_rate = SAMPLE_RATE_MAP[rate_code];
    } else {
        LOG_ERROR_CTX("data_writer", "Invalid sample rate code: %d", rate_code);
        return "";
    }
    
    // Calculate RMS
    // Write data samples (scaled, in scientific notation)
    double mean=calculate_mean(data);
    int meani=mean;
    double rms = calculate_rms(data,meani);
    
    // Open file for writing
    FILE* fp = fopen(filepath.c_str(), "w");
    if (!fp) {
        LOG_ERROR_CTX("data_writer", "Failed to open data file: %s", filepath.c_str());
        return "";
    }
    
    // Write header
    fprintf(fp, ";PodID %s\n", unit_id_hex);
    fprintf(fp, ";Date Year(%d) Month(%d) Day(%02d) Hour(%02d) Minutes(%02d) Seconds(%02d)\n",
            year, month, day, hour, min, sec);
    fprintf(fp, ";FSampleRate %f\n", sample_rate);
    fprintf(fp, ";Channels 1\n");
    fprintf(fp, ";nStart_channel %d\n", start_channel);
    fprintf(fp, ";Units 0\n");
    fprintf(fp, ";echobase %s\n", echobase_hex);
    fprintf(fp, ";Agc 1\n");
    // Optional fields (commented out for now, can be added later):
    // fprintf(fp, ";battery: %d\n", response->header_info.battery);
    // fprintf(fp, ";temperature: %d\n", response->header_info.temperature);
    // fprintf(fp, ";bluetooth_on: %d\n", response->header_bleon);
    // fprintf(fp, ";fpga_on: %d\n", response->header_fpgaon);
    // fprintf(fp, ";current_mistlx_time: %u\n", response->header_mincount);
    // fprintf(fp, ";sample_mistlx_time: %u\n", response->header_mincount);
    // fprintf(fp, ";fail_count: %u\n", response->header_failcount);
    fprintf(fp, ";Samples %zu\n", data.size());
    fprintf(fp, ";RMS %f\n", rms);
    fprintf(fp, ";channelIds -2 -1\n");

  
    for (int16_t sample : data) {
        double scaled_sample = (sample-meani) * DATA_SCALE;
        //fprintf(fp, "%e\n", scaled_sample);
        fprintf_3digit_exp(fp, scaled_sample);
    }
    
    fclose(fp);
    
    LOG_INFO_CTX("data_writer", "Wrote data file: %s (%zu samples, RMS=%.6f)", 
                 filepath.c_str(), data.size(), rms);
    
    return filepath;
}
