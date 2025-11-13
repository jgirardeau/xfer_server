#ifndef UPLOAD_COMMAND_BUILDER_H
#define UPLOAD_COMMAND_BUILDER_H

#include <vector>
#include <cstdint>

class UploadCommandBuilder
{
public:
    UploadCommandBuilder();
    ~UploadCommandBuilder();
    
    /**
     * Build a 0x51 full upload command
     */
    std::vector<uint8_t> build_full_upload_command(
        uint32_t macid,
        uint32_t start_addr,
        uint32_t length) const;
    
    /**
     * Build a 0x55 partial upload command with bitmap
     * Automatically optimizes start_segment to maximize bitmap density
     */
    std::vector<uint8_t> build_partial_upload_command(
        uint32_t macid,
        int suggested_start_segment,
        const std::vector<int>& missing_segments,
        int total_segments,
        int* out_segments_used = nullptr) const;
    
private:
    /**
     * Find optimal starting segment for 0x55 partial upload command
     * 
     * Scans systematic positions across the entire segment space to find
     * the region with the highest concentration of missing segments.
     * 
     * @param missing_segments List of missing segment numbers (must be sorted)
     * @param total_segments Total number of segments in the upload
     * @param max_segments_per_bitmap Maximum segments per 0x55 command (532)
     * @return Optimal starting segment, or -1 if no missing segments
     */
    int find_optimal_start_segment(
        const std::vector<int>& missing_segments,
        int total_segments,
        int max_segments_per_bitmap) const;
    
    /**
     * Count how many missing segments fall within a bitmap window
     * 
     * @param start_segment Starting segment of the window
     * @param missing_segments List of missing segment numbers (must be sorted)
     * @param max_segments_per_bitmap Size of bitmap window (532)
     * @return Number of missing segments in range [start_segment, start_segment + max_segments_per_bitmap)
     */
    int count_segments_in_window(
        int start_segment,
        const std::vector<int>& missing_segments,
        int max_segments_per_bitmap) const;
    
    /**
     * Build the segment bitmask for a 0x55 command
     * 
     * @param bitmask Output buffer for bitmap bytes (must be at least 76 bytes)
     * @param start_segment First segment number in the bitmap
     * @param missing_segments List of missing segment numbers
     * @param total_segments Total segments in upload
     * @return Number of segments represented in the bitmask
     */
    int build_segment_bitmask(
        unsigned char* bitmask,
        int start_segment,
        const std::vector<int>& missing_segments,
        int total_segments) const;
};

#endif // UPLOAD_COMMAND_BUILDER_H