#ifndef TS1XSAMPLINGREADER_H
#define TS1XSAMPLINGREADER_H

#include <string>
#include <vector>

// Structure to hold TS1X/StormX channel sampling configuration data
struct Ts1xChannel {
    std::string hw_type;        // Hardware type: "TS1X" or "StormX"
    std::string serial;         // Serial number (hex format like "0x00111578")
    int port;                   // Port number (typically 820)
    int channel_num;            // Channel number (0-7)
    std::string channel_type;   // Channel type: "DC" or "AC"
    std::string channel_id;     // UUID channel identifier
    double interval;            // Sampling interval in seconds
    double adj_interval;        // Adjusted sampling interval in seconds
    double max_freq;            // Maximum frequency (Hz) for AC channels, 0.0 for DC
    int resolution;             // Resolution for AC channels, 0 for DC
    std::string last_sampled;   // Last sampled timestamp
    int priority;               // Priority (typically 0)
    int is_demod;               // Demodulation flag (0 or 1)
    std::string external_input; // External input flag ("True" or "False")
    std::string external_name;  // External input name (or "-")
};

// Read and parse the TS1X sampling configuration file
// Returns a vector of Ts1xChannel structures
// On error, logs the error and returns whatever could be parsed (possibly empty)
std::vector<Ts1xChannel> readTs1xSamplingFile(const std::string& filepath);

#endif // TS1XSAMPLINGREADER_H
