#ifndef UPLOAD_TYPES_H
#define UPLOAD_TYPES_H

// Upload session state
enum UploadState {
    UPLOAD_IDLE,
    UPLOAD_INIT,                // Session initialized, waiting to send 0x51
    UPLOAD_COMMAND_SENT,        // Sent 0x51 or 0x55
    UPLOAD_RECEIVING,           // Receiving data packets
    UPLOAD_RETRY_PARTIAL        // Need to request missing segments
};

// Retry decision results
enum RetryDecision {
    DECISION_WAIT,          // Keep waiting, no action needed
    DECISION_RETRY_FULL,    // Send 0x51 again (full upload)
    DECISION_RETRY_PARTIAL  // Send 0x55 (partial upload)
};

#endif // UPLOAD_TYPES_H
