#include "UploadManager.h"
#include "TS1X.h"
#include "LinkTimingConstants.h"
#include "logger.h"
#include "StateLogger.h"
#include <cstring>
#include <algorithm>

UploadManager::UploadManager(CTS1X* core)
    : ts1x_core(core),
      current_state(UPLOAD_IDLE),
      current_macid(0),
      upload_start_addr(0),
      upload_length(0),
      retry_count(0),
      max_retries(LinkTiming::UPLOAD_MAX_RETRY_COUNT),
      retry_timeout_ms(LinkTiming::UPLOAD_RETRY_TIMEOUT_MS),
      triggering_response(nullptr)
{
    LOG_INFO_CTX("upload_mgr", "UploadManager initialized (max_retries=%d, retry_timeout=%d ms)", 
                 max_retries, retry_timeout_ms);
}

UploadManager::~UploadManager()
{
    // Clean up stored response if it exists
    if (triggering_response) {
        delete triggering_response;
        triggering_response = nullptr;
    }
}

const char* UploadManager::state_to_string(UploadState state) const
{
    switch (state) {
        case UPLOAD_IDLE: return "IDLE";
        case UPLOAD_INIT: return "INIT";
        case UPLOAD_COMMAND_SENT: return "COMMAND_SENT";
        case UPLOAD_RECEIVING: return "RECEIVING";
        case UPLOAD_RETRY_PARTIAL: return "RETRY_PARTIAL";
        default: return "UNKNOWN";
    }
}

uint32_t UploadManager::decode_data_length_from_descriptor(uint16_t descriptor)
{
    // Extract low byte of descriptor
    // Formula from remote unit: data_length = ((low_byte) + 1) * SAMPLES_PER_DESCRIPTOR_UNIT
    // This gives the number of samples
    uint8_t low_byte = descriptor & 0xFF;
    uint32_t data_length_samples = (low_byte + 1) * LinkTiming::UPLOAD_SAMPLES_PER_DESCRIPTOR_UNIT;
    
    return data_length_samples;
}

const char* UploadManager::state_to_string() const
{
    return state_to_string(current_state);
}

void UploadManager::transition_state(UploadState new_state, const std::string& reason)
{
    if (new_state != current_state) {
        LOG_INFO_CTX("upload_mgr", "STATE TRANSITION: %s -> %s | Reason: %s",
                     state_to_string(current_state),
                     state_to_string(new_state),
                     reason.c_str());
        
        // Log to state logger
        LOG_STATE("UPLOAD STATE: %s -> %s | %s",
                  state_to_string(current_state),
                  state_to_string(new_state),
                  reason.c_str());
        
        current_state = new_state;
    }
}

void UploadManager::reset()
{
    segment_tracker.reset();
    timeout_manager.reset();
    statistics.reset();
    
    transition_state(UPLOAD_IDLE, "Reset upload manager");
    current_macid = 0;
    upload_start_addr = 0;
    upload_length = 0;
    retry_count = 0;
    
    // Clean up stored response
    if (triggering_response) {
        delete triggering_response;
        triggering_response = nullptr;
    }
}

void UploadManager::reset_for_retry()
{
    // Reset upload state but keep the triggering response
    int total_segments = segment_tracker.get_total_count();
    
    segment_tracker.reset();
    segment_tracker.initialize(total_segments);
    
    retry_count++;  // Increment retry counter
    
    LOG_INFO_CTX("upload_mgr", "Retrying full upload (attempt %d/%d) - assuming initial command was lost",
                 retry_count, max_retries);
    
    transition_state(UPLOAD_INIT, "Retrying upload after initial command timeout");
    
    // Keep: current_macid, upload_start_addr, upload_length, triggering_response
}

int UploadManager::get_adaptive_timeout_ms() const
{
    double completion_rate = (double)segment_tracker.get_received_count() / 
                            segment_tracker.get_total_count();
    return timeout_manager.get_adaptive_timeout_ms(current_state, completion_rate);
}

int64_t UploadManager::get_ms_since_last_packet() const
{
    return timeout_manager.get_ms_since_last_packet();
}

int64_t UploadManager::get_ms_since_upload_start() const
{
    return timeout_manager.get_ms_since_session_start();
}

void UploadManager::reset_packet_timer()
{
    timeout_manager.reset_packet_timer();
}

int64_t UploadManager::get_expected_upload_time_ms() const
{
    return timeout_manager.get_expected_upload_time_ms(segment_tracker.get_total_count());
}

int64_t UploadManager::get_global_timeout_ms() const
{
    return timeout_manager.get_global_timeout_ms(segment_tracker.get_total_count());
}

bool UploadManager::check_global_timeout()
{
    return timeout_manager.check_global_timeout(segment_tracker.get_total_count());
}

