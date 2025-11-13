#include "UploadCoordinator.h"
#include "TS1X.h"
#include "UploadManager.h"
#include "SessionTimeoutTracker.h"
#include "ConfigManager.h"
#include "WriteOutputFiles.h"
#include "LinkTimingConstants.h"
#include "logger.h"
#include "StateLogger.h"
#include "pi_server_sleep.h"
#include <fstream>
#include <cinttypes>  // For PRId64 macro

UploadCoordinator::UploadCoordinator(CTS1X* core)
    : ts1x_core(core),
      pending_upload_response_valid(false),
      pending_upload_data_length(0),
      r_command_received_ack(false)
{
    upload_mgr = new UploadManager(core);
}

UploadCoordinator::~UploadCoordinator()
{
    delete upload_mgr;
}

void UploadCoordinator::touch_alive_file(uint32_t macid)
{
    // Get nodelist_directory from config (where alive files should be)
    std::string nodelist_dir = ConfigManager::instance().get_nodelist_directory();
    
    // Format MAC ID as hex string (lowercase, no 0x prefix)
    char macid_str[9];
    snprintf(macid_str, sizeof(macid_str), "%08x", macid);
    
    // Build filename: echobase_alive_<macid>.txt in nodelist directory
    std::string filename = nodelist_dir + "/echobase_alive_" + macid_str + ".txt";
    
    // Touch the file (create if doesn't exist, update timestamp if it does)
    std::ofstream touch_file(filename, std::ios::app);
    if (touch_file.is_open()) {
        touch_file.close();
        LOG_INFO_CTX("upload_coord", "Touched alive file: %s", filename.c_str());
    } else {
        LOG_WARN_CTX("upload_coord", "Failed to touch alive file: %s", filename.c_str());
    }
}

void UploadCoordinator::log_upload_result(bool success, uint32_t macid, const std::string& reason)
{
    // Get timing and statistics from upload manager
    int64_t duration_ms = upload_mgr->get_ms_since_upload_start();
    int received = upload_mgr->get_received_segments();
    int total = upload_mgr->get_total_segments();
    int retries = upload_mgr->get_retry_count();
    double link_rate = upload_mgr->get_link_rate_percent();
    
    // Calculate completion percentage
    double completion_pct = (total > 0) ? (100.0 * received / total) : 0.0;
    
    // Format duration as seconds with milliseconds
    int duration_sec = duration_ms / 1000;
    int duration_ms_part = duration_ms % 1000;
    
    // Single unified log line - easily greppable with "UPLOAD_RESULT:"
    LOG_STATE("UPLOAD_RESULT: %s | Node: 0x%08X | Duration: %d.%03d s | "
              "Segments: %d/%d (%.1f%%) | Retries: %d | Link: %.1f%% | Reason: %s",
              success ? "SUCCESS" : "FAILED",
              macid,
              duration_sec, duration_ms_part,
              received, total, completion_pct,
              retries,
              link_rate,
              reason.c_str());
    LOG_INFO_CTX("upload_coord","UPLOAD_RESULT: %s | Node: 0x%08X | Duration: %d.%03d s | "
              "Segments: %d/%d (%.1f%%) | Retries: %d | Link: %.1f%% | Reason: %s",
              success ? "SUCCESS" : "FAILED",
              macid,
              duration_sec, duration_ms_part,
              received, total, completion_pct,
              retries,
              link_rate,
              reason.c_str());
}

