#ifndef UPLOAD_SEGMENT_TRACKER_H
#define UPLOAD_SEGMENT_TRACKER_H

#include <cstdint>
#include <vector>
#include <cstring>

// Upload segment (32 samples = 64 bytes)
struct UploadSegment {
    uint16_t address;       // Segment number (0, 1, 2, ...)
    bool received;
    int16_t data[32];       // 32x 16-bit samples
    
    UploadSegment(uint16_t addr) : address(addr), received(false) {
        memset(data, 0, sizeof(data));
    }
};

class UploadSegmentTracker
{
public:
    UploadSegmentTracker();
    ~UploadSegmentTracker();
    
    // Initialize for a new upload
    void initialize(int total_segments);
    
    // Mark a segment as received and store its data
    bool mark_received(int segment_num, const int16_t data[32]);
    
    // Check if a segment has been received
    bool is_received(int segment_num) const;
    
    // Get list of missing segment numbers
    std::vector<int> get_missing_segments() const;
    
    // Get counts
    int get_received_count() const { return segments_received; }
    int get_total_count() const { return total_segments; }
    int get_missing_count() const { return total_segments - segments_received; }
    
    // Check if all segments received
    bool is_complete() const;
    
    // Get all data as a contiguous vector
    std::vector<int16_t> get_all_data() const;
    
    // Reset for new upload
    void reset();
    
private:
    std::vector<UploadSegment> segments;
    int total_segments;
    int segments_received;
};

#endif // UPLOAD_SEGMENT_TRACKER_H
