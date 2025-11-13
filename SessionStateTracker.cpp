#include "SessionStateTracker.h"
#include "logger.h"
#include "StateLogger.h"

SessionStateTracker::SessionStateTracker()
    : current_state(STATE_IDLE),
      current_result(RESULT_PENDING)
{
}

SessionStateTracker::~SessionStateTracker()
{
}

const char* SessionStateTracker::state_to_string(SessionState state) const
{
    switch (state) {
        case STATE_IDLE: return "IDLE";
        case STATE_COMMAND_SEQUENCE: return "COMMAND_SEQUENCE";
        case STATE_DATA_UPLOAD_INIT: return "DATA_UPLOAD_INIT";
        case STATE_DATA_UPLOAD_ACTIVE: return "DATA_UPLOAD_ACTIVE";
        case STATE_DATA_UPLOAD_RETRY: return "DATA_UPLOAD_RETRY";
        case STATE_DATA_UPLOAD_COMPLETE: return "DATA_UPLOAD_COMPLETE";
        case STATE_ERROR: return "ERROR";
        default: return "UNKNOWN";
    }
}

void SessionStateTracker::transition_state(SessionState new_state, const std::string& reason)
{
    if (new_state != current_state) {
        LOG_INFO_CTX("session_state", "STATE TRANSITION: %s -> %s | Reason: %s",
                     state_to_string(current_state),
                     state_to_string(new_state),
                     reason.c_str());
        
        // Log to state logger
        LOG_STATE("SESSION STATE: %s -> %s | %s", 
                  state_to_string(current_state),
                  state_to_string(new_state),
                  reason.c_str());
        
        current_state = new_state;
    }
}

void SessionStateTracker::log_session_event(const std::string& message, uint32_t macid)
{
    LOG_INFO_CTX("session_state", "[Node 0x%08x] %s", macid, message.c_str());
}

void SessionStateTracker::reset()
{
    transition_state(STATE_IDLE, "State reset");
    current_result = RESULT_PENDING;
}