void UploadCoordinator::handle_r_command_response(const CommandResponse& response,
                                                  SessionStateTracker& state_tracker)
{
    if (response.has_header_info) {
        LOG_INFO_CTX("upload_coord", "Node 0x%08x: 'R' response received", 
                     response.source_macid);
        
        // Set flag that 'R' received a valid ACK
        r_command_received_ack = true;
        
        // Touch the alive file
        touch_alive_file(response.source_macid);
        
        if (response.header_info.data_control_bits != 0) {
            // Decode data length from descriptor RIGHT NOW
            pending_upload_data_length = 
                UploadManager::decode_data_length_from_descriptor(
                    response.header_info.descriptor);
            
            LOG_INFO_CTX("upload_coord", "  -> Node HAS DATA (control_bits=0x%02x, "
                         "crc=0x%08x, descriptor=0x%04x, length=%d samples)",
                         response.header_info.data_control_bits,
                         response.on_deck_crc,
                         response.header_info.descriptor,
                         pending_upload_data_length);
            
            // Store response and mark as valid
            pending_upload_response = response;
            pending_upload_response_valid = true;
            
            state_tracker.transition_state(STATE_DATA_UPLOAD_INIT, 
                                          "Node has data ready for upload");
            LOG_INFO_CTX("upload_coord", "Initiating data upload from node 0x%08x "
                         "(%d samples, %d segments)", 
                         response.source_macid, pending_upload_data_length, 
                         (pending_upload_data_length + 31) / 32);
            
        } else {
            LOG_INFO_CTX("upload_coord", "  -> Node alive, no data");
        }
    } else {
        LOG_INFO_CTX("upload_coord", "Node 0x%08x: 'A' response (simple ack)", 
                     response.source_macid);
        
        // Touch alive file for 'A' command response
        touch_alive_file(response.source_macid);
    }
}

void UploadCoordinator::complete_upload_and_write_files(uint32_t macid, 
                                                        const std::string& completion_path)
{
    LOG_INFO_CTX("upload_coord", "Upload complete from node 0x%08x: %d/%d segments (via %s)",
                 macid, upload_mgr->get_received_segments(), 
                 upload_mgr->get_total_segments(), completion_path.c_str());
    
    // Log unified upload result - SINGLE SOURCE OF TRUTH
    log_upload_result(true, macid, completion_path);
    
    // Write output files
    std::string root_filehandler = ConfigManager::instance().get_root_filehandler();
    std::string config_files_dir = ConfigManager::instance().get_config_files_directory();
    std::string ts1_data_files = ConfigManager::instance().get_ts1_data_files();
    std::vector<int16_t> upload_data = upload_mgr->get_data();
    const CommandResponse* trigger_response = upload_mgr->get_triggering_response();
    
    OutputFileInfo file_info = write_output_files(root_filehandler, config_files_dir, 
                                                   ts1_data_files, upload_data, trigger_response);
    
    // Log the written filenames
    if (file_info.success) {
        LOG_STATE("FILES WRITTEN: DC=%s | DATA=%s", 
                  file_info.dc_filename.c_str(),
                  file_info.data_filename.c_str());
    } else {
        LOG_STATE("FILE WRITE ERROR: Failed to write output files for node 0x%08X", macid);
    }
}

void UploadCoordinator::process_upload_init(SessionStateTracker& state_tracker, 
                                           SessionTimeoutTracker& timeout_tracker,
                                           uint32_t current_macid)
{
    // Initialize upload parameters and start settling timer
    // We need to wait for ACKs from previous 'R' command to clear
    if (upload_mgr->get_state() == UPLOAD_IDLE) {
        // Validate we have a pending upload response
        if (!pending_upload_response_valid) {
            LOG_ERROR_CTX("upload_coord", "No valid pending upload response!");
            state_tracker.transition_state(STATE_ERROR, 
                                          "Upload init without valid triggering response");
            return;
        }
        
        // Use the already-decoded data length
        if (upload_mgr->start_full_upload(current_macid, 0, pending_upload_data_length, 
                                         &pending_upload_response)) {
            LOG_INFO_CTX("upload_coord", 
                         "Upload initialized for node 0x%08x: %d samples (%d segments), "
                         "starting %lld ms settling before 0x51",
                         current_macid, pending_upload_data_length,
                         (pending_upload_data_length + 31) / 32,
                         LinkTiming::UPLOAD_INIT_STATE_TIMEOUT_MS);
            
            // Log to state logger
            LOG_STATE("UPLOAD START: Node 0x%08X | Samples: %d | Segments: %d",
                      current_macid, pending_upload_data_length,
                      (pending_upload_data_length + 31) / 32);
            
            timeout_tracker.reset_timer();
            
            // Clear the flag after successful use
            pending_upload_response_valid = false;
        } else {
            LOG_ERROR_CTX("upload_coord", "Failed to initialize upload");
            
            // Log unified result - SINGLE SOURCE OF TRUTH
            log_upload_result(false, current_macid, "Failed to initialize upload manager");
            
            state_tracker.transition_state(STATE_ERROR, "Upload manager failed to initialize");
            pending_upload_response_valid = false;
        }
    } else {
        // Wait for settling delay, then send 0x51
        int64_t elapsed_ms = timeout_tracker.get_elapsed_ms();
        
        if (elapsed_ms >= LinkTiming::UPLOAD_INIT_STATE_TIMEOUT_MS) {
            LOG_INFO_CTX("upload_coord", "Settling complete, sending 0x51 command (after %lld ms)", 
                        elapsed_ms);
            if (upload_mgr->send_init_command()) {
                LOG_STATE("TX: 0x51 upload init command to node 0x%08X", current_macid);
                state_tracker.transition_state(STATE_DATA_UPLOAD_ACTIVE, 
                                              "0x51 sent, waiting for settling before 0x55");
                timeout_tracker.reset_timer();  // Start timer for next settling
            } else {
                LOG_ERROR_CTX("upload_coord", "Failed to send 0x51 command");
                
                // Log unified result - SINGLE SOURCE OF TRUTH
                log_upload_result(false, current_macid, "Failed to send 0x51 init command");
                
                state_tracker.transition_state(STATE_ERROR, 
                                              "Failed to send 0x51 upload init command");
            }
        }
    }
}

