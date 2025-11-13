#include "SamplesetGenerator.h"
#include "UnitType.h"
#include "logger.h"

#include <map>
#include <cstdio>
#include <algorithm>

// Helper structure for grouping channels by their common attributes
struct SamplesetKey {
    uint32_t nodeid;           // Serial number
    std::string channel_type;  // "DC" or "AC"
    double interval;           // Sampling interval
    double max_freq;           // Only relevant for AC channels
    int resolution;            // Only relevant for AC channels
    
    // Comparison operator needed for std::map
    bool operator<(const SamplesetKey& other) const {
        if (nodeid != other.nodeid) return nodeid < other.nodeid;
        if (channel_type != other.channel_type) return channel_type < other.channel_type;
        if (interval != other.interval) return interval < other.interval;
        if (max_freq != other.max_freq) return max_freq < other.max_freq;
        return resolution < other.resolution;
    }
};

// Convert hex string like "0x00111578" to uint32_t
static uint32_t parseSerial(const std::string& serial_str) {
    try {
        return std::stoul(serial_str, nullptr, 16);
    } catch (const std::exception& e) {
        LOG_ERROR_CTX("sampleset", "Failed to parse serial: %s", serial_str.c_str());
        return 0;
    }
}

std::vector<Sampleset> createSamplesets(const std::vector<Ts1xChannel>& ts1x_channels) {
    std::vector<Sampleset> samplesets;
    
    if (ts1x_channels.empty()) {
        LOG_WARN_CTX("sampleset", "No channels to process");
        return samplesets;
    }
    
    // Use a map to group channels by their common attributes
    // Key = common attributes, Value = bitmask of channels
    std::map<SamplesetKey, uint8_t> grouped_channels;
    std::map<SamplesetKey, uint8_t> grouped_priority;  // Track priority per group
    
    int skipped_invalid_serial = 0;
    int skipped_invalid_channel = 0;
    int skipped_echobase = 0;
    
    for (const auto& channel : ts1x_channels) {
        // Parse serial number from hex string to uint32_t
        uint32_t nodeid = parseSerial(channel.serial);
        if (nodeid == 0) {
            LOG_WARN_CTX("sampleset", "Skipping channel with invalid serial: %s", 
                        channel.serial.c_str());
            skipped_invalid_serial++;
            continue;
        }
        
        // Filter out EchoBase nodes - they should only be in nodelist_force.txt
        // Samplesets are for TS1X, StormX, and other non-EchoBase units
        if (is_echobox(nodeid)) {
            skipped_echobase++;
            LOG_WARN_CTX("sampleset", "Skipping EchoBase node 0x%08x - EchoBase nodes should be in nodelist_force.txt, not samplesets", 
                        nodeid);
            continue;
        }
        
        // Validate channel number (must be 0-7 for 8-bit mask)
        if (channel.channel_num < 0 || channel.channel_num > 7) {
            LOG_WARN_CTX("sampleset", "Skipping channel with invalid channel number: %d", 
                        channel.channel_num);
            skipped_invalid_channel++;
            continue;
        }
        
        // Create key for grouping
        // Channels can be combined if they share all these attributes
        SamplesetKey key;
        key.nodeid = nodeid;
        key.channel_type = channel.channel_type;
        key.interval = channel.interval;
        key.max_freq = channel.max_freq;      // 0.0 for DC (ignored in comparison)
        key.resolution = channel.resolution;  // 0 for DC (ignored in comparison)
        
        // Add this channel to the appropriate group by ORing its bit into the mask
        uint8_t channel_bit = 1 << channel.channel_num;
        grouped_channels[key] |= channel_bit;
        
        // Track priority: if ANY channel in the group has priority, mark the group as priority
        if (channel.priority != 0) {
            grouped_priority[key] = 1;
        }
    }
    
    // Convert the grouped channels map into a vector of samplesets
    for (const auto& pair : grouped_channels) {
        const SamplesetKey& key = pair.first;
        uint8_t mask = pair.second;
        
        Sampleset sampleset;
        sampleset.nodeid = key.nodeid;
        sampleset.sampling_mask = mask;
        sampleset.max_freq = key.max_freq;
        sampleset.resolution = key.resolution;
        sampleset.interval = key.interval;
        sampleset.priority = grouped_priority[key];  // Will be 0 if not set
        sampleset.ac_dc_flag = (key.channel_type == "AC") ? 1 : 0;  // 0=DC, 1=AC
        
        samplesets.push_back(sampleset);
    }
    
    // Sort samplesets: by nodeid, then DC (0) before AC (1)
    std::sort(samplesets.begin(), samplesets.end(), 
        [](const Sampleset& a, const Sampleset& b) {
            if (a.nodeid != b.nodeid) return a.nodeid < b.nodeid;
            return a.ac_dc_flag < b.ac_dc_flag;  // DC (0) before AC (1)
        });
    
    // Log summary
    LOG_INFO_CTX("sampleset", "Created %zu samplesets from %zu channels", 
                samplesets.size(), ts1x_channels.size());
    if (skipped_invalid_serial > 0 || skipped_invalid_channel > 0 || skipped_echobase > 0) {
        LOG_WARN_CTX("sampleset", "Skipped %d channels (invalid serial: %d, invalid channel#: %d, EchoBase: %d)",
                    skipped_invalid_serial + skipped_invalid_channel + skipped_echobase,
                    skipped_invalid_serial, skipped_invalid_channel, skipped_echobase);
    }
    
    return samplesets;
}