UploadManager::RetryDecision UploadManager::evaluate_retry_strategy(std::string& reason) const
{
    RetryDecision decision = retry_strategy.evaluate(
        current_state,
        segment_tracker.get_received_count(),
        segment_tracker.get_total_count(),
        retry_count,
        max_retries,
        reason);
    
    // Map internal enum to public enum (they're the same, but type safety)
    switch (decision) {
        case DECISION_WAIT: return DECISION_WAIT;
        case DECISION_RETRY_FULL: return DECISION_RETRY_FULL;
        case DECISION_RETRY_PARTIAL: return DECISION_RETRY_PARTIAL;
        default: return DECISION_WAIT;
    }
}

bool UploadManager::start_full_upload(uint32_t macid, uint32_t start_addr, uint32_t num_samples,
                                     const CommandResponse* triggering_resp)
{
    if (current_state != UPLOAD_IDLE) {
        LOG_ERROR_CTX("upload_mgr", "Cannot start upload - not in IDLE state");
        return false;
    }
    
    current_macid = macid;
    upload_start_addr = start_addr;
    upload_length = num_samples;
    
    // Calculate total segments needed (round up)
    int total_segs = (num_samples + LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT - 1) / 
                     LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT;
    
    // Initialize segment tracker
    segment_tracker.initialize(total_segs);
    
    // Start timeout tracking
    timeout_manager.start_session(total_segs);
    
    // Store a copy of the triggering response for file writing later
    if (triggering_resp) {
        triggering_response = new CommandResponse(*triggering_resp);
    }
    
    transition_state(UPLOAD_INIT, "Upload session initialized");
    
    LOG_INFO_CTX("upload_mgr", "Initialized upload: macid=0x%08x, start=%d, samples=%d, segments=%d",
                 macid, start_addr, num_samples, total_segs);

    
    
    return true;
}

bool UploadManager::send_init_command()
{
    if (current_state != UPLOAD_INIT) {
        LOG_ERROR_CTX("upload_mgr", "Cannot send init command - not in INIT state");
        return false;
    }

    // Check if we should use partial upload mode (0x55) or full upload mode (0x51)
    if (UploadRetryStrategy::FORCE_PARTIAL_UPLOAD) {
        // Use 0x55 with all segments marked missing (legacy mode)
        // This is functionally equivalent to 0x51 but uses the more flexible 0x55 format
        return send_init_command_0x55();
    } else {
        // Use traditional 0x51 full upload command
        return send_upload_command_0x51(upload_start_addr, upload_length);
    }
}

bool UploadManager::send_init_command_0x55()
{
    // Send initial 0x55 command with all segments marked as missing
    // This is functionally equivalent to 0x51 but uses partial upload format
    // At this point, segment_tracker has all segments marked as not received (missing)
    
    int start_segment = 0;  // Always start from segment 0
    std::vector<int> missing = segment_tracker.get_missing_segments();
    
    // Build command using command builder (with automatic optimization)
    int total_segments_used;
    std::vector<uint8_t> cmd = command_builder.build_partial_upload_command(
        current_macid,
        start_segment,
        missing,
        segment_tracker.get_total_count(),
        &total_segments_used);

    // Send command
    ts1x_core->send_command(cmd.data(), cmd.size());
    
    transition_state(UPLOAD_COMMAND_SENT, "Sent 0x55 upload init command (partial mode)");
    
    // Track statistics
    int total_segs = segment_tracker.get_total_count();
    statistics.on_segments_requested(total_segs);
    
    LOG_INFO_CTX("upload_mgr", "Sent 0x55 upload init command: start_seg=%d, requesting %d segments (FORCE_PARTIAL_UPLOAD mode)",
                 start_segment, total_segs);
    
    // Log to state logger
    LOG_STATE("TX: 0x55 upload init | Start: %d | Segments: %d (partial mode)",
              start_segment, total_segs);
    
    return true;
}

bool UploadManager::send_upload_command_0x51(uint32_t start_addr, uint32_t length)
{
    // Build command using command builder
    std::vector<uint8_t> cmd = command_builder.build_full_upload_command(
        current_macid, start_addr, length);
    
    // Send command
    ts1x_core->send_command(cmd.data(), cmd.size());
    
    transition_state(UPLOAD_COMMAND_SENT, "Sent 0x51 upload init command");
    
    // Track statistics
    int total_segs = segment_tracker.get_total_count();
    statistics.on_segments_requested(total_segs);
    
    LOG_INFO_CTX("upload_mgr", "Sent 0x51 upload command: start=%d, length=%d (expecting %d segments)",
                 start_addr / LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT, 
                 length / LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT, 
                 total_segs);
    
    // Log to state logger
    LOG_STATE("TX: 0x51 full upload | Start: %d | Length: %d | Segments: %d",
              start_addr / LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT, 
              length / LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT, 
              total_segs);
    
    return true;
}

