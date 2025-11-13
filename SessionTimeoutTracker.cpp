#include "SessionTimeoutTracker.h"
#include "ConfigManager.h"

// Note: Default session response timeout is defined in LinkTimingConstants.h
// as LinkTiming::SESSION_RESPONSE_TIMEOUT_MS (500ms)
// The actual timeout value is configurable via ConfigManager

SessionTimeoutTracker::SessionTimeoutTracker()
{
    reset_timer();
}

SessionTimeoutTracker::~SessionTimeoutTracker()
{
}

bool SessionTimeoutTracker::check_timeout() const
{
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::microseconds>(now - send_time).count();
    
    // Get configured timeout (defaults to SESSION_RESPONSE_TIMEOUT_MS if not configured)
    int timeout = ConfigManager::instance().get_response_timeout_ms() * 1000;
    return elapsed > timeout;
}

void SessionTimeoutTracker::reset_timer()
{
    send_time = std::chrono::steady_clock::now();
}

int64_t SessionTimeoutTracker::get_elapsed_ms() const
{
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(now - send_time).count();
}