void printSamplesets(const std::vector<Sampleset>& samplesets) {
    LOG_INFO_CTX("sampleset", "=== SAMPLESETS (%zu total) ===", samplesets.size());
    LOG_INFO_CTX("sampleset", "NodeID       | Mask | AC/DC | Max Freq  | Resolution | Interval | Priority | Channels");
    LOG_INFO_CTX("sampleset", "-------------+------+-------+-----------+------------+----------+----------+---------");
    
    for (const auto& ss : samplesets) {
        // Build the output line in a string buffer
        char line[256];
        int pos = 0;
        
        // NodeID in hex
        pos += snprintf(line + pos, sizeof(line) - pos, "0x%08x | ", ss.nodeid);
        
        // Mask in hex
        pos += snprintf(line + pos, sizeof(line) - pos, "0x%02x | ", ss.sampling_mask);
        
        // AC/DC flag
        pos += snprintf(line + pos, sizeof(line) - pos, "  %s   | ", 
                       ss.ac_dc_flag ? "AC" : "DC");
        
        // Max frequency
        if (ss.max_freq > 0.0) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%9.1f | ", ss.max_freq);
        } else {
            pos += snprintf(line + pos, sizeof(line) - pos, "    -     | ");
        }
        
        // Resolution
        if (ss.resolution > 0) {
            pos += snprintf(line + pos, sizeof(line) - pos, "%10d | ", ss.resolution);
        } else {
            pos += snprintf(line + pos, sizeof(line) - pos, "     -      | ");
        }
        
        // Interval and priority
        pos += snprintf(line + pos, sizeof(line) - pos, "%8.1f | ", ss.interval);
        pos += snprintf(line + pos, sizeof(line) - pos, "%8d | ", static_cast<int>(ss.priority));
        
        // Show which channels are included (decode the bitmask)
        bool first = true;
        for (int ch = 0; ch < 8; ch++) {
            if (ss.sampling_mask & (1 << ch)) {
                if (!first) {
                    pos += snprintf(line + pos, sizeof(line) - pos, ",");
                }
                pos += snprintf(line + pos, sizeof(line) - pos, "%d", ch);
                first = false;
            }
        }
        
        LOG_INFO_CTX("sampleset", "%s", line);
    }
}
