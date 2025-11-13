#include "UploadSegmentTracker.h"
#include <algorithm>

UploadSegmentTracker::UploadSegmentTracker()
    : total_segments(0),
      segments_received(0)
{
}

UploadSegmentTracker::~UploadSegmentTracker()
{
}

void UploadSegmentTracker::initialize(int total_segs)
{
    reset();
    total_segments = total_segs;
    
    // Pre-allocate segment vector
    segments.clear();
    segments.reserve(total_segments);
    for (int i = 0; i < total_segments; i++) {
        segments.push_back(UploadSegment(i));
    }
}

bool UploadSegmentTracker::mark_received(int segment_num, const int16_t data[32])
{
    if (segment_num < 0 || segment_num >= total_segments) {
        return false;  // Out of range
    }
    
    if (segments[segment_num].received) {
        return false;  // Already received (duplicate)
    }
    
    // Copy data
    for (int i = 0; i < 32; i++) {
        segments[segment_num].data[i] = data[i];
    }
    
    segments[segment_num].received = true;
    segments_received++;
    
    return true;
}

bool UploadSegmentTracker::is_received(int segment_num) const
{
    if (segment_num < 0 || segment_num >= total_segments) {
        return false;
    }
    return segments[segment_num].received;
}

std::vector<int> UploadSegmentTracker::get_missing_segments() const
{
    std::vector<int> missing;
    for (int i = 0; i < total_segments; i++) {
        if (!segments[i].received) {
            missing.push_back(i);
        }
    }
    return missing;
}

bool UploadSegmentTracker::is_complete() const
{
    return (segments_received == total_segments) && (total_segments > 0);
}

std::vector<int16_t> UploadSegmentTracker::get_all_data() const
{
    std::vector<int16_t> result;
    result.reserve(total_segments * 32);
    
    for (const auto& segment : segments) {
        for (int i = 0; i < 32; i++) {
            result.push_back(segment.data[i]);
        }
    }
    
    return result;
}

void UploadSegmentTracker::reset()
{
    segments.clear();
    total_segments = 0;
    segments_received = 0;
}
