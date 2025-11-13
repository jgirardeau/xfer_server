#include "UploadCommandBuilder.h"
#include "LinkTimingConstants.h"
#include <cstring>
#include <cstdio>
#include <algorithm>
#include <unordered_set>
#include <cmath>

UploadCommandBuilder::UploadCommandBuilder()
{
}

UploadCommandBuilder::~UploadCommandBuilder()
{
}

std::vector<uint8_t> UploadCommandBuilder::build_full_upload_command(
    uint32_t macid, 
    uint32_t start_addr, 
    uint32_t length) const
{
    std::vector<uint8_t> cmd_buffer(128, 0x30);  // Initialize with padding
    
    int idx = 0;
    
    // Header
    cmd_buffer[idx++] = 0x74;  // 0
    cmd_buffer[idx++] = 0x53;  // 1
    
    // Reserved
    cmd_buffer[idx++] = 0x01;  // 2
    
    // Reserved (4 bytes)
    cmd_buffer[idx++] = 0xff;  // 3-6
    cmd_buffer[idx++] = 0xff;
    cmd_buffer[idx++] = 0xff;
    cmd_buffer[idx++] = 0xff;
    
    // Reserved
    cmd_buffer[idx++] = 0x01;  // 7
    
    // Reserved (5 bytes of 0x30) - skip to 13
    idx = 13;
    
    // MACID (4 bytes, big endian) - First copy at 13-16
    cmd_buffer[idx++] = (macid >> 24) & 0xff;
    cmd_buffer[idx++] = (macid >> 16) & 0xff;
    cmd_buffer[idx++] = (macid >> 8) & 0xff;
    cmd_buffer[idx++] = macid & 0xff;
    
    // MACID (4 bytes, big endian) - Second copy at 17-20
    cmd_buffer[idx++] = (macid >> 24) & 0xff;
    cmd_buffer[idx++] = (macid >> 16) & 0xff;
    cmd_buffer[idx++] = (macid >> 8) & 0xff;
    cmd_buffer[idx++] = macid & 0xff;
    
    // Don't care (24 bytes) - skip to 45
    idx = 45;
    
    // Command
    cmd_buffer[idx++] = 0x51;  // 45
    
    // Sample Start (4 ASCII hex chars) - start_addr / SAMPLES_PER_SEGMENT
    uint32_t start_segment = start_addr / LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT;
    char start_str[5];
    snprintf(start_str, 5, "%04x", start_segment);
    memcpy(&cmd_buffer[idx], start_str, 4);  // Copy only 4 chars, no null
    idx += 4;

    // Sample Length (4 ASCII hex chars) - length / SAMPLES_PER_SEGMENT
    uint32_t length_segments = length / LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT;
    char length_str[5];
    snprintf(length_str, 5, "%04x", length_segments);
    memcpy(&cmd_buffer[idx], length_str, 4);  // Copy only 4 chars, no null
    idx += 4;
    
    // Don't care (72 bytes) - already filled with 0x30
    idx = 126;
    
    // Tail
    cmd_buffer[126] = 0x75;
    cmd_buffer[127] = 0x50;
    
    return cmd_buffer;
}
/**
 * Count how many missing segments fall within a bitmap window
 * 
 * @param start_segment Starting segment of the window
 * @param missing_segments List of missing segment numbers (must be sorted)
 * @param max_segments_per_bitmap Size of bitmap window (532)
 * @return Number of missing segments in range [start_segment, start_segment + max_segments_per_bitmap)
 */
int UploadCommandBuilder::count_segments_in_window(
    int start_segment,
    const std::vector<int>& missing_segments,
    int max_segments_per_bitmap) const
{
    int end_segment = start_segment + max_segments_per_bitmap;
    int count = 0;
    
    for (int seg : missing_segments) {
        if (seg < start_segment) continue;
        if (seg >= end_segment) break;
        count++;
    }
    
    return count;
}

/**
 * Find optimal starting segment for 0x55 partial upload command
 * 
 * Strategy: Scan systematic positions across the entire segment space to find
 * the region with the highest concentration of missing segments. This ensures
 * consistent performance regardless of packet loss patterns.
 * 
 * Early exit: If we find a window that captures all missing segments or fills
 * the bitmap completely, return immediately (can't do better).
 * 
 * @param missing_segments List of missing segment numbers (must be sorted)
 * @param total_segments Total number of segments in the upload
 * @param max_segments_per_bitmap Maximum segments per 0x55 command (532)
 * @return Optimal starting segment, or -1 if no missing segments
 */
