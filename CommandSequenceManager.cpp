#include "CommandSequenceManager.h"
#include "logger.h"
#include <chrono>

CommandSequenceManager::CommandSequenceManager()
    : current_command(0)
    , secondary_command(0)
    , command_mask(0)
    , delay_ms(0)
    , max_attempts(0)
    , current_attempt(0)
    , ack_received(false)
    , transmission_active(false)
{
    LOG_INFO_CTX("cmd_seq_mgr", "CommandSequenceManager initialized (retry-based mode)");
}

CommandSequenceManager::~CommandSequenceManager()
{
}

void CommandSequenceManager::start_command_transmission(char command, int delay_ms, int max_attempts,
                                                        char secondary_command, uint32_t command_mask)
{
    this->current_command = command;
    this->secondary_command = secondary_command;
    this->command_mask = command_mask;
    this->delay_ms = delay_ms;
    this->max_attempts = max_attempts;
    this->current_attempt = 0;
    this->ack_received = false;
    this->transmission_active = true;
    
    // Initialize timing so first send happens immediately
    this->last_send_time = std::chrono::steady_clock::now() - 
                           std::chrono::milliseconds(delay_ms);
    
    if (secondary_command != 0 && command_mask != 0) {
        LOG_INFO_CTX("cmd_seq_mgr", "Starting command '%c' transmission with secondary '%c': "
                     "delay=%dms, max_attempts=%d, mask=0x%08X",
                     command, secondary_command, delay_ms, max_attempts, command_mask);
    } else {
        LOG_INFO_CTX("cmd_seq_mgr", "Starting command '%c' transmission: delay=%dms, max_attempts=%d",
                     command, delay_ms, max_attempts);
    }
}

bool CommandSequenceManager::is_ready_to_send() const
{
    if (!transmission_active) {
        return false;
    }
    
    if (is_transmission_complete()) {
        return false;
    }
    
    // Check if enough time has elapsed since last send
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_send_time).count();
    
    return elapsed >= delay_ms;
}

char CommandSequenceManager::get_command() const
{
    // Check if we should use secondary command for this attempt
    // Mask is limited to first 32 attempts (uint32_t size)
    if (secondary_command != 0 && command_mask != 0 && current_attempt < 32) {
        // Check if bit for current_attempt is set in mask
        if (command_mask & (1 << current_attempt)) {
            return secondary_command;
        }
    }
    
    return current_command;
}

void CommandSequenceManager::mark_command_sent()
{
    if (!transmission_active) {
        LOG_ERROR_CTX("cmd_seq_mgr", "mark_command_sent() called but no transmission active");
        return;
    }
    
    // Get which command was sent (primary or secondary based on mask)
    char sent_command = get_command();
    
    current_attempt++;
    last_send_time = std::chrono::steady_clock::now();
    
    LOG_INFO_CTX("cmd_seq_mgr", "Command '%c' sent (attempt %d/%d)",
                 sent_command, current_attempt, max_attempts);
}

void CommandSequenceManager::record_ack_received()
{
    if (!transmission_active) {
        LOG_WARN_CTX("cmd_seq_mgr", "ACK received but no transmission active");
        return;
    }
    
    ack_received = true;
    
    LOG_INFO_CTX("cmd_seq_mgr", "ACK received for command '%c' after %d attempt(s)",
                 current_command, current_attempt);
}

bool CommandSequenceManager::is_transmission_complete() const
{
    if (!transmission_active) {
        return true;
    }
    
    // Complete if we received an ACK
    if (ack_received) {
        return true;
    }
    
    // Complete if we've exhausted all attempts
    if (current_attempt >= max_attempts) {
        return true;
    }
    
    return false;
}

void CommandSequenceManager::reset()
{
    current_command = 0;
    secondary_command = 0;
    command_mask = 0;
    delay_ms = 0;
    max_attempts = 0;
    current_attempt = 0;
    ack_received = false;
    transmission_active = false;
    
    LOG_INFO_CTX("cmd_seq_mgr", "Command transmission reset");
}
