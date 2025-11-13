#ifndef SESSION_MANAGER_H
#define SESSION_MANAGER_H

#include <cstdint>
#include <vector>
#include <string>
#include <chrono>
#include "ConfigManager.h"
#include "CommandProcessor.h"
#include "ConfigBroadcaster.h"
#include "NodeListManager.h"
#include "CommandSequenceManager.h"
#include "SessionStateTracker.h"
#include "SessionTimeoutTracker.h"
#include "UploadCoordinator.h"
#include "SamplesetSupervisor.h"

class CTS1X;  // Forward declaration
class UploadManager;

class SessionManager
{
public:
    SessionManager(CTS1X* core);
    ~SessionManager();
    
    // Session control
    void handle_response(const CommandResponse& response);
    void process(CommandResponse* response = nullptr);
    void reset_session();
    
    // Config broadcasting
    void initialize_config_broadcaster(const std::string& config_dir,
                                      unsigned char rssi_threshold,
                                      unsigned char rssi_delay,
                                      unsigned char rssi_increment,
                                      unsigned char power_adjust,
                                      int broadcast_interval_hours);
    void broadcast_config_files();
    bool check_periodic_broadcast();
    
    // Getters
    SessionState get_state() const { return state_tracker.get_state(); }
    SessionResult get_result() const { return state_tracker.get_result(); }
    uint32_t get_current_macid() const { return current_macid; }
    const std::vector<NodeInfo>& get_node_list() const { 
        return nodelist_mgr->get_node_list(); 
    }

    // Erase old config files before broadcasting
    void erase_old_config_files(uint8_t age);

    void set_monitor_mode(bool enable);
    
private:
    // Helper methods
    bool send_command();  // Simplified: sends current command from cmd_seq_mgr
    void process_state_machine();
    
    // State tracking
    uint32_t current_macid;
    int retry_count;
    int upload_counter;
    
    // Settling delay tracking - wait after ACK before moving to next node
    bool awaiting_settling;
    std::chrono::steady_clock::time_point settling_start_time;
    
    // Dwell tracking - stay on node with data
    int dwell_count;       // Current consecutive upload count from same node
    int max_dwell_count;   // Maximum times to dwell on a node with data (default 25)
    
    // Sampleset dwell tracking - prevent sampleset monopoly at end of list
    int sampleset_dwell_count;     // Consecutive samplesets sampled at end of list
    int max_sampleset_dwell_count; // Maximum samplesets to check before forcing reload (default 25)
    
    // Component managers
    SessionStateTracker state_tracker;
    SessionTimeoutTracker timeout_tracker;
    UploadCoordinator* upload_coord;
    NodeListManager* nodelist_mgr;
    CommandSequenceManager* cmd_seq_mgr;  // Now handles retry logic for single commands
    
    // Reference to core
    CTS1X* ts1x_core;
    
    // Config broadcasting
    ConfigBroadcaster config_broadcaster;
    bool config_broadcast_enabled;
    bool startup_broadcast_done;
    uint8_t config_erase_age;  // Age parameter for erase command

    bool monitor_mode;

    bool sample_sampleset(const Sampleset& sampleset);
};

#endif // SESSION_MANAGER_H
