#include "StateLogger.h"
#include <cstdio>
#include <ctime>
#include <sys/stat.h>
#include <unistd.h>

StateLogger& StateLogger::instance() {
    static StateLogger inst;
    return inst;
}

StateLogger::StateLogger()
    : log_directory("/srv/UPTIMEDRIVE/logs"),
      log_file(nullptr),
      current_size(0)
{
}

StateLogger::~StateLogger() {
    if (log_file) {
        fclose(log_file);
        log_file = nullptr;
    }
}

void StateLogger::init(const std::string& log_dir) {
    log_directory = log_dir;
    log_filepath = log_directory + "/ts1_states.log";
    
    // Open log file in append mode
    log_file = fopen(log_filepath.c_str(), "a");
    if (!log_file) {
        fprintf(stderr, "Failed to open state log: %s\n", log_filepath.c_str());
        return;
    }
    
    // Get current file size
    struct stat st;
    if (stat(log_filepath.c_str(), &st) == 0) {
        current_size = st.st_size;
    }
    
    // Write startup marker
    log_event("========================================");
    log_event("State Logger Started");
    log_event("========================================");
}

void StateLogger::rotate_if_needed() {
    if (current_size < MAX_LOG_SIZE) {
        return;
    }
    
    if (log_file) {
        fclose(log_file);
        log_file = nullptr;
    }
    
    // Rotate logs: ts1_states.log.4 -> ts1_states.log.5, etc.
    for (int i = MAX_ROTATIONS - 1; i > 0; i--) {
        std::string old_name = log_filepath + "." + std::to_string(i);
        std::string new_name = log_filepath + "." + std::to_string(i + 1);
        rename(old_name.c_str(), new_name.c_str());
    }
    
    // Move current log to .1
    std::string backup_name = log_filepath + ".1";
    rename(log_filepath.c_str(), backup_name.c_str());
    
    // Open new log file
    log_file = fopen(log_filepath.c_str(), "a");
    current_size = 0;
    
    if (log_file) {
        log_event("========================================");
        log_event("Log Rotated");
        log_event("========================================");
    }
}

void StateLogger::write_timestamp() {
    if (!log_file) return;
    
    time_t now = time(nullptr);
    struct tm* tm_info = localtime(&now);
    
    char timestamp[64];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);
    
    fprintf(log_file, "[%s] ", timestamp);
}

void StateLogger::log_event(const char* format, ...) {
    if (!log_file) return;
    
    rotate_if_needed();
    
    write_timestamp();
    
    va_list args;
    va_start(args, format);
    int written = vfprintf(log_file, format, args);
    va_end(args);
    
    fprintf(log_file, "\n");
    fflush(log_file);
    
    if (written > 0) {
        current_size += written + 1; // +1 for newline
    }
}

void StateLogger::flush() {
    if (log_file) {
        fflush(log_file);
    }
}
