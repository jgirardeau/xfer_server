#include "UploadRetryStrategy.h"
#include "UploadTypes.h"
#include <cstdio>

UploadRetryStrategy::UploadRetryStrategy()
{
}

UploadRetryStrategy::~UploadRetryStrategy()
{
}

int UploadRetryStrategy::calculate_expected_packets(int total_segments) const
{
    // Expected number of packets with 5% success rate (95% packet loss)
    return static_cast<int>(total_segments * 0.05);
}

RetryDecision UploadRetryStrategy::evaluate(
    UploadState current_state,
    int segments_received,
    int total_segments,
    int retry_count,
    int max_retries,
    std::string& reason) const
{
    int missing = total_segments - segments_received;
    int expected = calculate_expected_packets(total_segments);
    
    RetryDecision decision;
    std::string original_reason;
    
    // BRANCH 1: No packets received (waiting for first packet after 0x51)
    if (current_state == UPLOAD_COMMAND_SENT && segments_received == 0) {
        original_reason = "No packets after timeout - 0x51 command likely lost (99% confidence)";
        decision = DECISION_RETRY_FULL;
    }
    // BRANCH 2: Very few packets received (possible garbled command or severe link degradation)
    // If we received < 10% of expected packets, something is wrong
    else if (segments_received > 0 && segments_received < expected * 0.10) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer), 
                 "Very few packets: %d received vs %.1f expected (<10%%) - command may be garbled or link degraded",
                 segments_received, (double)expected);
        original_reason = buffer;
        decision = DECISION_RETRY_FULL;
    }
    // BRANCH 3: Missing >80% AND >532 segments (efficiency consideration)
    // 0x55 bitmask can only request 532 segments max (76 bytes x 7 bits/byte)
    // If missing >80% and >532, full retry is much more efficient
    else if (missing > MAX_SEGMENTS_PER_0X55 && 
        missing > static_cast<int>(total_segments * 0.80)) {
        char buffer[256];
        snprintf(buffer, sizeof(buffer),
                 "Missing %d segments (>80%% of %d and >%d) - full retry more efficient than multiple 0x55",
                 missing, total_segments, MAX_SEGMENTS_PER_0X55);
        original_reason = buffer;
        decision = DECISION_RETRY_FULL;
    }
    // BRANCH 4: Normal partial upload (may need multiple 0x55 if >532 missing)
    else if (missing > 0) {
        if (missing > MAX_SEGMENTS_PER_0X55) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                     "Missing %d segments (>%d but <%d%% of total) - partial uploads worthwhile, may need multiple 0x55",
                     missing, MAX_SEGMENTS_PER_0X55, 80);
            original_reason = buffer;
        } else {
            char buffer[256];
            snprintf(buffer, sizeof(buffer),
                     "Missing %d segments - normal partial upload",
                     missing);
            original_reason = buffer;
        }
        decision = DECISION_RETRY_PARTIAL;
    }
    else {
        // No action needed
        original_reason = "Upload complete or no timeout condition";
        decision = DECISION_WAIT;
    }
    
    // Return the decision and reason
    reason = original_reason;
    return decision;
}
