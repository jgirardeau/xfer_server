#ifndef SESSION_TIMEOUT_TRACKER_H
#define SESSION_TIMEOUT_TRACKER_H

#include <chrono>

class SessionTimeoutTracker
{
public:
    SessionTimeoutTracker();
    ~SessionTimeoutTracker();
    
    // Timeout checking
    bool check_timeout() const;
    
    // Timer management
    void reset_timer();
    std::chrono::steady_clock::time_point get_send_time() const { return send_time; }
    void set_send_time(std::chrono::steady_clock::time_point time) { send_time = time; }
    
    // Get elapsed time in milliseconds
    int64_t get_elapsed_ms() const;
    
private:
    std::chrono::steady_clock::time_point send_time;
};

#endif // SESSION_TIMEOUT_TRACKER_H