void UploadCoordinator::evaluate_and_handle_timeout(SessionStateTracker& state_tracker,
                                                   uint32_t current_macid)
{
    int adaptive_timeout = upload_mgr->get_adaptive_timeout_ms();
    int64_t ms_since_packet = upload_mgr->get_ms_since_last_packet();
    
    // Check for timeout using adaptive timeout
    if (ms_since_packet > adaptive_timeout) {
        std::string reason;
        LOG_INFO_CTX("upload_coord", 
                  "Packet timeout: waited %" PRId64 " ms (threshold: %d ms)",
                  ms_since_packet, adaptive_timeout);
        UploadManager::RetryDecision decision = upload_mgr->evaluate_retry_strategy(reason);
        
        switch (decision) {
            case UploadManager::DECISION_RETRY_FULL:
            {
                // BRANCHES 1, 2, or 3: Retry full upload (send 0x51 again)
                LOG_STATE("TIMEOUT: Full retry decision | %s | Retry: %d/%d",
                         reason.c_str(),
                         upload_mgr->get_retry_count() + 1,
                         upload_mgr->get_max_retries());
                
                // Check if we've exceeded max retries
                if (upload_mgr->get_retry_count() >= upload_mgr->get_max_retries()) {
                    LOG_ERROR_CTX("upload_coord", "Max retry attempts exceeded (%d)", 
                                upload_mgr->get_max_retries());
                    
                    // Log unified result - SINGLE SOURCE OF TRUTH
                    log_upload_result(false, current_macid, 
                                    "Max retries exceeded on initial command timeout");
                    
                    // Go through STATE_ERROR to properly reset dwell_count and move to next node
                    state_tracker.transition_state(STATE_ERROR, "Upload abandoned - max retries exceeded");
                    state_tracker.set_result(RESULT_ERROR);
                } else {
                    // Reset and retry full upload
                    upload_mgr->reset_for_retry();
                    
                    // Wait brief settling period before resending 0x51
                    Server_sleep_ms(LinkTiming::UPLOAD_TX_SETTLING_MS);
                    
                    if (!upload_mgr->send_init_command()) {
                        LOG_ERROR_CTX("upload_coord", "Failed to retry 0x51 command");
                        
                        // Log unified result - SINGLE SOURCE OF TRUTH
                        log_upload_result(false, current_macid, "Failed to send retry 0x51 command");
                        
                        state_tracker.transition_state(STATE_ERROR, 
                                                      "Failed to retry initial upload command");
                    } else {
                        LOG_INFO_CTX("upload_coord", "Retrying 0x51 command (attempt %d/%d) - %s",
                                    upload_mgr->get_retry_count(), 
                                    upload_mgr->get_max_retries(),
                                    reason.c_str());
                        LOG_STATE("TX: Retry 0x51 to node 0x%08X | Attempt: %d/%d | %s",
                                 current_macid, 
                                 upload_mgr->get_retry_count(), 
                                 upload_mgr->get_max_retries(),
                                 reason.c_str());
                    }
                }
                break;
            }
            
            case UploadManager::DECISION_RETRY_PARTIAL:
            {
                // BRANCH 4: Send 0x55 for missing segments
                LOG_STATE("TIMEOUT: Partial retry decision | %s | Segments: %d/%d missing | Retry: %d/%d",
                         reason.c_str(),
                         upload_mgr->get_received_segments(),
                         upload_mgr->get_total_segments(),
                         upload_mgr->get_missing_segments(),
                         upload_mgr->get_retry_count() + 1,
                         upload_mgr->get_max_retries());
                
                if (!upload_mgr->send_partial_upload()) {
                    LOG_ERROR_CTX("upload_coord", "Failed to send 0x55 retry request");
                    
                    // Log unified result - SINGLE SOURCE OF TRUTH
                    log_upload_result(false, current_macid, "Failed to send 0x55 retry command");
                    
                    state_tracker.transition_state(STATE_ERROR, 
                                                  "Failed to send timeout-triggered retry");
                } else {
                    state_tracker.transition_state(STATE_DATA_UPLOAD_RETRY, 
                                                  "Sent 0x55 retry request, waiting for response");
                    LOG_INFO_CTX("upload_coord", "Sent 0x55 retry request - %s", reason.c_str());
                }
                break;
            }
            
            case UploadManager::DECISION_WAIT:
            default:
                // No action needed, continue waiting
                LOG_DEBUG_CTX("upload_coord", "Timeout evaluation: continue waiting");
                break;
        }
    }
}

