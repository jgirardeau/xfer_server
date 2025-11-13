#!/bin/bash

# Script to find usage of all constants from LinkTimingConstants.h
# Searches in .h and .cpp files

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
BLUE='\033[0;34m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

# Array of all constants from LinkTimingConstants.h
constants=(
    "UPLOAD_INITIAL_TIMEOUT_MS"
    "UPLOAD_MIN_PACKET_TIMEOUT_MS"
    "UPLOAD_PACKET_TIMEOUT_NORMAL_MS"
    "UPLOAD_PACKET_TIMEOUT_HIGH_LOSS_MS"
    "UPLOAD_HIGH_COMPLETION_THRESHOLD"
    "UPLOAD_LOW_COMPLETION_THRESHOLD"
    "UPLOAD_PACKET_INTERVAL_MS"
    "UPLOAD_EXPECTED_RETRIES_PER_SEGMENT"
    "UPLOAD_GLOBAL_TIMEOUT_MULTIPLIER"
    "UPLOAD_GLOBAL_TIMEOUT_MAX_MS"
    "UPLOAD_MAX_SEGMENTS_PER_0X55"
    "UPLOAD_INIT_STATE_TIMEOUT_MS"
    "UPLOAD_ACTIVE_STATE_TIMEOUT_MS"
    "UPLOAD_TX_SETTLING_MS"
    "UPLOAD_RETRY_TIMEOUT_MS"
    "UPLOAD_MAX_RETRY_COUNT"
    "UPLOAD_SAMPLES_PER_SEGMENT"
    "UPLOAD_BYTES_PER_SAMPLE"
    "UPLOAD_BYTES_PER_SEGMENT"
    "UPLOAD_SAMPLES_PER_DESCRIPTOR_UNIT"
    "BITMAP_SCAN_STRIDE"
    "BITMAP_OPTIMIZATION_THRESHOLD"
    "CMD_R_RETRY_DELAY_MS"
    "CMD_R_MAX_ATTEMPTS"
    "CMD_SETTLING_DELAY_MS"
    "SESSION_RESPONSE_TIMEOUT_MS"
    "SESSION_DEFAULT_DWELL_COUNT"
    "SESSION_POLL_DELAY_MS"
)

# Get search directory from command line, default to current directory
SEARCH_DIR="${1:-.}"

echo -e "${BLUE}==============================================================================${NC}"
echo -e "${BLUE}Searching for LinkTimingConstants usage in: ${SEARCH_DIR}${NC}"
echo -e "${BLUE}==============================================================================${NC}"
echo

# Loop through each constant
for constant in "${constants[@]}"; do
    echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    echo -e "${GREEN}Searching for: ${constant}${NC}"
    echo -e "${YELLOW}━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━${NC}"
    
    # Search for the constant, excluding the definition file itself
    results=$(grep -rn --include="*.h" --include="*.cpp" --exclude="LinkTimingConstants.h" \
        "$constant" "$SEARCH_DIR" 2>/dev/null)
    
    if [ -z "$results" ]; then
        echo -e "${RED}  No usage found (only defined in LinkTimingConstants.h)${NC}"
    else
        echo "$results" | while IFS= read -r line; do
            # Extract filename and line number
            file=$(echo "$line" | cut -d: -f1)
            linenum=$(echo "$line" | cut -d: -f2)
            content=$(echo "$line" | cut -d: -f3-)
            
            echo -e "  ${BLUE}${file}:${linenum}${NC}"
            echo -e "    ${content}"
        done
    fi
    echo
done

echo -e "${BLUE}==============================================================================${NC}"
echo -e "${BLUE}Search complete${NC}"
echo -e "${BLUE}==============================================================================${NC}"
