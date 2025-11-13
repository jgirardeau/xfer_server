#include "SessionManager.h"
#include "TS1X.h"
#include "CommandProcessor.h"
#include "UploadManager.h"
#include "NodeListManager.h"
#include "CommandSequenceManager.h"
#include "ConfigManager.h"
#include "LinkTimingConstants.h"
#include "UnitType.h"
#include "logger.h"
#include "StateLogger.h"
#include "SamplesetSupervisor.h"
#include "command_definitions.h"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include "pi_server_sleep.h"

// External reference to global sampleset supervisor (defined in main.cpp)
extern SamplesetSupervisor* g_sampleset_supervisor;

SessionManager::SessionManager(CTS1X* core)
    : current_macid(0),
      retry_count(0),
      upload_counter(0),
      dwell_count(0),
      max_dwell_count(LinkTiming::SESSION_DEFAULT_DWELL_COUNT),
      sampleset_dwell_count(0),
      max_sampleset_dwell_count(LinkTiming::SESSION_DEFAULT_DWELL_COUNT),
      ts1x_core(core),
      config_broadcast_enabled(false),
      startup_broadcast_done(false),
      config_erase_age(24),
      monitor_mode(false),
      awaiting_settling(false)
{
    LOG_INFO_CTX("session_mgr", "SessionManager initialized");
    
    // Initialize state logger
    std::string log_dir = ConfigManager::instance().get_log_directory();
    StateLogger::instance().init(log_dir);
    LOG_STATE("=== SessionManager Initialized ===");
    
    // Create managers
    upload_coord = new UploadCoordinator(core);
    nodelist_mgr = new NodeListManager();
    cmd_seq_mgr = new CommandSequenceManager();
    
    // Get nodelist filename from config and set it
    // Use ConfigManager's helper which constructs: nodelist_directory + "/nodelist_force.txt"
    std::string nodelist_file = ConfigManager::instance().get_node_list_file();
    
    nodelist_mgr->set_node_list_file(nodelist_file);
    
    // Get dwell count from config (optional, default from LinkTiming constants)
    max_dwell_count = ConfigManager::instance().get("session.dwell_count", 
                                                     LinkTiming::SESSION_DEFAULT_DWELL_COUNT);
    
    LOG_INFO_CTX("session_mgr", "Node list file configured as: %s", nodelist_file.c_str());
    LOG_INFO_CTX("session_mgr", "Max dwell count: %d", max_dwell_count);
    LOG_INFO_CTX("session_mgr", "Command retry config: R_delay=%dms, R_attempts=%d",
                 LinkTiming::CMD_R_RETRY_DELAY_MS, LinkTiming::CMD_R_MAX_ATTEMPTS);
}

SessionManager::~SessionManager()
{
    delete upload_coord;
    delete nodelist_mgr;
    delete cmd_seq_mgr;
}

void SessionManager::set_monitor_mode(bool enable)
{
    monitor_mode = enable;
    if (monitor_mode) {
        LOG_INFO_CTX("session_mgr", "Monitor mode ENABLED - No TX responses, no config broadcasts");
    } else {
        LOG_INFO_CTX("session_mgr", "Monitor mode disabled - Normal operation");
    }
}

void SessionManager::initialize_config_broadcaster(const std::string& config_dir,
                                                   unsigned char rssi_threshold,
                                                   unsigned char rssi_delay,
                                                   unsigned char rssi_increment,
                                                   unsigned char power_adjust,
                                                   int broadcast_interval_hours)
{
    if (config_broadcaster.Initialize(config_dir, rssi_threshold, rssi_delay, 
                                     rssi_increment, power_adjust, broadcast_interval_hours)) {
        config_broadcast_enabled = true;
        LOG_INFO_CTX("session_mgr", "Config broadcaster initialized from: %s", config_dir.c_str());
        LOG_INFO_CTX("session_mgr", "Broadcast interval: %d hours", broadcast_interval_hours);
    } else {
        config_broadcast_enabled = false;
        LOG_INFO_CTX("session_mgr", "WARNING: Config broadcasting disabled - directory not found: %s", 
                     config_dir.c_str());
    }
}

