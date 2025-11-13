/*******************************************************************************
 * command_definitions.h
 * 
 * Protocol Command Byte Definitions
 * 
 * This file defines all command bytes that appear at position 45 (COMMAND_START)
 * in the packet structure. Each command is defined as both a character constant
 * and its hexadecimal value for clarity.
 * 
 * Packet Structure:
 *   Bytes 0-44:  Header, MAC, padding
 *   Byte 45:     Command character (defined here)
 *   Bytes 46-125: Command body/data
 *   Bytes 126-127: Tail "uP"
 * 
 ******************************************************************************/

#ifndef COMMAND_DEFINITIONS_H
#define COMMAND_DEFINITIONS_H

/*******************************************************************************
 * Direction: BASE → UNIT (Commands sent from base station to remote units)
 ******************************************************************************/

/** Wake up command - signals battery-powered nodes to wake up */
#define CMD_WAKEUP              'A'     // 0x41 (formerly "Ax"); 'a' is 0x61
#define CMD_WAKEUP_LC           'a'     // 0x61 (lowercase variant)

/** Sample data request - requests sensor data from unit */
#define CMD_SAMPLE_DATA         'R'     // 0x52 (formerly "Rx"); 'r' is 0x72
#define CMD_SAMPLE_DATA_LC      'r'     // 0x72 (lowercase variant)

/** Sleep command - puts unit into low power sleep mode */
#define CMD_SLEEP               'S'     // 0x53 (formerly "Sx"); 's' is 0x73
#define CMD_SLEEP_LC            's'     // 0x73 (lowercase variant)

/** Reset command - resets the unit */
#define CMD_RESET               'X'     // 0x58 (formerly "Xx"); 'x' is 0x78
#define CMD_RESET_LC            'x'     // 0x78 (lowercase variant)

/** Erase config - clears old configuration files from unit */
#define CMD_ERASE_CFG           'E'     // 0x45 (formerly "Ex"); 'e' is 0x65
#define CMD_ERASE_CFG_LC        'e'     // 0x65 (lowercase variant)

/** Initialize unit - probe command */
#define CMD_INITIALIZE          'I'     // 0x49 (formerly "Ix"); 'i' is 0x69
#define CMD_INITIALIZE_LC       'i'     // 0x69 (lowercase variant)

/** Upload partial - partial file upload */
#define CMD_UPLOAD_PARTIAL      'U'     // 0x55 (formerly "Ux"); 'u' is 0x75
#define CMD_UPLOAD_PARTIAL_LC   'u'     // 0x75 (lowercase variant)e

/** Broadcast halt - stops broadcast mode */
#define CMD_BROADCAST_HALT      'H'     // 0x48 (formerly "Hx"); 'h' is 0x68
#define CMD_BROADCAST_HALT_LC   'h'     // 0x68 (lowercase variant)

/*******************************************************************************
 * Direction: UNIT → BASE (Commands sent from remote units to base station)
 ******************************************************************************/

/** Acknowledge/Initialize - unit responds with initialization data */
#define CMD_ACK_INIT            '1'     // 0x31 (unit response to Initialize command)

/** Data upload segment - unit sends data segment */
#define CMD_DATA_UPLOAD         '3'     // 0x33 (unit uploads data segment)

/** Data response - unit sends sensor readings */
#define CMD_DATA_RESPONSE       'D'     // 0x44 (formerly "Dx"); 'd' is 0x64
#define CMD_DATA_RESPONSE_LC    'd'     // 0x64 (lowercase variant)

/** Acknowledgment response - general ACK from unit */
#define CMD_ACK                 'K'     // 0x4B (formerly "Kx"); 'k' is 0x6B
#define CMD_ACK_LC              'k'     // 0x6B (lowercase variant)

/*******************************************************************************
 * Bidirectional Commands (Can be sent either direction)
 ******************************************************************************/

/** No operation - keepalive/ping */
#define CMD_NOP                 'N'     // 0x4E (formerly "Nx"); 'n' is 0x6E
#define CMD_NOP_LC              'n'     // 0x6E (lowercase variant)

/** Status request/response */
#define CMD_STATUS              'O'     // 0x4F (formerly "Ox"); 'o' is 0x6F
#define CMD_STATUS_LC           'o'     // 0x6F (lowercase variant)

/** Ping command */
#define CMD_PING                'p'     // 0x70 (formerly "px")

/*******************************************************************************
 * File Transfer Commands
 ******************************************************************************/

/** Upload file - initiate file upload */
#define CMD_UPLOAD_INIT              'Q'     // 0x51 (formerly "Qx"); 'q' is 0x71
#define CMD_UPLOAD_INIT_LC           'q'     // 0x71 (lowercase variant)

