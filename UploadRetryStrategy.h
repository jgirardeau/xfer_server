#ifndef UPLOAD_RETRY_STRATEGY_H
#define UPLOAD_RETRY_STRATEGY_H

#include <string>
#include "UploadTypes.h"

class UploadRetryStrategy
{
public:
    UploadRetryStrategy();
    ~UploadRetryStrategy();
    
    // Evaluate what retry action to take
    RetryDecision evaluate(
        UploadState current_state,
        int segments_received,
        int total_segments,
        int retry_count,
        int max_retries,
        std::string& reason) const;
    
    // Calculate expected packets with 5% success rate
    int calculate_expected_packets(int total_segments) const;
    
    // Constants
    static const int MAX_SEGMENTS_PER_0X55 = 532;  // Bitmask limitation (76×7)
    
    // Strategy flag: When true, always use partial uploads (0x55) like legacy unit
    // When false, use intelligent strategy (full upload for severe packet loss)
    // Set to true to match legacy unit behavior
    static const bool FORCE_PARTIAL_UPLOAD = true;
    
private:
    // No state needed - pure decision logic
};

#endif // UPLOAD_RETRY_STRATEGY_H