void SessionManager::broadcast_config_files()
{
    if (!config_broadcast_enabled) {
        LOG_INFO_CTX("session_mgr", "Config broadcasting is disabled");
        return;
    }
    
    // Don't broadcast if there are no nodes in the nodelist_force
    if (!nodelist_mgr->has_nodes()) {
        LOG_INFO_CTX("session_mgr", "Skipping config broadcast - no nodes in nodelist_force.txt");
        return;
    }
    
    // First, erase old config files
    erase_old_config_files(config_erase_age);

    LOG_INFO_CTX("session_mgr", "=== Broadcasting Config Files ===");
    config_broadcaster.BroadcastAllConfigs(ts1x_core);
    LOG_INFO_CTX("session_mgr", "=== Config Broadcast Complete ===");
}

bool SessionManager::check_periodic_broadcast()
{
    if (!config_broadcast_enabled) {
        return false;
    }
    
    return config_broadcaster.IsTimeForPeriodicBroadcast();
}

bool SessionManager::send_command()
{
    char cmd = cmd_seq_mgr->get_command();
    
    unsigned char cmd_buffer[128];
    if (!ts1x_core->get_command_processor()->make_command(cmd_buffer, cmd, current_macid)) {
        state_tracker.log_session_event("Error: Failed to create command", current_macid);
        return false;
    }
    
    ts1x_core->send_command(cmd_buffer, 128);
    LOG_STATE("TX: '%c' command to node 0x%08X (attempt %d/%d)", 
              cmd, current_macid, 
              cmd_seq_mgr->get_current_attempt() + 1,
              cmd_seq_mgr->get_max_attempts());
    LOG_INFO_CTX("session_mgr", "TX: '%c' command to node 0x%08X (attempt %d/%d)", 
                 cmd, current_macid,
                 cmd_seq_mgr->get_current_attempt() + 1,
                 cmd_seq_mgr->get_max_attempts());

    cmd_seq_mgr->mark_command_sent();
    
    return true;
}

void SessionManager::reset_session()
{
    state_tracker.reset();
    retry_count = 0;
    upload_counter = 0;
}

void SessionManager::handle_response(const CommandResponse& response)
{
    if (response.source_macid != current_macid) {
        LOG_INFO_CTX("session_mgr", "Warning: Response from unexpected node 0x%08x (expected 0x%08x)",
                     response.source_macid, current_macid);
        return;
    }
    
    // Note: Legacy QUERY protocol handlers removed - current code uses command sequences
    // If needed for future use, responses are handled in process() function
}

void SessionManager::process(CommandResponse* response)
{
    if (!monitor_mode) {  // Monitor mode: skip TX processing
        if (response != nullptr) {
            // Log combined state information when processing a response
            LOG_INFO_CTX("session_mgr", "Processing response | SessionMgr: %s | UploadMgr: %s",
                         state_tracker.state_to_string(state_tracker.get_state()),
                         upload_coord->get_upload_manager()->state_to_string());
            
            SessionState current_state = state_tracker.get_state();
            
            // Handle ACK responses ('1') from any state
            if (response->command_code == CMD_ACK_INIT) {
                // Let UploadCoordinator handle the response and decide on state transitions
                upload_coord->handle_r_command_response(*response, state_tracker);
                
                // Notify command manager that ACK was received (stops retries)
                if (current_state == STATE_COMMAND_SEQUENCE) {
                    cmd_seq_mgr->record_ack_received();
                    
                    // Check if upload was initiated (node has data)
                    if (state_tracker.get_state() == STATE_DATA_UPLOAD_INIT) {
                        // Upload starting - cancel any settling delay
                        awaiting_settling = false;
                        LOG_INFO_CTX("session_mgr", 
                                    "Node 0x%08x has data - initiating upload (cancelled settling)",
                                    current_macid);
                    } else {
                        // ACK received but no data - we'll move to next node after settling
                        LOG_INFO_CTX("session_mgr",
                                    "Node 0x%08x ACK received with NO data - will move to next node after settling",
                                    current_macid);
                    }
                }
                
                // Update nodelist if needed
                if (upload_coord->has_pending_upload()) {
                    NodeInfo* node = nodelist_mgr->find_node_by_macid(response->source_macid);
                    if (node) {
                        node->has_data_ready = true;
                    }
                    current_macid = response->source_macid;
                }
            }
            // Handle upload data packets ('3')
            else if (current_state == STATE_DATA_UPLOAD_ACTIVE || 
                     current_state == STATE_DATA_UPLOAD_RETRY) {
                // During upload, only process upload data packets (command '3')
                // Ignore ACK_INIT responses (command '1') which may arrive during settling period
                if (response->command_code == CMD_DATA_UPLOAD) {
                    upload_coord->get_upload_manager()->process_upload_response(*response);
                    
                    // Don't bypass state machine - transition to COMPLETE state
                    // Let process_state_machine() handle file writing and dwell logic
                    if (upload_coord->get_upload_manager()->is_complete()) {
                        state_tracker.transition_state(STATE_DATA_UPLOAD_COMPLETE,
                                                      "All segments received");
                    }
                    // Note: Stuck segment detection and retry logic is handled by
                    // UploadCoordinator's process_upload_active() method via timeout tracking
                }
                else if (response->command_code == CMD_ACK_INIT) {
                    // Ignore stray '1' responses during upload settling period
                    LOG_INFO_CTX("session_mgr", "Ignoring stray ACK_INIT ('1') during upload state");
                }
                else {
                    LOG_INFO_CTX("session_mgr", "Received unexpected command code: '%c' (0x%02x) during upload",
                                response->command_code, response->command_code);
                }
            }
            else {
                // Unexpected command code in other states
                if (response->command_code != CMD_ACK_INIT && response->command_code != CMD_DATA_UPLOAD) {
                    LOG_INFO_CTX("session_mgr", "Received unexpected command code: '%c' (0x%02x)",
                                response->command_code, response->command_code);
                }
            }
        }
    }
    
    // Run the state machine
    process_state_machine();
}

