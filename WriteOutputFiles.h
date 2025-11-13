#ifndef WRITE_OUTPUT_FILES_H
#define WRITE_OUTPUT_FILES_H

#include <string>
#include <vector>
#include <cstdint>
#include "CommandProcessor.h"

/**
 * Structure to hold filenames written during upload
 */
struct OutputFileInfo {
    std::string dc_filename;    // DC metadata file (full path)
    std::string data_filename;  // Waveform data file (full path)
    bool success;               // Overall success flag
    
    OutputFileInfo() : success(false) {}
};

/**
 * Write output files after successful upload from a remote unit
 * 
 * @param root_filehandler Base directory for output files
 * @param config_files_directory Directory containing config files
 * @param ts1_data_files Base directory for TS1 data files (DC files go in ts1_data_files/dcvals)
 * @param data Uploaded sample data (16-bit samples)
 * @param triggering_response The response that triggered this upload (contains header info, descriptor, etc.)
 * @return OutputFileInfo struct containing DC and data filenames written
 */
OutputFileInfo write_output_files(
    const std::string& root_filehandler,
    const std::string& config_files_directory,
    const std::string& ts1_data_files,
    const std::vector<int16_t>& data,
    const CommandResponse* triggering_response
);

/**
 * Write DC (Data Collection) file with metadata from response
 * 
 * Creates a DC file with format: DC_<unit_id>_YYYY_MM_DD__HH_M_SS.txt
 * File contains: date, unit_id, count, temperature, battery voltage, and fixed placeholder values
 * Files are written to: ts1_data_files/dcvals/
 * Creates directory structure if it doesn't exist
 * 
 * @param ts1_data_files Base directory for TS1 data files (DC files go in subdirectory /dcvals)
 * @param response The command response containing all metadata
 * @return Full path to the DC file written, or empty string on failure
 */
std::string write_dc_file(
    const std::string& ts1_data_files,
    const CommandResponse* response
);

#endif // WRITE_OUTPUT_FILES_H