bool UploadManager::send_partial_upload()
{
    if (segment_tracker.is_complete()) {
        return false;  // Already complete
    }
    
    // Get missing segments
    std::vector<int> missing = segment_tracker.get_missing_segments();
    
    if (missing.empty()) {
        return false;  // Nothing missing
    }
    
    int first_missing = missing[0];
    int missing_count = (int)missing.size();
    
    // Track statistics
    statistics.on_segments_requested(missing_count);
    
    LOG_INFO_CTX("upload_mgr", "First missing segment: %d, requesting %d segments", 
                 first_missing, missing_count);
    
    reset_packet_timer();  // Reset timeout timer on retry
    
    // Convert segment number to byte address
    // Formula: segment * SAMPLES_PER_SEGMENT * BYTES_PER_SAMPLE
    uint32_t first_missing_addr = first_missing * LinkTiming::UPLOAD_SAMPLES_PER_SEGMENT * 
                                   LinkTiming::UPLOAD_BYTES_PER_SAMPLE;
    
    return send_upload_command_0x55(first_missing_addr);
}

bool UploadManager::send_upload_command_0x55(uint32_t start_addr)
{
    // Get missing segments
    std::vector<int> missing = segment_tracker.get_missing_segments();
    
    // Convert byte address to segment number
    int start_segment = start_addr / LinkTiming::UPLOAD_BYTES_PER_SEGMENT;

    // Build command using OPTIMIZED command builder
    // The command builder will automatically find the best start_segment
    // to maximize bitmap density - no manual override logic needed!
    int total_segments_used;
    std::vector<uint8_t> cmd = command_builder.build_partial_upload_command(
        current_macid,
        start_segment,
        missing,
        segment_tracker.get_total_count(),
        &total_segments_used);
    
    // Send command
    ts1x_core->send_command(cmd.data(), cmd.size());
    
    transition_state(UPLOAD_RETRY_PARTIAL, "Sent 0x55 partial upload request");
    retry_count++;
    
    LOG_INFO_CTX("upload_mgr", "Sent 0x55 partial upload (retry %d/%d): start_seg=%d, %d segments missing",
                 retry_count, max_retries, start_segment, get_missing_segments());
    
    // Log to state logger with details
    LOG_STATE("TX: 0x55 partial upload | Retry: %d/%d | Missing: %d segments | First missing: %d",
              retry_count, max_retries, get_missing_segments(), start_segment);
    
    return true;
}

bool UploadManager::process_upload_response(const CommandResponse& response)
{
    // Check if this is upload data
    if (!response.has_upload_data) {
        LOG_ERROR_CTX("upload_mgr", "Response is not upload data");
        return false;
    }
    
    statistics.on_packet_received();
    
    // Checksum was already verified in CommandReceiver
    if (!response.crc_valid) {
        statistics.on_checksum_error();
        LOG_ERROR_CTX("upload_mgr", "Checksum error in upload packet");
        return false;
    }
    
    uint16_t segment_addr = response.upload_segment_addr;
    int total_segments = segment_tracker.get_total_count();
    
    if (segment_addr >= total_segments) {
        // It's normal for units to send extra segments - just ignore them
        LOG_INFO_CTX("upload_mgr", "Ignoring out-of-range segment %d (expected 0-%d)",
                      segment_addr, total_segments - 1);
        return true;  // Not an error, just ignore it
    }
    
    // Check if already received
    if (segment_tracker.is_received(segment_addr)) {
        LOG_WARN_CTX("upload_mgr", "Duplicate segment %d", segment_addr);
        return true;
    }
    
    // Store the segment data
    bool stored = segment_tracker.mark_received(segment_addr, response.upload_data);
    
    if (stored) {
        
        // Reset timeout timer - we just received a packet
        reset_packet_timer();
        
        transition_state(UPLOAD_RECEIVING, "Received upload data segment");
        
        LOG_INFO_CTX("upload_mgr", "Received %s segment %d (%d/%d)", 
                      response.is_fast_mode ? "Fast" : "Slow",
                      segment_addr, segment_tracker.get_received_count(), 
                      segment_tracker.get_total_count());
        
        // Log progress every 10 segments
        if (segment_tracker.get_received_count() % 10 == 0) {
            LOG_INFO_CTX("upload_mgr", "Upload progress: %d/%d segments (%.1f%%)",
                         segment_tracker.get_received_count(), 
                         segment_tracker.get_total_count(),
                         100.0 * segment_tracker.get_received_count() / segment_tracker.get_total_count());
        }
    }
    
    return true;
}

bool UploadManager::is_complete() const
{
    return segment_tracker.is_complete();
}

bool UploadManager::has_failed() const
{
    return (retry_count >= max_retries);
}

std::vector<int16_t> UploadManager::get_data() const
{
    return segment_tracker.get_all_data();
}