void UploadCoordinator::process_upload_active(SessionStateTracker& state_tracker,
                                              SessionTimeoutTracker& timeout_tracker,
                                              uint32_t current_macid)
{
    if (upload_mgr->is_complete()) {
        // Don't write files here - let SessionManager::STATE_DATA_UPLOAD_COMPLETE handle it
        // This ensures consistent dwell logic for all upload completion paths
        state_tracker.transition_state(STATE_DATA_UPLOAD_COMPLETE, 
                                      "Upload completed successfully");
    } else if (upload_mgr->has_failed()) {
        LOG_ERROR_CTX("upload_coord", "Upload failed from node 0x%08x after retries",
                      current_macid);
        
        // Log unified result - SINGLE SOURCE OF TRUTH
        log_upload_result(false, current_macid, "Max retries exceeded");
        
        state_tracker.transition_state(STATE_ERROR, "Upload exceeded maximum retry attempts");
    } else if (upload_mgr->check_global_timeout()) {
        // Global timeout exceeded (15X expected time or 8 minutes max)
        LOG_ERROR_CTX("upload_coord", "Upload abandoned due to global timeout");
        
        // Log unified result - SINGLE SOURCE OF TRUTH
        log_upload_result(false, current_macid, "Global timeout exceeded");
        
        // Go through STATE_ERROR to properly reset dwell_count and move to next node
        state_tracker.transition_state(STATE_ERROR, "Upload timeout - global timeout exceeded");
        state_tracker.set_result(RESULT_ERROR);
    } else {
        // After sending 0x51, we need to send 0x55 to actually request data
        if (upload_mgr->get_state() == UPLOAD_COMMAND_SENT) {
            // Wait for settling delay to let 4 ACKs flush out
            int64_t elapsed_ms = timeout_tracker.get_elapsed_ms();
            
            if (elapsed_ms >= LinkTiming::UPLOAD_ACTIVE_STATE_TIMEOUT_MS) {
                LOG_INFO_CTX("upload_coord", 
                            "Sending initial data request (0x55) for node 0x%08x (after %lld ms settling)",
                             current_macid, elapsed_ms);
                if (!upload_mgr->send_partial_upload()) {
                    LOG_ERROR_CTX("upload_coord", "Failed to send initial partial upload");
                    
                    // Log unified result - SINGLE SOURCE OF TRUTH
                    log_upload_result(false, current_macid, "Failed to send initial 0x55 data request");
                    
                    state_tracker.transition_state(STATE_ERROR, 
                                                  "Failed to send 0x55 partial upload command");
                } else {
                    LOG_STATE("TX: Initial 0x55 data request to node 0x%08X", current_macid);
                }
            }
        } else if (upload_mgr->get_state() == UPLOAD_COMMAND_SENT ||
                   (upload_mgr->get_state() == UPLOAD_RECEIVING &&
                    upload_mgr->get_missing_segments() > 0)) {
            // Check for timeout and evaluate retry strategy
            evaluate_and_handle_timeout(state_tracker, current_macid);
        } else {
            // DIAGNOSTIC: Log when the condition doesn't match
            static int diagnostic_counter = 0;
            if (++diagnostic_counter % 100 == 0) {
                LOG_DEBUG_CTX("upload_coord", 
                             "Upload state check: state=%s, missing=%d, complete=%d, failed=%d",
                             upload_mgr->state_to_string(),
                             upload_mgr->get_missing_segments(),
                             upload_mgr->is_complete() ? 1 : 0,
                             upload_mgr->has_failed() ? 1 : 0);
            }
        }
    }
}

