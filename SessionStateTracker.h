#ifndef SESSION_STATE_TRACKER_H
#define SESSION_STATE_TRACKER_H

#include <string>
#include <cstdint>

// Session states
enum SessionState {
    STATE_IDLE,
    STATE_COMMAND_SEQUENCE,      // Command sequencing (polling)
    STATE_DATA_UPLOAD_INIT,      // Initialize upload
    STATE_DATA_UPLOAD_ACTIVE,    // Receiving upload data
    STATE_DATA_UPLOAD_RETRY,     // Retry missing segments
    STATE_DATA_UPLOAD_COMPLETE,  // Upload finished
    STATE_ERROR
};

// Session results
enum SessionResult {
    RESULT_PENDING,
    RESULT_SUCCESS,
    RESULT_NO_RESPONSE,
    RESULT_NO_DATA_READY,
    RESULT_ERROR
};

class SessionStateTracker
{
public:
    SessionStateTracker();
    ~SessionStateTracker();
    
    // State management
    SessionState get_state() const { return current_state; }
    SessionResult get_result() const { return current_result; }
    void set_result(SessionResult result) { current_result = result; }
    
    // State transitions with logging
    void transition_state(SessionState new_state, const std::string& reason);
    
    // State string conversion
    const char* state_to_string(SessionState state) const;
    
    // Logging
    void log_session_event(const std::string& message, uint32_t macid);
    
    // Reset
    void reset();
    
private:
    SessionState current_state;
    SessionResult current_result;
};

#endif // SESSION_STATE_TRACKER_H