int UploadCommandBuilder::find_optimal_start_segment(
    const std::vector<int>& missing_segments,
    int total_segments,
    int max_segments_per_bitmap) const
{
    if (missing_segments.empty()) {
        return -1;
    }
    
    // If few segments remaining, just use first missing (no optimization needed)
    if (missing_segments.size() < LinkTiming::BITMAP_OPTIMIZATION_THRESHOLD) {
        return missing_segments[0];
    }
    
    int best_start = missing_segments[0];
    int best_count = 0;
    
    const int scan_stride = LinkTiming::BITMAP_SCAN_STRIDE;  // 28
    const int total_missing = missing_segments.size();
    
    // Calculate the maximum possible count we could achieve
    // It's the minimum of: total missing segments OR bitmap capacity
    const int max_possible_count = std::min(total_missing, max_segments_per_bitmap);
    
    // Strategy: Scan systematic positions across the entire segment space
    // This ensures consistent ~19 evaluations (532÷28 or ~19) regardless of where
    // missing segments happen to fall due to random packet loss
    
    // Phase 1: Scan every scan_stride-th position (0, 28, 56, 84, 112, ...)
    // This provides systematic coverage across the entire segment space
    for (int scan_pos = 0; scan_pos < total_segments; scan_pos += scan_stride) {
        int count = count_segments_in_window(scan_pos, missing_segments, 
                                            max_segments_per_bitmap);
        
        if (count > best_count) {
            best_start = scan_pos;
            best_count = count;
            
            // Early exit: If we've captured all missing segments OR filled the bitmap,
            // we've found the perfect solution - no need to keep searching
            if (best_count >= max_possible_count) {
                return best_start;
            }
        }
    }
    
    // Phase 2: Check first missing segment (if we haven't already found perfect solution)
    // This is often optimal for sequential loss patterns (e.g., timeout after segment N)
    int first_count = count_segments_in_window(missing_segments[0], missing_segments,
                                              max_segments_per_bitmap);
    
    if (first_count > best_count) {
        best_start = missing_segments[0];
        best_count = first_count;
        
        // Note: No need to check for perfect here - if first was perfect,
        // Phase 1 would have found it (since we always check scan position 0)
    }
    
    return best_start;
}


std::vector<uint8_t> UploadCommandBuilder::build_partial_upload_command(
    uint32_t macid,
    int start_segment,
    const std::vector<int>& missing_segments,
    int total_segments,
    int *total_segments_used
    ) const
{
    std::vector<uint8_t> cmd_buffer(128, 0x30);  // Initialize with padding
    
    // OPTIMIZATION: Find better start segment based on density
    int optimized_start = find_optimal_start_segment(
        missing_segments,
        total_segments,
        LinkTiming::UPLOAD_MAX_SEGMENTS_PER_0X55
    );
    
    // Use optimized start if found
    if (optimized_start >= 0) {
        start_segment = optimized_start;
    }
    
    int idx = 0;
    
    // Header
    cmd_buffer[idx++] = 0x74;  // 0
    cmd_buffer[idx++] = 0x53;  // 1
    
    // Reserved
    cmd_buffer[idx++] = 0x01;  // 2
    
    // Reserved (4 bytes)
    cmd_buffer[idx++] = 0xff;  // 3-6
    cmd_buffer[idx++] = 0xff;
    cmd_buffer[idx++] = 0xff;
    cmd_buffer[idx++] = 0xff;
    
    // Reserved
    cmd_buffer[idx++] = 0x01;  // 7
    
    // Reserved (5 bytes) - skip to 13
    idx = 13;
    
    // MACID (4 bytes, big endian) - First copy at 13-16
    cmd_buffer[idx++] = (macid >> 24) & 0xff;
    cmd_buffer[idx++] = (macid >> 16) & 0xff;
    cmd_buffer[idx++] = (macid >> 8) & 0xff;
    cmd_buffer[idx++] = macid & 0xff;
    
    // MACID (4 bytes, big endian) - Second copy at 17-20
    cmd_buffer[idx++] = (macid >> 24) & 0xff;
    cmd_buffer[idx++] = (macid >> 16) & 0xff;
    cmd_buffer[idx++] = (macid >> 8) & 0xff;
    cmd_buffer[idx++] = macid & 0xff;
    
    // Don't care (24 bytes) - skip to 45
    idx = 45;
    
    // Command
    cmd_buffer[idx++] = 0x55;  // 45
    
    // Sample Start (4 ASCII hex chars) - this is the segment number
    char start_str[5];
    snprintf(start_str, 5, "%04x", start_segment);
    memcpy(&cmd_buffer[idx], start_str, 4);
    idx += 4;
    
    // Build bitmask (76 bytes) starting from the optimized segment
    unsigned char bitmask[76];
    *total_segments_used=build_segment_bitmask(bitmask, start_segment, missing_segments, total_segments);
    memcpy(&cmd_buffer[idx], bitmask, 76);
    idx += 76;
    
    // Tail
    cmd_buffer[126] = 0x75;
    cmd_buffer[127] = 0x50;
    
    return cmd_buffer;
}

int UploadCommandBuilder::build_segment_bitmask(
    unsigned char* bitmask,
    int start_segment,
    const std::vector<int>& missing_segments,
    int total_segments) const
{
    // Initialize all to 0x01 (LSB always 1, all other bits 0)
    memset(bitmask, 0x01, 76);
    int total_segments_used=0;
    
    // Create a set for O(1) lookup of missing segments
    std::unordered_set<int> missing_set(missing_segments.begin(), missing_segments.end());
    
    int current_segment = start_segment;
    
    // Build bitmask for 76 bytes
    // Each byte covers 7 segments (bits 7-1) + LSB is always 1
    for (int byte_idx = 0; byte_idx < 76; byte_idx++) {
        unsigned char byte_value = 0x01;  // LSB always set
        
        // Process bits 7 down to 1 (skip bit 0)
        for (int bit = 7; bit >= 1; bit--) {
            if (current_segment >= total_segments) {
                // Past the end of our segments, leave bit as 0
                current_segment++;
                continue;
            }
            
            // Check if this segment is missing
            if (missing_set.find(current_segment) != missing_set.end()) {
                // Segment is missing - set the bit to 1
                byte_value |= (1 << bit);
                total_segments_used++;
            }
            // If segment is received, bit stays 0
            
            current_segment++;
        }
        
        bitmask[byte_idx] = byte_value;
        
        // If we've gone past all segments, fill rest with 0x01
        if (current_segment >= total_segments) {
            for (int remaining = byte_idx + 1; remaining < 76; remaining++) {
                bitmask[remaining] = 0x01;
            }
            break;
        }
    }
    return total_segments_used;
}