void UploadCoordinator::process_upload_retry(SessionStateTracker& state_tracker,
                                             uint32_t current_macid)
{
    // Check if upload completed
    if (upload_mgr->is_complete()) {
        state_tracker.transition_state(STATE_DATA_UPLOAD_COMPLETE, 
                                      "All segments received after retry");
        return;
    }
    
    // Check if upload failed (max retries exceeded)
    if (upload_mgr->has_failed()) {
        LOG_ERROR_CTX("upload_coord", "Upload failed after max retries");
        
        // Log unified result - SINGLE SOURCE OF TRUTH
        log_upload_result(false, current_macid, "Max retries exceeded in retry state");
        
        // Go through STATE_ERROR to properly reset dwell_count and move to next node
        state_tracker.transition_state(STATE_ERROR, "Upload failed - returning to node list");
        state_tracker.set_result(RESULT_ERROR);
        return;
    }
    
    // Check global timeout
    if (upload_mgr->check_global_timeout()) {
        LOG_ERROR_CTX("upload_coord", "Upload abandoned due to global timeout during retry");
        
        // Log unified result - SINGLE SOURCE OF TRUTH
        log_upload_result(false, current_macid, "Global timeout exceeded in retry state");
        
        // Go through STATE_ERROR to properly reset dwell_count and move to next node
        state_tracker.transition_state(STATE_ERROR, "Upload timeout - global timeout exceeded in retry");
        state_tracker.set_result(RESULT_ERROR);
        return;
    }
    
    // Check if we're receiving data (transition back to ACTIVE state for normal processing)
    if (upload_mgr->get_state() == UPLOAD_RECEIVING) {
        state_tracker.transition_state(STATE_DATA_UPLOAD_ACTIVE, 
                                      "Receiving data after retry, resuming normal upload");
        return;
    }
    
    // Check for timeout while waiting for retry response
    int retry_timeout = upload_mgr->get_retry_timeout_ms();
    if (upload_mgr->get_ms_since_last_packet() > retry_timeout) {
        LOG_WARN_CTX("upload_coord", "No response to 0x55 retry after %d ms, re-sending", 
                    retry_timeout);
        
        // Check if we've exceeded max retries
        if (upload_mgr->get_retry_count() >= upload_mgr->get_max_retries()) {
            LOG_ERROR_CTX("upload_coord", "Max retry attempts exceeded (%d)", 
                         upload_mgr->get_max_retries());
            
            // Log unified result - SINGLE SOURCE OF TRUTH
            log_upload_result(false, current_macid, "Max retries exceeded waiting for retry response");
            
            // Go through STATE_ERROR to properly reset dwell_count and move to next node
            state_tracker.transition_state(STATE_ERROR, "Upload abandoned - max retries exceeded");
            state_tracker.set_result(RESULT_ERROR);
            return;
        }
        
        // Re-send partial upload request
        if (upload_mgr->send_partial_upload()) {
            LOG_INFO_CTX("upload_coord", "Re-sent 0x55 retry request after timeout");
            LOG_STATE("TX: Re-send 0x55 retry (no response) | Retry: %d/%d | Missing: %d segments",
                      upload_mgr->get_retry_count(),
                      upload_mgr->get_max_retries(),
                      upload_mgr->get_missing_segments());
        } else {
            LOG_ERROR_CTX("upload_coord", "Failed to re-send 0x55 retry request");
            
            // Log unified result - SINGLE SOURCE OF TRUTH
            log_upload_result(false, current_macid, "Failed to send retry command");
            
            // Go through STATE_ERROR to properly reset dwell_count and move to next node
            state_tracker.transition_state(STATE_ERROR, "Upload failed - could not send retry");
            state_tracker.set_result(RESULT_ERROR);
        }
    }
}
