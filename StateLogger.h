#ifndef STATE_LOGGER_H
#define STATE_LOGGER_H

#include <string>
#include <cstdarg>

// State logger for high-level state machine tracking
// Logs to ts1_states.log with rotation
class StateLogger {
public:
    static StateLogger& instance();
    
    // Initialize with log directory
    void init(const std::string& log_dir);
    
    // Log a state machine event
    void log_event(const char* format, ...);
    
    // Flush the log
    void flush();
    
private:
    StateLogger();
    ~StateLogger();
    
    void rotate_if_needed();
    void write_timestamp();
    
    std::string log_directory;
    std::string log_filepath;
    FILE* log_file;
    size_t current_size;
    
    static const size_t MAX_LOG_SIZE = 10 * 1024 * 1024; // 10MB
    static const int MAX_ROTATIONS = 5;
    
    StateLogger(const StateLogger&) = delete;
    StateLogger& operator=(const StateLogger&) = delete;
};

// Convenience macro for state logging
#define LOG_STATE(...) StateLogger::instance().log_event(__VA_ARGS__)

#endif // STATE_LOGGER_H
