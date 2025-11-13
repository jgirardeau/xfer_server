// MainLoopConstants.h
// System-level timing constants for pi_server main loop
// These constants control periodic operations, health checks, and loop behavior

#ifndef MAIN_LOOP_CONSTANTS_H
#define MAIN_LOOP_CONSTANTS_H

// ===== Periodic Operation Intervals =====

// Configuration file check interval (seconds)
// How often to check if sampleset configuration has been modified
constexpr int CONFIG_FILE_CHECK_INTERVAL_SEC = 120;  // 2 minutes

// Database flush interval (seconds)
// How often to flush accumulated data to persistent storage
constexpr int DATABASE_FLUSH_INTERVAL_SEC = 3600;  // 1 hour

// Ping file update frequency (loop iterations)
// Modulo counter for touching the ping file to indicate system is alive
constexpr int PING_FILE_UPDATE_MODULO = 1500;

// Radio startup retry delay (milliseconds)
// Delay between retry attempts when waiting for radio to become ready
constexpr int RADIO_STARTUP_RETRY_DELAY_MS = 200;

// Main loop fallback delay (microseconds)
// Used when main_loop_delay_us is 0 or invalid
constexpr int MAIN_LOOP_FALLBACK_DELAY_US = 10;


// ===== Configuration Validation Ranges =====
// These constants define acceptable ranges for configuration parameters

// Radio check period limits (seconds)
constexpr int RADIO_CHECK_MIN_SEC = 10;          // 10 seconds minimum
constexpr int RADIO_CHECK_MAX_SEC = 604800;      // 7 days maximum

// Buffer size limits (bytes)
constexpr int PI_BUFFER_MIN_SIZE = 1024;         // 1 KB minimum
constexpr int PI_BUFFER_MAX_SIZE = 8388608;      // 8 MB maximum
constexpr int CMD_BUFFER_MIN_SIZE = 1;
constexpr int CMD_BUFFER_MAX_SIZE = 4096;

// UART timing limits (microseconds)
constexpr int TIMER_INTERVAL_MIN_US = 100;
constexpr int TIMER_INTERVAL_MAX_US = 1000000;   // 1 second
constexpr int LOOP_DELAY_MIN_US = 0;
constexpr int LOOP_DELAY_MAX_US = 1000000;       // 1 second

// RSSI parameter limits
constexpr int RSSI_THRESHOLD_MIN = -128;
constexpr int RSSI_THRESHOLD_MAX = 127;
constexpr unsigned char RSSI_PARAM_MIN = 0;
constexpr unsigned char RSSI_PARAM_MAX = 255;

// Config broadcast interval limits (hours)
constexpr int BROADCAST_INTERVAL_MIN_HOURS = 1;
constexpr int BROADCAST_INTERVAL_MAX_HOURS = 168;  // 1 week

#endif // MAIN_LOOP_CONSTANTS_H
