#ifndef LOGGER_H
#define LOGGER_H

#include <stdio.h>
#include <stdarg.h>
#include <time.h>
#include <sys/time.h>
#include <string.h>
#include <pthread.h>
#include <string>

class SimpleLogger {
private:
    FILE* log_file;
    char log_path[256];
    size_t max_file_size;
    int max_files;
    pthread_mutex_t log_mutex;

    void rotate_logs() {
        fclose(log_file);

        // Rotate existing log files
        char old_name[300], new_name[300];
        for (int i = max_files - 1; i > 0; i--) {
            snprintf(old_name, sizeof(old_name), "%s.%d", log_path, i - 1);
            snprintf(new_name, sizeof(new_name), "%s.%d", log_path, i);
            rename(old_name, new_name);
        }

        // Move current log to .0
        snprintf(new_name, sizeof(new_name), "%s.0", log_path);
        rename(log_path, new_name);

        // Open new log file
        log_file = fopen(log_path, "a");
    }

    void check_rotation() {
        if (log_file) {
            fseek(log_file, 0, SEEK_END);
            long size = ftell(log_file);
            if (size >= (long)max_file_size) {
                rotate_logs();
            }
        }
    }

    
    void log_internal(const char* level, const char* context, const char* format, va_list args) {
        pthread_mutex_lock(&log_mutex);

        check_rotation();

        if (!log_file) {
            pthread_mutex_unlock(&log_mutex);
            return;
        }

        // Get timestamp with milliseconds
        struct timeval tv;
        gettimeofday(&tv, NULL);
        struct tm* tm_info = localtime(&tv.tv_sec);

        char timestamp[64];
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tm_info);

        // Write log entry
        va_list args2;
        va_copy(args2, args);

        /* file */
        fprintf(log_file, "%s,%03ld - %s - %s - ",
                timestamp, tv.tv_usec / 1000, context, level);
        vfprintf(log_file, format, args);
        fprintf(log_file, "\n");
        fflush(log_file);

        /* console (stderr) */
        fprintf(stderr, "%s,%03ld - %s - %s - ",
                timestamp, tv.tv_usec / 1000, context, level);
        vfprintf(stderr, format, args2);
        fprintf(stderr, "\n");
        fflush(stderr);

        va_end(args2);

        pthread_mutex_unlock(&log_mutex);
    }

public:
    SimpleLogger(const char* path, size_t max_size_kb = 256, int num_files = 5)
        : max_file_size(max_size_kb * 1024), max_files(num_files) {
        strncpy(log_path, path, sizeof(log_path) - 1);
        log_file = fopen(log_path, "a");
        pthread_mutex_init(&log_mutex, NULL);
    }

    ~SimpleLogger() {
        if (log_file) {
            fclose(log_file);
        }
        pthread_mutex_destroy(&log_mutex);
    }
    void debug(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("DEBUG", "pi_server", format, args);
        va_end(args);
    }

    void debug_ctx(const char* context, const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("DEBUG", context, format, args);
        va_end(args);
    }

    void info(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("INFO", "pi_server", format, args);
        va_end(args);
    }

    void info_ctx(const char* context, const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("INFO", context, format, args);
        va_end(args);
    }

    void warn(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("WARN", "pi_server", format, args);
        va_end(args);
    }

    void warn_ctx(const char* context, const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("WARN", context, format, args);
        va_end(args);
    }

    void error(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("ERROR", "pi_server", format, args);
        va_end(args);
    }

    void error_ctx(const char* context, const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("ERROR", context, format, args);
        va_end(args);
    }

    void critical(const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("CRITICAL", "pi_server", format, args);
        va_end(args);
    }

    void critical_ctx(const char* context, const char* format, ...) {
        va_list args;
        va_start(args, format);
        log_internal("CRITICAL", context, format, args);
        va_end(args);
    }
    
    // Raw write method for header logger (writes directly without timestamp/level formatting)
    void write_raw(const char* line) {
        pthread_mutex_lock(&log_mutex);
        
        check_rotation();
        
        if (log_file) {
            fprintf(log_file, "%s\n", line);
            fflush(log_file);
        }
        
        pthread_mutex_unlock(&log_mutex);
    }
};

// Get logger instance (defined in logger.cpp)
SimpleLogger* get_logger();

// Get header logger instance (defined in logger.cpp)
SimpleLogger* get_header_logger();

// Initialize logger - call this once in main() with log directory from config
void init_logger(const std::string& log_directory = "/srv/UPTIMEDRIVE/logs");
void cleanup_logger();

// Standard macros (default context: pi_server)
#define LOG_DEBUG(...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->debug(__VA_ARGS__); \
} while(0)

#define LOG_INFO(...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->info(__VA_ARGS__); \
} while(0)

#define LOG_WARN(...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->warn(__VA_ARGS__); \
} while(0)

#define LOG_ERROR(...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->error(__VA_ARGS__); \
} while(0)

#define LOG_CRITICAL(...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->critical(__VA_ARGS__); \
} while(0)

// Context macros (custom context)
#define LOG_DEBUG_CTX(context, ...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->debug_ctx(context, __VA_ARGS__); \
} while(0)

#define LOG_INFO_CTX(context, ...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->info_ctx(context, __VA_ARGS__); \
} while(0)

#define LOG_WARN_CTX(context, ...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->warn_ctx(context, __VA_ARGS__); \
} while(0)

#define LOG_ERROR_CTX(context, ...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->error_ctx(context, __VA_ARGS__); \
} while(0)

#define LOG_CRITICAL_CTX(context, ...) do { \
    SimpleLogger* logger = get_logger(); \
    if (logger) logger->critical_ctx(context, __VA_ARGS__); \
} while(0)

#endif // LOGGER_H