/** Upload done - complete file upload */
#define CMD_UPLOAD_DONE         'Z'     // 0x5A (formerly "Zx"); 'z' is 0x7A
#define CMD_UPLOAD_DONE_LC      'z'     // 0x7A (lowercase variant)

/** File read/write operations */
#define CMD_FILE_READWRITE      'B'     // 0x42 (formerly "Bx"); 'b' is 0x62
#define CMD_FILE_READWRITE_LC   'b'     // 0x62 (lowercase variant)

/** Echo upload RSSI files */
#define CMD_ECHO_UPLOAD_RSSI    'F'     // 0x46 (formerly "Fx"); 'f' is 0x66
#define CMD_ECHO_UPLOAD_RSSI_LC 'f'     // 0x66 (lowercase variant)



/*******************************************************************************
 * Configuration Commands
 ******************************************************************************/

/** Initialize repeater */
#define CMD_INIT_REPEATER       'K'     // 0x4B (formerly "Kx") - NOTE: Same as ACK; 'k' is 0x6B

/** Broadcast configuration - same as DATA_RESPONSE */
#define CMD_BROADCAST           'D'     // 0x44 (formerly "Dx") - NOTE: Same as DATA_RESPONSE; 'd' is 0x64

/** Crypto operations */
#define CMD_CRYPTO              'c'     // 0x63 (formerly "cx")

/*******************************************************************************
 * Hardware Control Commands
 ******************************************************************************/

/** LED control */
#define CMD_LED                 'L'     // 0x4C (formerly "Lx"); 'l' is 0x6C
#define CMD_LED_LC              'l'     // 0x6C (lowercase variant)

/** Gear/hardware settings */
#define CMD_GEAR                'g'     // 0x67 (formerly "gx")

/** Radio configuration - NOTE: Same byte as RESET */
#define CMD_RADIO               'X'     // 0x58 (formerly "Xx"); 'x' is 0x78

/** Low power mode enable - NOTE: Same byte as RESET/RADIO */
#define CMD_LPOW                'X'     // 0x58 (formerly "X0"); 'x' is 0x78

/** Low power mode disable */
#define CMD_LPOW_OFF            'Y'     // 0x59 (formerly "Y0"); 'y' is 0x79
#define CMD_LPOW_OFF_LC         'y'     // 0x79 (lowercase variant)

/** EchoBase shell command - NOTE: Same byte as LPOW_OFF */
#define CMD_ECHOBASE_SHELL      'Y'     // 0x59 (formerly "Yx"); 'y' is 0x79

/*******************************************************************************
 * Command Validation Helpers
 ******************************************************************************/

/** Check if command is a wake/sample command */
#define IS_WAKE_COMMAND(cmd)    ((cmd) == CMD_WAKEUP || (cmd) == CMD_WAKEUP_LC || (cmd) == 'A' || (cmd) == 'a')
#define IS_SAMPLE_COMMAND(cmd)  ((cmd) == CMD_SAMPLE_DATA || (cmd) == CMD_SAMPLE_DATA_LC || (cmd) == 'R' || (cmd) == 'r')
#define IS_SLEEP_COMMAND(cmd)   ((cmd) == CMD_SLEEP || (cmd) == CMD_SLEEP_LC || (cmd) == 'S' || (cmd) == 's')
#define IS_RESET_COMMAND(cmd)   ((cmd) == CMD_RESET || (cmd) == CMD_RESET_LC || (cmd) == 'X' || (cmd) == 'x')
#define IS_ERASE_COMMAND(cmd)   ((cmd) == CMD_ERASE_CFG || (cmd) == CMD_ERASE_CFG_LC || (cmd) == 'E' || (cmd) == 'e')
#define IS_INIT_COMMAND(cmd)    ((cmd) == CMD_INITIALIZE || (cmd) == CMD_INITIALIZE_LC || (cmd) == 'I' || (cmd) == 'i')
#define IS_DATA_COMMAND(cmd)    ((cmd) == CMD_DATA_RESPONSE || (cmd) == CMD_DATA_RESPONSE_LC || (cmd) == 'D' || (cmd) == 'd')
#define IS_ACK_INIT_COMMAND(cmd) ((cmd) == CMD_ACK_INIT || (cmd) == '1')
#define IS_ACK_COMMAND(cmd)     ((cmd) == CMD_ACK || (cmd) == CMD_ACK_LC || (cmd) == 'K' || (cmd) == 'k')
#define IS_DATA_UPLOAD_COMMAND(cmd) ((cmd) == CMD_DATA_UPLOAD || (cmd) == '3')

/*******************************************************************************
 * Command Byte Position in Packet
 ******************************************************************************/

#ifndef COMMAND_START
#define COMMAND_START           45      // Command byte position in packet
#endif

#endif // COMMAND_DEFINITIONS_H
