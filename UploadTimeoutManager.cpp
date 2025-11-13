#include "UploadTimeoutManager.h"
#include "UploadTypes.h"
#include "LinkTimingConstants.h"
#include <algorithm>

UploadTimeoutManager::UploadTimeoutManager()
{
    reset();
}

UploadTimeoutManager::~UploadTimeoutManager()
{
}

void UploadTimeoutManager::start_session(int total_segments)
{
    session_start_time = std::chrono::steady_clock::now();
    last_packet_time = session_start_time;
}

void UploadTimeoutManager::reset_packet_timer()
{
    last_packet_time = std::chrono::steady_clock::now();
}

int64_t UploadTimeoutManager::get_ms_since_last_packet() const
{
    if (last_packet_time.time_since_epoch().count() == 0) {
        return 0;  // Timer not started yet
    }
    
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - last_packet_time).count();
}

int64_t UploadTimeoutManager::get_ms_since_session_start() const
{
    if (session_start_time.time_since_epoch().count() == 0) {
        return 0;  // Session not started yet
    }
    
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - session_start_time).count();
}

int UploadTimeoutManager::get_adaptive_timeout_ms(UploadState state, double completion_rate) const
{
    if (state == UPLOAD_COMMAND_SENT) {
        return LinkTiming::UPLOAD_INITIAL_TIMEOUT_MS;
    }
    
    // For normal reception - enforce minimum timeout to handle 1+ second gaps
    // Field data shows occasional gaps up to 1137ms from remote units (likely processing delays)
    // These are NOT packet loss - just periodic delays in the remote unit transmission
    //
    // Strategy: Use adaptive timeouts but never go below MIN_PACKET_TIMEOUT_MS
    
    int adaptive_timeout;
    
    if (completion_rate > LinkTiming::UPLOAD_HIGH_COMPLETION_THRESHOLD) {
        // High completion rate - use normal timeout
        adaptive_timeout = LinkTiming::UPLOAD_PACKET_TIMEOUT_NORMAL_MS;
    } else if (completion_rate < LinkTiming::UPLOAD_LOW_COMPLETION_THRESHOLD) {
        // Major packet loss or slow start - be very patient
        adaptive_timeout = LinkTiming::UPLOAD_PACKET_TIMEOUT_HIGH_LOSS_MS;
    } else {
        // Normal case - most of the upload (50-90% completion)
        adaptive_timeout = LinkTiming::UPLOAD_PACKET_TIMEOUT_NORMAL_MS;
    }
    
    // Enforce minimum timeout floor
    if (adaptive_timeout < LinkTiming::UPLOAD_MIN_PACKET_TIMEOUT_MS) {
        adaptive_timeout = LinkTiming::UPLOAD_MIN_PACKET_TIMEOUT_MS;
    }
    
    return adaptive_timeout;
}

int64_t UploadTimeoutManager::get_expected_upload_time_ms(int total_segments) const
{
    // Assume 95% packet loss rate (5% success)
    // Each segment takes PACKET_INTERVAL_MS
    // With 5% success rate, we need EXPECTED_RETRIES_PER_SEGMENT attempts per segment on average
    return (int64_t)total_segments * LinkTiming::UPLOAD_PACKET_INTERVAL_MS * 
           LinkTiming::UPLOAD_EXPECTED_RETRIES_PER_SEGMENT;
}

int64_t UploadTimeoutManager::get_global_timeout_ms(int total_segments) const
{
    // Global timeout is 15X expected time, capped at 8 minutes
    int64_t expected = get_expected_upload_time_ms(total_segments);
    int64_t global_timeout = expected * LinkTiming::UPLOAD_GLOBAL_TIMEOUT_MULTIPLIER;
    return std::min(global_timeout, (int64_t)LinkTiming::UPLOAD_GLOBAL_TIMEOUT_MAX_MS);
}

bool UploadTimeoutManager::check_global_timeout(int total_segments) const
{
    int64_t elapsed = get_ms_since_session_start();
    int64_t timeout = get_global_timeout_ms(total_segments);
    return elapsed > timeout;
}

void UploadTimeoutManager::reset()
{
    session_start_time = std::chrono::steady_clock::time_point();
    last_packet_time = std::chrono::steady_clock::time_point();
}
