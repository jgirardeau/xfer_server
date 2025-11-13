#ifndef SAMPLESETGENERATOR_H
#define SAMPLESETGENERATOR_H

#include "Ts1xSamplingReader.h"
#include <vector>
#include <cstdint>

// Structure for a sampleset entry
struct Sampleset {
    uint32_t nodeid;           // Serial number (converted from hex string)
    uint8_t sampling_mask;     // Bitmask of active channels (bit 0 = ch0, bit 1 = ch1, etc.)
    double max_freq;           // Maximum frequency (0.0 for DC channels)
    int resolution;            // Resolution (0 for DC channels)
    double interval;           // Sampling interval in seconds
    uint8_t priority;          // Priority flag: 1 = priority, 0 = normal
    uint8_t ac_dc_flag;        // AC/DC flag: 0 = DC, 1 = AC
};

// Create optimized samplesets from TS1X channel configuration
// Combines channels with matching serial, type, interval, and (for AC) max_freq/resolution
// Returns a compressed vector of samplesets with channel bitmasks
std::vector<Sampleset> createSamplesets(const std::vector<Ts1xChannel>& ts1x_channels);

// Helper function to print samplesets for debugging
void printSamplesets(const std::vector<Sampleset>& samplesets);

#endif // SAMPLESETGENERATOR_H
