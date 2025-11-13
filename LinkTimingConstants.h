#ifndef LINK_TIMING_CONSTANTS_H
#define LINK_TIMING_CONSTANTS_H

#include <cstdint>

/**
 * @file LinkTimingConstants.h
 * @brief Centralized timing constants for all RF protocol and system timing
 * 
 * This file contains all timing-related constants used throughout the system.
 * Constants are organized by functional area for clarity.
 * 
 * IMPORTANT: This is the single source of truth for all timing values.
 * Do not duplicate these constants in other files.
 */

namespace LinkTiming {

//=============================================================================
// UPLOAD PROTOCOL TIMEOUTS
//=============================================================================
// These constants control the data upload protocol timing for receiving
// packets from remote sensor nodes.

// Time to wait for the first packet after sending 0x51 init command
constexpr int UPLOAD_INITIAL_TIMEOUT_MS = 250;

// Minimum timeout between packets during active upload
// This is a floor value - adaptive timeouts cannot go below this
constexpr int UPLOAD_MIN_PACKET_TIMEOUT_MS = 250;

// Adaptive packet timeout for normal upload conditions
// Used when completion rate > 90% or in normal case (50-90% completion)
// Field data shows occasional gaps up to 1137ms from remote units
constexpr int UPLOAD_PACKET_TIMEOUT_NORMAL_MS = 250;

// Adaptive packet timeout for high packet loss conditions
// Used when completion rate < 50% (major packet loss or slow start)
constexpr int UPLOAD_PACKET_TIMEOUT_HIGH_LOSS_MS = 500;

// Completion rate thresholds for adaptive timeout selection
constexpr double UPLOAD_HIGH_COMPLETION_THRESHOLD = 0.90;  // Above this: use normal timeout
constexpr double UPLOAD_LOW_COMPLETION_THRESHOLD = 0.50;   // Below this: use high-loss timeout

// Expected time interval between consecutive packets (nominal)
constexpr int UPLOAD_PACKET_INTERVAL_MS = 25;

// Expected number of retry attempts per segment (assumes 95% packet loss = 5% success rate)
constexpr int UPLOAD_EXPECTED_RETRIES_PER_SEGMENT = 100;

// Global timeout calculation: expected_time * MULTIPLIER
constexpr int UPLOAD_GLOBAL_TIMEOUT_MULTIPLIER = 15;

// Absolute maximum upload time (8 minutes) - safety limit
constexpr int UPLOAD_GLOBAL_TIMEOUT_MAX_MS = 480000;

// Maximum segments that can be requested in a single 0x55 command
// Limited by bitmask size (76 bytes × 7 bits = 532 segments)
constexpr int UPLOAD_MAX_SEGMENTS_PER_0X55 = 532;

// Timeout for upload coordinator state transitions (ms)
// Time to wait after initializing upload before sending 0x51
constexpr int64_t UPLOAD_INIT_STATE_TIMEOUT_MS = 120;

// Time to wait after sending 0x51 before sending initial 0x55 data request
constexpr int64_t UPLOAD_ACTIVE_STATE_TIMEOUT_MS = 150;

// Settling time after TX before sending retry command (allows ACKs to clear)
constexpr int UPLOAD_TX_SETTLING_MS = 30;

// Timeout waiting for response after sending 0x55 retry command (ms)
// Longer than packet timeout since remote unit needs time to process command
constexpr int UPLOAD_RETRY_TIMEOUT_MS = 1000;

// Maximum number of 0x55 partial upload commands that can be sent
// Set very high (10000) to effectively disable - global timeout (8 min) is the real limit
constexpr int UPLOAD_MAX_RETRY_COUNT = 10000;

//=============================================================================
// UPLOAD DATA FORMAT CONSTANTS
//=============================================================================
// Constants defining the structure of upload data packets and segments

// Number of data samples in each upload segment
constexpr int UPLOAD_SAMPLES_PER_SEGMENT = 32;

// Number of bytes per data sample (16-bit samples)
constexpr int UPLOAD_BYTES_PER_SAMPLE = 2;

// Total bytes per segment (derived: 32 samples × 2 bytes/sample)
constexpr int UPLOAD_BYTES_PER_SEGMENT = 64;

// Samples per unit in descriptor field decoding
// Remote unit formula: data_length = ((descriptor & 0xFF) + 1) * 256 samples
constexpr int UPLOAD_SAMPLES_PER_DESCRIPTOR_UNIT = 256;

//=============================================================================
// BITMAP OPTIMIZATION PARAMETERS
//=============================================================================
// Parameters for optimizing 0x55 partial upload command bitmaps
// These control the segment scanning strategy to maximize bitmap density

// Scan stride for finding optimal start segment
// Uses 28 (a divisor of 532) to align with natural bitmap boundaries
// 532 = 4 × 7 × 19, so divisors include: 1, 2, 4, 7, 14, 19, 28, 38, 76, 133, 266, 532
// Using divisor ensures we check natural chunk boundaries after receiving full bitmaps
constexpr int BITMAP_SCAN_STRIDE = 28;

// Minimum missing segments before using optimization (below this, just use first missing)
constexpr int BITMAP_OPTIMIZATION_THRESHOLD = 10;

//=============================================================================
// COMMAND TRANSMISSION TIMING (RF Protocol)
//=============================================================================
// These control command retry behavior for single command transmissions.
// Used by CommandSequenceManager for sending commands with automatic retry.

// Delay between 'R' command transmission attempts (1.8 seconds)
constexpr int CMD_R_RETRY_DELAY_MS = 1800;

// Maximum number of times to send 'R' command before giving up (general case)
constexpr int CMD_R_MAX_ATTEMPTS = 8;

// TS1X-specific command transmission parameters
// TS1X units benefit from alternating 'R' and 'a' commands to maintain wake state
constexpr int CMD_R_MAX_ATTEMPTS_TS1X = 15;

// Bitmask for TS1X alternating command pattern: r, r, a, r, a, r, a, ...
// Bits 2, 4, 6, 8, 10, 12, 14 are set (use 'a' on these attempts)
// Binary: 00000000000000000101010101010100
constexpr uint32_t CMD_R_TS1X_ALTERNATING_MASK = 0x00005554;

// Settling delay after ACK received before moving to next node
// This allows multiple ACKs from the remote unit to clear out
constexpr int CMD_SETTLING_DELAY_MS = 500;

//=============================================================================
// SESSION TIMEOUTS
//=============================================================================
// Timeouts for command/response cycles in normal session operations

// Default timeout waiting for command response (configurable via config file)
constexpr int SESSION_RESPONSE_TIMEOUT_MS = 500;

// Default maximum number of consecutive uploads from same node before moving to next
// This "dwell" mechanism allows efficient draining of nodes with multiple datasets
constexpr int SESSION_DEFAULT_DWELL_COUNT = 25;

//=============================================================================
// SYSTEM POLLING AND SLEEP INTERVALS
//=============================================================================
// Various sleep/poll intervals used in system loops

// Polling interval for session manager and config broadcaster loops
constexpr int SESSION_POLL_DELAY_MS = 100;

} // namespace LinkTiming

#endif // LINK_TIMING_CONSTANTS_H