void SessionManager::process_state_machine()
{
    SessionState current_state = state_tracker.get_state();
    
    switch (current_state) {
        case STATE_IDLE:
            if (!monitor_mode) {  // Monitor mode: skip TX processing
                // === Startup config broadcast (only once) ===
                if (config_broadcast_enabled && !startup_broadcast_done) {
                    LOG_INFO_CTX("session_mgr", "=== Performing Startup Config Broadcast ===");
                    broadcast_config_files();
                    startup_broadcast_done = true;
                    LOG_INFO_CTX("session_mgr", "=== Startup Broadcast Complete ===");
                }
                
                // === Periodic config broadcast ===
                if (check_periodic_broadcast()) {
                    LOG_INFO_CTX("session_mgr", "=== Time for Periodic Config Broadcast ===");
                    broadcast_config_files();
                    LOG_INFO_CTX("session_mgr", "=== Periodic Broadcast Complete ===");
                }
                
                // === Try to load nodelist if not loaded ===
                if (!nodelist_mgr->has_nodes()) {
                    if (nodelist_mgr->should_attempt_load()) {
                        LOG_INFO_CTX("session_mgr", "Attempting to load node list...");
                        if (nodelist_mgr->load_node_list()) {
                            LOG_INFO_CTX("session_mgr", "Node list loaded successfully: %zu EchoBase nodes", 
                                        nodelist_mgr->get_node_count());
                        } else {
                            LOG_DEBUG_CTX("session_mgr", "No node list file or empty - will retry later");
                        }
                    }
                }
                
                // === Determine operational mode ===
                bool has_nodelist = nodelist_mgr->has_nodes();
                bool has_samplesets = (g_sampleset_supervisor && 
                                      g_sampleset_supervisor->get_sampleset_count() > 0);
                
                // MODE 1: Neither nodelist nor samplesets - do nothing except periodic refreshes
                if (!has_nodelist && !has_samplesets) {
                    LOG_DEBUG_CTX("session_mgr", "Mode 1: No nodelist, no samplesets - waiting");
                    break;
                }
                
                // MODE 3: No nodelist, but has samplesets - sample samplesets only
                if (!has_nodelist && has_samplesets) {
                    const Sampleset* sampleset = g_sampleset_supervisor->get_sampleset();
                    if (sampleset != nullptr) {
                        LOG_INFO_CTX("session_mgr", "Mode 3: Sampling sampleset - Node 0x%08x, mask=0x%02x, %s",
                                    sampleset->nodeid,
                                    sampleset->sampling_mask,
                                    sampleset->ac_dc_flag ? "AC" : "DC");
                        
                        if (sample_sampleset(*sampleset)) {
                            g_sampleset_supervisor->record_sample(*sampleset);
                        }
                    }
                    break;
                }
                
                // === MODE 2 & 4: Has nodelist (with or without samplesets) ===
                
                // Check if at end of nodelist
                if (nodelist_mgr->is_at_end()) {
                    LOG_INFO_CTX("session_mgr", "Reached end of node list");
                    
                    // MODE 4: If we have samplesets, check them before reloading nodelist
                    if (has_samplesets) {
                        // Check if we've hit sampleset dwell limit
                        if (sampleset_dwell_count >= max_sampleset_dwell_count) {
                            LOG_INFO_CTX("session_mgr", 
                                        "Sampleset dwell limit reached (%d samples), forcing reload to prevent nodelist starvation",
                                        sampleset_dwell_count);
                            sampleset_dwell_count = 0;
                            // Skip to reload
                        } else {
                            const Sampleset* sampleset = g_sampleset_supervisor->get_sampleset();
                            if (sampleset != nullptr) {
                                LOG_INFO_CTX("session_mgr", 
                                            "Mode 4: Sampling sampleset before reloading nodelist - Node 0x%08x, mask=0x%02x, %s (potential dwell %d/%d)",
                                            sampleset->nodeid,
                                            sampleset->sampling_mask,
                                            sampleset->ac_dc_flag ? "AC" : "DC",
                                            sampleset_dwell_count + 1,
                                            max_sampleset_dwell_count);
                                
                                if (sample_sampleset(*sampleset)) {
                                    g_sampleset_supervisor->record_sample(*sampleset);
                                }
                                // After sampling, we'll come back here next iteration to continue with samplesets
                                // or reload the nodelist if no more samplesets need sampling or dwell limit hit
                                break;
                            }
                            // sampleset == nullptr means no samplesets due, reset counter and reload
                            sampleset_dwell_count = 0;
                        }
                    }
                    
                    // No samplesets needing sampling (or no samplesets at all, or dwell limit hit), reload nodelist
                    LOG_INFO_CTX("session_mgr", "Reloading node list...");
                    nodelist_mgr->check_and_reload_if_at_end();
                    if (!nodelist_mgr->has_nodes()) {
                        LOG_WARN_CTX("session_mgr", "Node list reload failed or empty");
                        break;
                    }
                    LOG_INFO_CTX("session_mgr", "Node list reloaded: %zu EchoBase nodes", 
                                nodelist_mgr->get_node_count());
                    sampleset_dwell_count = 0;  // Reset for next end-of-list cycle
                }
                
                // === Process current node from nodelist ===
                current_macid = nodelist_mgr->get_current_macid();
                
                LOG_INFO_CTX("session_mgr", "Mode %d: Sampling EchoBase node %zu/%zu: 0x%08x", 
                            has_samplesets ? 4 : 2,
                            nodelist_mgr->get_current_index() + 1, 
                            nodelist_mgr->get_node_count(), 
                            current_macid);
                
                // EchoBase units: Standard 'R' command only
                // (TS1X/StormX special handling removed - they're now managed via samplesets)
                cmd_seq_mgr->start_command_transmission(
                    CMD_SAMPLE_DATA, 
                    LinkTiming::CMD_R_RETRY_DELAY_MS,
                    LinkTiming::CMD_R_MAX_ATTEMPTS
                );
                
                // Reset settling delay flag
                awaiting_settling = false;
                
                state_tracker.transition_state(STATE_COMMAND_SEQUENCE, 
                                              "Starting 'R' command transmission");
                
                // Send first command immediately
                send_command();
            }
            break;
            
        case STATE_COMMAND_SEQUENCE:
            // Check if transmission is complete
            if (cmd_seq_mgr->is_transmission_complete()) {
                // Start settling delay if we just completed
                if (!awaiting_settling) {
                    awaiting_settling = true;
                    settling_start_time = std::chrono::steady_clock::now();
                    
                    if (cmd_seq_mgr->has_ack()) {
                        LOG_INFO_CTX("session_mgr", 
                                    "Command transmission complete for node 0x%08x (ACK received) - settling for %dms", 
                                    current_macid, LinkTiming::CMD_SETTLING_DELAY_MS);
                    } else {
                        LOG_WARN_CTX("session_mgr", 
                                    "Command transmission complete for node 0x%08x (NO ACK after %d attempts) - settling for %dms", 
                                    current_macid, cmd_seq_mgr->get_max_attempts(), LinkTiming::CMD_SETTLING_DELAY_MS);
                    }
                }
                
                // Check if settling delay has elapsed
                auto now = std::chrono::steady_clock::now();
                auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                    now - settling_start_time).count();
                
                if (elapsed >= LinkTiming::CMD_SETTLING_DELAY_MS) {
                    // Settling complete - now move on
                    awaiting_settling = false;
                    
                    LOG_INFO_CTX("session_mgr",
                                "Settling complete for node 0x%08x after %lld ms - moving to next node",
                                current_macid, elapsed);
                    
                    // Reset command manager
                    cmd_seq_mgr->reset();
                    
                    // Reset upload manager before moving to next node
                    upload_coord->get_upload_manager()->reset();
                    
                    // Reset dwell count (no data found or node didn't respond)
                    dwell_count = 0;
                    
                    // CRITICAL: Move to next node in the list
                    uint32_t old_macid = current_macid;
                    nodelist_mgr->move_to_next_node();
                    uint32_t new_macid = nodelist_mgr->get_current_macid();
                    
                    LOG_INFO_CTX("session_mgr",
                                "Advanced from node 0x%08x to node 0x%08x (index %zu/%zu)",
                                old_macid, new_macid,
                                nodelist_mgr->get_current_index() + 1,
                                nodelist_mgr->get_node_count());
                    
                    const char* reason = cmd_seq_mgr->has_ack() ? 
                        "Command sequence completed (no data), moving to next node" :
                        "No response from node, moving to next node";
                    
                    state_tracker.transition_state(STATE_IDLE, reason);
                }
                // else: still waiting for settling delay to complete
                
                break;
            }
            
            // Check if ready to send next retry
            if (cmd_seq_mgr->is_ready_to_send()) {
                send_command();
            }
            break;
            
        case STATE_DATA_UPLOAD_INIT:
            upload_coord->process_upload_init(state_tracker, timeout_tracker, current_macid);
            break;
            
        case STATE_DATA_UPLOAD_ACTIVE:
            upload_coord->process_upload_active(state_tracker, timeout_tracker, current_macid);
            break;
            
        case STATE_DATA_UPLOAD_RETRY:
            upload_coord->process_upload_retry(state_tracker, current_macid);
            break;
            
        case STATE_DATA_UPLOAD_COMPLETE:
        {
            // UNIFIED completion path for all upload scenarios
            // This is now the ONLY way uploads complete (no more bypass)
            upload_coord->complete_upload_and_write_files(current_macid, "COMPLETE");
            
            // Reset upload manager for next upload (important for dwell logic)
            upload_coord->get_upload_manager()->reset();
            
            // Reset command manager (in case we were mid-command when upload started)
            cmd_seq_mgr->reset();
            
            // Determine if this was an EchoBase node or a TS1X/StormX sampleset
            bool is_echobase_node = nodelist_mgr->is_in_node_list(current_macid);
            bool is_sampleset_node = (current_macid != 0 && !is_echobase_node);
            
            if (is_echobase_node) {
                // Increment EchoBase dwell count
                dwell_count++;
                LOG_INFO_CTX("session_mgr", "Upload complete from EchoBase node 0x%08x (dwell %d/%d)",
                            current_macid, dwell_count, max_dwell_count);
                
                // Only move to next node if we've hit the dwell limit
                if (dwell_count >= max_dwell_count) {
                    LOG_INFO_CTX("session_mgr", "Max dwell count reached, moving to next node");
                    dwell_count = 0;  // Reset for next node
                    nodelist_mgr->move_to_next_node();
                }
            } else if (is_sampleset_node) {
                // Increment sampleset dwell count
                sampleset_dwell_count++;
                LOG_INFO_CTX("session_mgr", "Upload complete from sampleset node 0x%08x (sampleset dwell %d/%d)",
                            current_macid, sampleset_dwell_count, max_sampleset_dwell_count);
                
                // Samplesets don't advance nodes - we keep sampling until dwell limit or no more due
                // The dwell check happens at the top of the main loop
            } else {
                LOG_WARN_CTX("session_mgr", "Upload complete from unknown node type 0x%08x", current_macid);
            }
            
            // Return to IDLE to query (same or next) node
            state_tracker.transition_state(STATE_IDLE, 
                                          "Upload completed successfully, returning to polling");
            break;
        }
            
        case STATE_ERROR:
            LOG_ERROR_CTX("session_mgr", "Error state reached for node 0x%08x, moving to next node",
                        current_macid);
            
            // Reset upload manager so it's ready for next upload attempt
            upload_coord->get_upload_manager()->reset();
            
            // Reset command manager
            cmd_seq_mgr->reset();
            
            // Reset settling flag
            awaiting_settling = false;
            
            // Reset dwell count before moving to next node (error occurred)
            dwell_count = 0;
            nodelist_mgr->move_to_next_node();
            state_tracker.transition_state(STATE_IDLE, "Error recovery - moving to next node");
            state_tracker.set_result(RESULT_PENDING);
            break;
    }
}

