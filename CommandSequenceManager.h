#ifndef COMMAND_SEQUENCE_MANAGER_H
#define COMMAND_SEQUENCE_MANAGER_H

#include <chrono>

/**
 * CommandSequenceManager - Simplified retry-based command transmission
 * 
 * Manages sending a single command with configurable retry logic:
 * - Send command
 * - Wait for delay period
 * - If no ACK received, retry up to max_attempts times
 * - Move on if ACK received or max attempts exhausted
 * 
 * This pattern will be reused for all command types (R, I, etc.)
 * 
 * Optional secondary command feature:
 * - Can specify a secondary command and bitmask
 * - Bitmask indicates which retry attempts should use secondary command
 * - Bit 0 = attempt 0, bit 1 = attempt 1, etc.
 * - Limited to 32 attempts (uint32_t mask size)
 * - Example: primary='R', secondary='a', mask=0x5554 produces: r,r,a,r,a,r,a,...
 */
class CommandSequenceManager {
public:
    CommandSequenceManager();
    ~CommandSequenceManager();
    
    /**
     * Start transmission of a command with retry logic
     * @param command - The primary command character to send (e.g., 'R')
     * @param delay_ms - Milliseconds to wait between transmission attempts
     * @param max_attempts - Maximum number of times to send commands
     * @param secondary_command - Optional alternate command (0 = disabled)
     * @param command_mask - Bitmask controlling when to use secondary command
     *                       Bit N set = use secondary command on attempt N
     *                       (limited to first 32 attempts due to uint32_t size)
     */
    void start_command_transmission(char command, int delay_ms, int max_attempts,
                                   char secondary_command = 0, uint32_t command_mask = 0);
    
    /**
     * Check if enough time has elapsed to send the next command attempt
     * @return true if delay has elapsed and we should send
     */
    bool is_ready_to_send() const;
    
    /**
     * Get the current command character to send
     * @return command character (e.g., 'R')
     */
    char get_command() const;
    
    /**
     * Mark that we just sent the command (updates timing and attempt counter)
     * Call this immediately after sending the command
     */
    void mark_command_sent();
    
    /**
     * Notify that an ACK was received from the remote unit
     * This stops further retry attempts
     */
    void record_ack_received();
    
    /**
     * Check if transmission is complete (either ACK received or max attempts exhausted)
     * @return true if we're done with this command
     */
    bool is_transmission_complete() const;
    
    /**
     * Get current attempt number (1-indexed)
     */
    int get_current_attempt() const { return current_attempt; }
    
    /**
     * Get maximum attempts configured
     */
    int get_max_attempts() const { return max_attempts; }
    
    /**
     * Check if ACK was received
     */
    bool has_ack() const { return ack_received; }
    
    /**
     * Reset the manager for next command transmission
     */
    void reset();
    
private:
    // Current command being transmitted
    char current_command;
    
    // Optional secondary command and mask
    char secondary_command;   // Alternate command (0 = disabled)
    uint32_t command_mask;    // Bitmask: bit N set = use secondary on attempt N
    
    // Timing
    int delay_ms;                                        // Delay between transmission attempts
    std::chrono::steady_clock::time_point last_send_time; // When we last sent the command
    
    // Retry tracking
    int max_attempts;      // Maximum number of send attempts
    int current_attempt;   // Current attempt number (1-indexed)
    bool ack_received;     // Has remote unit acknowledged?
    
    // State tracking
    bool transmission_active;  // Is a transmission in progress?
};

#endif // COMMAND_SEQUENCE_MANAGER_H
