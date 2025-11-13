/**
 * @file UnitType.h
 * @brief Global unit type identification based on MAC address
 * 
 * This header provides a unified system for determining unit types from MAC addresses.
 * Can be included throughout the codebase for consistent unit type checking.
 */

#ifndef UNIT_TYPE_H
#define UNIT_TYPE_H

#include <cstdint>

/**
 * @enum UNIT_TYPE
 * @brief Enumeration of supported unit types identified by MAC address patterns
 */
enum UNIT_TYPE {
    UNIT_TYPE_TS1X,      // MAC: 0x00xxxxxx (legacy sensor units)
    UNIT_TYPE_CRONOS,    // MAC: 0x00bxxxxx (Cronos units)
    UNIT_TYPE_MISTLX,    // MAC: 0xbbxxxxxx (MistLX units)
    UNIT_TYPE_ECHOBOX,   // MAC: 0xbcxxxxxx (EchoBox units)
    UNIT_TYPE_STORMX,    // MAC: 0xbaxxxxxx (StormX base units)
    UNIT_TYPE_STORMXT,   // MAC: 0xbexxxxxx, 0xbfxxxxxx (StormXT variant units)
    UNIT_TYPE_UNKNOWN    // No matching pattern
};

/**
 * @brief Determine unit type from MAC address
 * 
 * This function examines the MAC address pattern to identify the unit type.
 * The MAC address high bytes encode the unit type:
 * 
 * - 0x00bxxxxx = CRONOS (checked first, most specific TS1X variant)
 * - 0x00xxxxxx = TS1X (legacy units)
 * - 0xbbxxxxxx = MistLX
 * - 0xbcxxxxxx = EchoBox
 * - 0xbexxxxxx, 0xbfxxxxxx = StormXT (checked first, more specific)
 * - 0xbaxxxxxx = StormX (base variant)
 * 
 * @param macid 32-bit MAC address to check
 * @return UNIT_TYPE enum value identifying the unit type
 * 
 * @note CRONOS is checked before TS1X since CRONOS MAC addresses are a 
 *       subset of TS1X addresses (0x00bxxxxx matches 0x00xxxxxx)
 * @note STORMXT is checked before STORMX to distinguish variants
 */
inline UNIT_TYPE get_unit_type(uint32_t macid)
{
    // Check CRONOS first (more specific than TS1X)
    // CRONOS: 0x00b00000 to 0x00bfffff
    if ((macid & 0xfff00000) == 0x00b00000) {
        return UNIT_TYPE_CRONOS;
    }
    
    // TS1X: 0x00000000 to 0x00ffffff (but CRONOS already handled)
    if ((macid & 0xff000000) == 0x00000000) {
        return UNIT_TYPE_TS1X;
    }
    
    // MistLX: 0xbb000000 to 0xbbffffff
    if ((macid & 0xff000000) == 0xbb000000) {
        return UNIT_TYPE_MISTLX;
    }
    
    // EchoBox: 0xbc000000 to 0xbcffffff
    if ((macid & 0xff000000) == 0xbc000000) {
        return UNIT_TYPE_ECHOBOX;
    }
    
    // StormXT: 0xbe or 0xbf prefix (check before StormX)
    uint32_t high_byte = macid & 0xff000000;
    if (high_byte == 0xbe000000 || high_byte == 0xbf000000) {
        return UNIT_TYPE_STORMXT;
    }
    
    // StormX: 0xba prefix only
    if (high_byte == 0xba000000) {
        return UNIT_TYPE_STORMX;
    }
    
    // No match
    return UNIT_TYPE_UNKNOWN;
}

/**
 * @brief Convert unit type enum to human-readable string
 * 
 * @param type Unit type enum value
 * @return Const string name of the unit type
 */
inline const char* unit_type_to_string(UNIT_TYPE type)
{
    switch (type) {
        case UNIT_TYPE_TS1X:     return "TS1X";
        case UNIT_TYPE_CRONOS:   return "CRONOS";
        case UNIT_TYPE_MISTLX:   return "MistLX";
        case UNIT_TYPE_ECHOBOX:  return "EchoBox";
        case UNIT_TYPE_STORMX:   return "StormX";
        case UNIT_TYPE_STORMXT:  return "StormXT";
        case UNIT_TYPE_UNKNOWN:  return "UNKNOWN";
        default:                 return "INVALID";
    }
}

/**
 * @brief Check if MAC address is a TS1X unit (legacy function for backward compatibility)
 * @param macid 32-bit MAC address
 * @return true if TS1X unit (0x00xxxxxx, excluding CRONOS 0x00bxxxxx)
 */
inline bool is_ts1x(uint32_t macid) {
    return get_unit_type(macid) == UNIT_TYPE_TS1X;
}

/**
 * @brief Check if MAC address is a CRONOS unit (legacy function for backward compatibility)
 * @param macid 32-bit MAC address
 * @return true if CRONOS unit (0x00bxxxxx)
 */
inline bool is_cronos(uint32_t macid) {
    return get_unit_type(macid) == UNIT_TYPE_CRONOS;
}

/**
 * @brief Check if MAC address is a MistLX unit (legacy function for backward compatibility)
 * @param macid 32-bit MAC address
 * @return true if MistLX unit (0xbbxxxxxx)
 */
inline bool is_mistlx(uint32_t macid) {
    return get_unit_type(macid) == UNIT_TYPE_MISTLX;
}

/**
 * @brief Check if MAC address is an EchoBox unit (legacy function for backward compatibility)
 * @param macid 32-bit MAC address
 * @return true if EchoBox unit (0xbcxxxxxx)
 */
inline bool is_echobox(uint32_t macid) {
    return get_unit_type(macid) == UNIT_TYPE_ECHOBOX;
}

/**
 * @brief Check if MAC address is a StormX unit (legacy function for backward compatibility)
 * @param macid 32-bit MAC address
 * @return true if StormX unit (0xbaxxxxxx only, not StormXT)
 * @note Changed from original: now only returns true for 0xba, not 0xbe/0xbf
 */
inline bool is_stormx(uint32_t macid) {
    return get_unit_type(macid) == UNIT_TYPE_STORMX;
}

/**
 * @brief Check if MAC address is a StormXT unit
 * @param macid 32-bit MAC address
 * @return true if StormXT unit (0xbexxxxxx or 0xbfxxxxxx)
 */
inline bool is_stormxt(uint32_t macid) {
    return get_unit_type(macid) == UNIT_TYPE_STORMXT;
}

/**
 * @brief Check if MAC address is any StormX family unit (StormX or StormXT)
 * @param macid 32-bit MAC address
 * @return true if StormX or StormXT unit (0xbaxxxxxx, 0xbexxxxxx, or 0xbfxxxxxx)
 * @note This is the new family grouping function that matches the old is_stormx() behavior
 */
inline bool is_stormx_family(uint32_t macid) {
    UNIT_TYPE type = get_unit_type(macid);
    return (type == UNIT_TYPE_STORMX || type == UNIT_TYPE_STORMXT);
}

#endif // UNIT_TYPE_H