void SessionManager::erase_old_config_files(uint8_t age)
{
    std::vector<std::string> config_files = config_broadcaster.GetConfigFiles();
    if(config_files.empty())return;
    
    LOG_INFO_CTX("session_mgr", "=== Erasing Old Config Files (age=%d) ===", age);
    
    unsigned char erase_cmd[128];
    
    // Send erase command with delays
    for (int i = 0; i < 4; i++) {
        // Create erase command (macid=0 for broadcast)
        if (ts1x_core->get_command_processor()->make_erase_command(erase_cmd, age)) {
            erase_cmd[125]=i+1;
            ts1x_core->send_command(erase_cmd, 128);
            LOG_INFO_CTX("session_mgr", "Erase command sent (%d)", i + 1);
        } else {
            LOG_ERROR_CTX("session_mgr", "Failed to create erase command");
            break;
        }
        ts1x_core->flush_tx_buffer();
        Server_sleep_ms(LinkTiming::SESSION_POLL_DELAY_MS);
    }
    
    LOG_INFO_CTX("session_mgr", "=== Erase Commands Complete ===");
}

/*
 * Sample a sampleset from a TS1X or StormX node
 * 
 * This function will handle sampling for non-EchoBase units that are managed
 * via the sampleset system (TS1X, StormX, etc.). Future implementation will:
 * 
 * 1. Check if we have an active session with sampleset->nodeid
 * 2. Build appropriate sample command based on sampleset parameters:
 *    - For TS1X: Use alternating R/a pattern with extended attempts
 *    - For StormX: TBD based on protocol requirements
 *    - AC/DC flag, sampling mask, frequency, resolution
 * 3. Send the command and wait for response
 * 4. Record the sample in the supervisor on success
 * 
 * Example TS1X sampling code (commented out for future use):
 * 
 *   if (get_unit_type(sampleset.nodeid) == UNIT_TYPE_TS1X) {
 *       // Build specialized TS1X sample command
 *       cmd_seq_mgr->start_command_transmission(
 *           CMD_SAMPLE_DATA,                          // Primary: 'R'
 *           LinkTiming::CMD_R_RETRY_DELAY_MS,         // 1800ms delay
 *           LinkTiming::CMD_R_MAX_ATTEMPTS_TS1X,      // 15 attempts
 *           CMD_WAKEUP_LC,                            // Secondary: 'a'
 *           LinkTiming::CMD_R_TS1X_ALTERNATING_MASK   // r,r,a,r,a,r,a pattern
 *       );
 *       
 *       // Set sampling parameters from sampleset
 *       // - sampling_mask: which channels to sample
 *       // - ac_dc_flag: AC vs DC mode
 *       // - max_freq: for AC channels
 *       // - resolution: for AC channels
 *       
 *       // Transition to command sequence state
 *       state_tracker.transition_state(STATE_COMMAND_SEQUENCE, 
 *                                     "Starting TS1X sampleset sampling");
 *       send_command();
 *       return true;
 *   }
 */
bool SessionManager::sample_sampleset(const Sampleset& sampleset) {
    LOG_DEBUG_CTX("session_mgr", "sample_sampleset called for node 0x%08x (placeholder - not yet implemented)",
                  sampleset.nodeid);
    
    // TODO: Implement actual sampling logic for TS1X/StormX units
    // For now, just return true to record the sample attempt
    return true;
}
