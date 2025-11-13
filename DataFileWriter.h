#ifndef DATA_FILE_WRITER_H
#define DATA_FILE_WRITER_H

#include <string>
#include <vector>
#include <cstdint>
#include "CommandProcessor.h"

/**
 * Write data file with waveform samples
 * 
 * Creates a data file with format: YYYY_MM_DD__HH_M_SS.txt
 * File location: ts1_data_files/<unit_id>_ch<1/2>/filename.txt
 * 
 * @param ts1_data_files Base directory for TS1 data files
 * @param data Vector of 16-bit sample data (signed)
 * @param response Command response containing metadata
 * @return Full path to the data file written, or empty string on error
 */
std::string write_data_file(
    const std::string& ts1_data_files,
    const std::vector<int16_t>& data,
    const CommandResponse* response
);

#endif // DATA_FILE_WRITER_H
