#ifndef UPLOAD_MANAGER_H
#define UPLOAD_MANAGER_H

#include <cstdint>
#include <vector>
#include <string>
#include <cstring>
#include <chrono>
#include "CommandProcessor.h"
#include "UploadTypes.h"
#include "UploadSegmentTracker.h"
#include "UploadTimeoutManager.h"
#include "UploadRetryStrategy.h"
#include "UploadCommandBuilder.h"
#include "UploadStatistics.h"

// Note: All timing constants moved to LinkTimingConstants.h
// Do not add timing constants here - use LinkTiming:: namespace instead

class CTS1X;  // Forward declaration

class UploadManager
{
public:
    UploadManager(CTS1X* core);
    ~UploadManager();
    
    // Backward compatibility: Re-export types from UploadTypes.h
    // This allows existing code using UploadManager::RetryDecision to still work
    typedef ::RetryDecision RetryDecision;
    static const ::RetryDecision DECISION_WAIT = ::DECISION_WAIT;
    static const ::RetryDecision DECISION_RETRY_FULL = ::DECISION_RETRY_FULL;
    static const ::RetryDecision DECISION_RETRY_PARTIAL = ::DECISION_RETRY_PARTIAL;
    
    // Start upload from a node
    bool start_full_upload(uint32_t macid, uint32_t start_addr, uint32_t num_samples, 
            const CommandResponse* triggering_response = nullptr);
    bool send_init_command();
    
    // Send partial upload for missing segments
    bool send_partial_upload();

    // Get the response that triggered this upload
    const CommandResponse* get_triggering_response() const { return triggering_response; }

    // Decode data length from descriptor field
    static uint32_t decode_data_length_from_descriptor(uint16_t descriptor);
    
    // Process incoming upload response packet
    bool process_upload_response(const CommandResponse& response);
    
    // Check if upload is complete
    bool is_complete() const;
    
    // Check if upload failed
    bool has_failed() const;
    
    // Check if global timeout has been exceeded
    bool check_global_timeout();
    
    // Get current state
    UploadState get_state() const { return current_state; }
    const char* state_to_string() const;
    
    // Get upload statistics
    int get_total_segments() const { return segment_tracker.get_total_count(); }
    int get_received_segments() const { return segment_tracker.get_received_count(); }
    int get_missing_segments() const { return segment_tracker.get_missing_count(); }
    int get_retry_count() const { return retry_count; }
    int get_max_retries() const { return max_retries; }
    int get_retry_timeout_ms() const { return retry_timeout_ms; }
    double get_link_rate_percent() const { 
        return statistics.get_link_rate_percent();
    }
    
    // Timeout management
    int64_t get_ms_since_last_packet() const;
    int64_t get_ms_since_upload_start() const;
    void reset_packet_timer();
    
    // Get the uploaded data
    std::vector<int16_t> get_data() const;
    
    // Reset for new upload
    void reset();
    
    // Reset for retry (keeps triggering response and increments retry count)
    void reset_for_retry();
    
    // Get adaptive timeout based on current state and retry count
    int get_adaptive_timeout_ms() const;
    
    // Calculate expected upload time and global timeout
    int64_t get_expected_upload_time_ms() const;
    int64_t get_global_timeout_ms() const;
    
    // Decision logic for retry strategy
    RetryDecision evaluate_retry_strategy(std::string& reason) const;
    
private:
    // State management
    const char* state_to_string(UploadState state) const;
    void transition_state(UploadState new_state, const std::string& reason);
    
    // State
    CTS1X* ts1x_core;
    UploadState current_state;
    uint32_t current_macid;
    uint32_t upload_start_addr;
    uint32_t upload_length;
    
    // Retry management
    int retry_count;
    int max_retries;
    int retry_timeout_ms;
    
    // Component managers (internal helpers)
    UploadSegmentTracker segment_tracker;
    UploadTimeoutManager timeout_manager;
    UploadRetryStrategy retry_strategy;
    UploadCommandBuilder command_builder;
    UploadStatistics statistics;

    // Store the response that triggered the upload (for file output)
    CommandResponse* triggering_response;
    
    // Helper for sending 0x51 and 0x55
    bool send_init_command_0x55();  // Initial 0x55 (all segments missing)
    bool send_upload_command_0x51(uint32_t start_addr, uint32_t length);
    bool send_upload_command_0x55(uint32_t start_addr);
};

#endif // UPLOAD_MANAGER_H
