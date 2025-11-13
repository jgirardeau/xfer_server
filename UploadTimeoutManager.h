#ifndef UPLOAD_TIMEOUT_MANAGER_H
#define UPLOAD_TIMEOUT_MANAGER_H

#include <chrono>
#include <cstdint>
#include "UploadTypes.h"

// Note: All timing constants moved to LinkTimingConstants.h
// Do not add timing constants here - use LinkTiming:: namespace instead

class UploadTimeoutManager
{
public:
    UploadTimeoutManager();
    ~UploadTimeoutManager();
    
    // Start tracking for a new upload session
    void start_session(int total_segments);
    
    // Reset packet timer (called when we receive a packet)
    void reset_packet_timer();
    
    // Get elapsed time
    int64_t get_ms_since_last_packet() const;
    int64_t get_ms_since_session_start() const;
    
    // Adaptive timeout based on state and progress
    int get_adaptive_timeout_ms(UploadState state, double completion_rate) const;
    
    // Expected upload time calculations
    int64_t get_expected_upload_time_ms(int total_segments) const;
    int64_t get_global_timeout_ms(int total_segments) const;
    bool check_global_timeout(int total_segments) const;
    
    // Reset for new session
    void reset();
    
private:
    std::chrono::steady_clock::time_point session_start_time;
    std::chrono::steady_clock::time_point last_packet_time;
};

#endif // UPLOAD_TIMEOUT_MANAGER_H
