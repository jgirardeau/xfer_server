#ifndef UPLOAD_COORDINATOR_H
#define UPLOAD_COORDINATOR_H

#include <cstdint>
#include <string>
#include "CommandProcessor.h"
#include "SessionStateTracker.h"

class CTS1X;  // Forward declaration
class UploadManager;
class SessionTimeoutTracker;

class UploadCoordinator
{
public:
    UploadCoordinator(CTS1X* core);
    ~UploadCoordinator();
    
    // Upload management
    UploadManager* get_upload_manager() const { return upload_mgr; }
    
    // Process upload states
    void process_upload_init(SessionStateTracker& state_tracker, 
                            SessionTimeoutTracker& timeout_tracker,
                            uint32_t current_macid);
    
    void process_upload_active(SessionStateTracker& state_tracker,
                              SessionTimeoutTracker& timeout_tracker,
                              uint32_t current_macid);
    
    void process_upload_retry(SessionStateTracker& state_tracker,
                             uint32_t current_macid);
    
    // Handle upload response from 'R' command
    void handle_r_command_response(const CommandResponse& response,
                                   SessionStateTracker& state_tracker);
    
    // Check if we have a pending upload ready
    bool has_pending_upload() const { return pending_upload_response_valid; }
    
    // Get the pending upload data length
    uint32_t get_pending_upload_length() const { return pending_upload_data_length; }
    
    // Clear pending upload
    void clear_pending_upload() { pending_upload_response_valid = false; }
    
    // Get pending response
    const CommandResponse* get_pending_response() const { 
        return pending_upload_response_valid ? &pending_upload_response : nullptr; 
    }
    
    // Check if R command received ACK
    bool has_r_command_ack() const { return r_command_received_ack; }
    void set_r_command_ack(bool ack) { r_command_received_ack = ack; }
    
    // Touch alive file for a node
    void touch_alive_file(uint32_t macid);
    
    // Complete upload and write files
    void complete_upload_and_write_files(uint32_t macid, const std::string& completion_path);
    
private:
    // Unified upload result logging - single source of truth for all upload outcomes
    void log_upload_result(bool success, uint32_t macid, const std::string& reason);
    
    // Helper methods
    void evaluate_and_handle_timeout(SessionStateTracker& state_tracker,
                                    uint32_t current_macid);
    
    // State
    CTS1X* ts1x_core;
    UploadManager* upload_mgr;
    
    // Upload response tracking
    CommandResponse pending_upload_response;
    bool pending_upload_response_valid;
    uint32_t pending_upload_data_length;
    
    // R command ACK tracking
    bool r_command_received_ack;
};

#endif // UPLOAD_COORDINATOR_H
